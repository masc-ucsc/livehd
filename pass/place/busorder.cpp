// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "busorder.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
using Clock = std::chrono::steady_clock;
double              elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
std::vector<double> merge_lines(std::vector<double> values, double low, double high, double gap) {
  std::erase_if(values, [&](double p) { return p <= low || p >= high; });
  values.push_back(low);
  values.push_back(high);
  std::sort(values.begin(), values.end());
  while (values.size() > 2) {
    size_t best = 0;
    for (size_t i = 1; i + 1 < values.size(); ++i) {
      if (values[i + 1] - values[i] < values[best + 1] - values[best]) {
        best = i;
      }
    }
    if (values[best + 1] - values[best] > gap) {
      break;
    }
    values[best] = best == 0 ? low : best + 2 == values.size() ? high : (values[best] + values[best + 1]) / 2;
    values.erase(values.begin() + best + 1);
  }
  return values;
}
double coord(const dp::Design& d, int pin, int axis) {
  return axis == 0 ? d.node_x[d.pin2node[pin]] + d.pin_offset_x[pin] : d.node_y[d.pin2node[pin]] + d.pin_offset_y[pin];
}
bool ordered(const std::vector<Busorder_crossing>& rows) {
  bool ascending = true, descending = true;
  int  previous_low = 0, previous_high = 0;
  bool first = true;
  for (size_t i = 0; i < rows.size();) {
    size_t end = i + 1;
    int    low = rows[i].bit, high = low;
    while (end < rows.size() && rows[end].coordinate == rows[i].coordinate) {
      low  = std::min(low, rows[end].bit);
      high = std::max(high, rows[end].bit);
      ++end;
    }
    if (!first) {
      ascending  &= previous_high <= low;
      descending &= previous_low >= high;
    }
    first         = false;
    previous_low  = low;
    previous_high = high;
    i             = end;
  }
  return ascending || descending;
}
}  // namespace

void validate_busorder_options(const Busorder_options& o) {
  if (!o.iterations || o.iterations > 2000 || !o.refresh || o.refresh > o.iterations || !o.max_cells || o.max_cells > 1000000
      || !std::isfinite(o.weight) || o.weight < 0 || o.weight > 100 || !std::isfinite(o.merge_um) || o.merge_um < 0
      || !std::isfinite(o.max_displacement_um) || o.max_displacement_um <= 0) {
    throw std::invalid_argument("invalid busorder iterations/refresh/cell cap/weight/merge/displacement");
  }
}

std::optional<Bus_bit> indexed_bus_bit(const std::string& source) {
  std::string name;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\\' && i + 1 < source.size()) {
      ++i;
    }
    name.push_back(source[i]);
  }
  if (name.empty() || name.back() != ']') {
    return {};
  }
  const auto left = name.rfind('[');
  if (left == std::string::npos || left == 0 || left + 2 >= name.size()) {
    return {};
  }
  int        bit    = -1;
  const auto result = std::from_chars(name.data() + left + 1, name.data() + name.size() - 1, bit);
  if (result.ec != std::errc{} || result.ptr != name.data() + name.size() - 1 || bit < 0) {
    return {};
  }
  return Bus_bit{name.substr(0, left), bit};
}

std::vector<Busorder_edge> busorder_edges(const std::vector<Busorder_rect>& mesh) {
  std::vector<Busorder_edge> edges;
  using Span = std::pair<double, double>;
  std::map<std::pair<int, double>, std::set<Span>> boundaries;
  for (const auto& r : mesh) {
    for (double x : {r.xl, r.xh}) {
      boundaries[{0, x}].emplace(r.yl, r.yh);
    }
    for (double y : {r.yl, r.yh}) {
      boundaries[{1, y}].emplace(r.xl, r.xh);
    }
  }
  for (const auto& [line, spans] : boundaries) {
    std::set<double> ends;
    for (auto [lo, hi] : spans) {
      ends.insert(lo);
      ends.insert(hi);
    }
    for (auto it = ends.begin(); it != ends.end();) {
      const double lo = *it++;
      if (it == ends.end()) {
        break;
      }
      const double hi = *it;
      if (std::any_of(spans.begin(), spans.end(), [&](auto s) { return s.first <= lo && hi <= s.second; })) {
        edges.push_back({line.first, line.second, lo, hi});
      }
    }
  }
  return edges;
}

Busorder::Busorder(const dp::Design& d, const std::vector<int>& macros, const std::vector<Busorder_io>& ios,
                   const std::vector<Bus_bit>& identities, Busorder_route router, double units, Busorder_options policy,
                   std::string directory)
    : options(policy), dbu(units), bits(identities), route(std::move(router)), output(std::move(directory)) {
  const auto start = Clock::now();
  validate_busorder_options(options);
  d.validate();
  if (!(std::isfinite(dbu) && dbu > 0) || bits.size() + 1 != d.flat_net2pin_start.size() || !route) {
    throw std::invalid_argument("invalid busorder design/units/router");
  }
  for (int i = 0; i < d.num_movable; ++i) {
    if (!d.node_macro.empty() && d.node_macro[i]) {
      throw std::invalid_argument("busorder requires fixed macros");
    }
  }
  const auto&         l = d.layout;
  std::vector<double> raw[2];
  for (int i : macros) {
    if (i < d.num_movable || i >= int(d.node_x.size())) {
      throw std::invalid_argument("invalid busorder fixed macro index");
    }
    raw[0].insert(raw[0].end(), {d.node_x[i], d.node_x[i] + d.node_size_x[i]});
    raw[1].insert(raw[1].end(), {d.node_y[i], d.node_y[i] + d.node_size_y[i]});
  }
  std::map<std::pair<std::string, int>, std::pair<double, double>> buses;
  for (const auto& io : ios) {
    const double distances[] = {std::abs(io.x - l.xl), std::abs(io.x - l.xh), std::abs(io.y - l.yl), std::abs(io.y - l.yh)};
    const int    side        = int(std::min_element(std::begin(distances), std::end(distances)) - std::begin(distances));
    const int    axis        = side < 2 ? 1 : 0;
    const double p           = axis == 0 ? io.x : io.y;
    const auto   bit         = indexed_bus_bit(io.name);
    auto [it, added]         = buses.emplace(std::pair{bit ? bit->bus : io.name, side}, std::pair{p, p});
    if (!added) {
      it->second.first  = std::min(it->second.first, p);
      it->second.second = std::max(it->second.second, p);
    }
  }
  for (const auto& [key, span] : buses) {
    const int axis = key.second < 2 ? 1 : 0;
    raw[axis].insert(raw[axis].end(), {span.first, span.second});
  }
  const auto                    xs = merge_lines(raw[0], l.xl, l.xh, options.merge_um * dbu);
  const auto                    ys = merge_lines(raw[1], l.yl, l.yh, options.merge_um * dbu);
  const size_t                  nx = xs.size() - 1, ny = ys.size() - 1;
  std::vector<std::vector<int>> members(nx * ny);
  for (int i = 0; i < d.num_movable; ++i) {
    const double x = d.node_x[i] + d.node_size_x[i] / 2, y = d.node_y[i] + d.node_size_y[i] / 2;
    const size_t ix = std::clamp<std::ptrdiff_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin() - 1, 0, nx - 1);
    const size_t iy = std::clamp<std::ptrdiff_t>(std::upper_bound(ys.begin(), ys.end(), y) - ys.begin() - 1, 0, ny - 1);
    members[iy * nx + ix].push_back(i);
  }
  std::set<std::pair<int, double>> candidates;
  for (size_t i = 1; i + 1 < xs.size(); ++i) {
    candidates.emplace(0, xs[i]);
  }
  for (size_t i = 1; i + 1 < ys.size(); ++i) {
    candidates.emplace(1, ys[i]);
  }
  const auto refine = [&](auto&& self, Busorder_rect box, const std::vector<int>& cells, unsigned depth) -> void {
    if (cells.size() <= options.max_cells) {
      box.cells = cells.size();
      mesh.push_back(box);
      return;
    }
    if (depth >= 48) {
      throw std::runtime_error("busorder mesh cannot split coincident cell centers below the cell cap");
    }
    const bool       x   = box.xh - box.xl >= box.yh - box.yl;
    const double     mid = x ? (box.xl + box.xh) / 2 : (box.yl + box.yh) / 2;
    std::vector<int> low, high;
    for (int i : cells) {
      const double p = x ? d.node_x[i] + d.node_size_x[i] / 2 : d.node_y[i] + d.node_size_y[i] / 2;
      (p < mid ? low : high).push_back(i);
    }
    auto a = box, b = box;
    if (x) {
      a.xh = mid;
      b.xl = mid;
    } else {
      a.yh = mid;
      b.yl = mid;
    }
    candidates.emplace(x ? 0 : 1, mid);
    self(self, a, low, depth + 1);
    self(self, b, high, depth + 1);
  };
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      refine(refine, {xs[x], ys[y], xs[x + 1], ys[y + 1], 0}, members[y * nx + x], 0);
    }
  }
  initial_rectangles = mesh.size();
  // Rank distances ONCE using the original mesh, then alternate H / V.
  std::vector<std::tuple<double, double, double>> ranked[2];
  for (int axis : {0, 1}) {
    std::vector<double> positions{axis == 0 ? l.xl : l.yl, axis == 0 ? l.xh : l.yh};
    for (auto [a, p] : candidates) {
      if (a == axis) {
        positions.push_back(p);
      }
    }
    std::sort(positions.begin(), positions.end());
    for (size_t i = 1; i + 1 < positions.size(); ++i) {
      const double p = positions[i], gap = std::min(p - positions[i - 1], positions[i + 1] - p);
      ranked[axis].emplace_back(gap, axis == 1 ? -p : p, p);
    }
    std::sort(ranked[axis].begin(), ranked[axis].end());
  }
  for (size_t k = 0; k < std::max(ranked[0].size(), ranked[1].size()); ++k) {
    for (int axis : {1, 0}) {
      if (k >= ranked[axis].size()) {
        continue;
      }
      const double                                p = std::get<2>(ranked[axis][k]);
      std::map<std::pair<double, double>, size_t> low, high;
      for (size_t i = 0; i < mesh.size(); ++i) {
        const auto& r    = mesh[i];
        const auto  span = axis == 0 ? std::pair{r.yl, r.yh} : std::pair{r.xl, r.xh};
        if ((axis == 0 ? r.xh : r.yh) == p) {
          low.emplace(span, i);
        }
        if ((axis == 0 ? r.xl : r.yl) == p) {
          high.emplace(span, i);
        }
      }
      if (low.empty() || low.size() != high.size()) {
        continue;
      }
      bool valid = true;
      for (const auto& [span, i] : low) {
        const auto found = high.find(span);
        if (found == high.end() || mesh[i].cells + mesh[found->second].cells > options.max_cells) {
          valid = false;
          break;
        }
      }
      if (!valid) {
        continue;
      }
      std::vector<bool>          consumed(mesh.size());
      std::vector<Busorder_rect> next;
      for (const auto& [span, i] : low) {
        const auto j  = high.at(span);
        auto       r  = mesh[i];
        r.cells      += mesh[j].cells;
        if (axis == 0) {
          r.xh = mesh[j].xh;
        } else {
          r.yh = mesh[j].yh;
        }
        consumed[i] = consumed[j] = true;
        next.push_back(r);
      }
      for (size_t i = 0; i < mesh.size(); ++i) {
        if (!consumed[i]) {
          next.push_back(mesh[i]);
        }
      }
      mesh = std::move(next);
      ++(axis == 0 ? removed_v : removed_h);
    }
  }
  uint64_t total = 0;
  for (const auto& r : mesh) {
    if (r.cells > options.max_cells) {
      throw std::runtime_error("busorder mesh cell cap violated");
    }
    total += r.cells;
  }
  if (total != unsigned(d.num_movable)) {
    throw std::runtime_error("busorder mesh cell count mismatch");
  }
  edges         = busorder_edges(mesh);
  build_seconds = elapsed(start);
}

std::vector<Busorder_crossing> Busorder::crossings(const dp::Design& d, unsigned& high_degree) const {
  std::map<double, std::vector<int>> boundaries[2];
  for (size_t i = 0; i < edges.size(); ++i) {
    boundaries[edges[i].axis][edges[i].position].push_back(i);
  }
  std::vector<Busorder_crossing> result;
  for (size_t net = 0; net < bits.size(); ++net) {
    if (bits[net].bit < 0 || bits[net].bus.empty() || (!d.net_mask.empty() && !d.net_mask[net])) {
      continue;
    }
    const auto begin = d.flat_net2pin_start[net], end = d.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    if (end - begin > 1000) {
      ++high_degree;
      continue;
    }
    std::vector<Busorder_point>           points;
    std::map<long long, std::vector<int>> coordinates[2];
    for (int k = begin; k < end; ++k) {
      const int    pin = d.flat_net2pin[k];
      const double x = coord(d, pin, 0), y = coord(d, pin, 1);
      points.emplace_back(x, y);
      coordinates[0][std::llround(x)].push_back(pin);
      coordinates[1][std::llround(y)].push_back(pin);
    }
    std::set<std::tuple<int, double, int>> seen;
    const auto                             segment = [&](Busorder_point a, Busorder_point b) {
      if (a == b) {
        return;
      }
      const int    normal  = a.first == b.first ? 1 : 0;  // wire vertical crosses horizontal boundaries
      const int    tangent = 1 - normal;
      const double lo      = normal == 0 ? std::min(a.first, b.first) : std::min(a.second, b.second);
      const double hi      = normal == 0 ? std::max(a.first, b.first) : std::max(a.second, b.second);
      const double q       = tangent == 0 ? a.first : a.second;
      const auto   sources = coordinates[tangent].find(std::llround(q));
      if (sources == coordinates[tangent].end()) {
        return;
      }
      // FLUTE coordinates lie on the terminal Hanan grid. Select a stable
      // supporting terminal nearest this branch: the proxy is that real pin
      // coordinate plus its frozen rounding offset, and has no free DOF.
      int    pin      = sources->second.front();
      double distance = std::numeric_limits<double>::infinity();
      for (int candidate : sources->second) {
        const double delta = std::min(std::abs(coord(d, candidate, normal) - (normal == 0 ? a.first : a.second)),
                                      std::abs(coord(d, candidate, normal) - (normal == 0 ? b.first : b.second)));
        if (delta < distance || (delta == distance && candidate < pin)) {
          distance = delta;
          pin      = candidate;
        }
      }
      for (auto it = boundaries[normal].upper_bound(lo); it != boundaries[normal].end() && it->first < hi; ++it) {
        for (int edge : it->second) {
          if (edges[edge].low <= q && q < edges[edge].high && seen.emplace(edge, q, net).second) {
            result.push_back({edge, int(net), bits[net].bit, pin, q});
          }
        }
      }
    };
    for (auto [a, b] : route(points)) {
      if (b < a) {
        std::swap(a, b);
      }
      // Same deterministic HV realization used in the preCTS diagnostic.
      const Busorder_point bend{b.first, a.second};
      segment(a, bend);
      segment(bend, b);
    }
  }
  return result;
}

Busorder_sample Busorder::evaluate(const dp::Design& d, const std::string& stage, unsigned iterations,
                                   std::vector<dp::RelativeOrder>* constraints) {
  const auto      start = Clock::now();
  Busorder_sample sample;
  sample.stage                                                                 = stage;
  sample.iterations                                                            = iterations;
  sample.hpwl                                                                  = dp::hpwl(d);
  const auto                                                            points = crossings(d, sample.high_degree);
  std::map<std::pair<int, std::string>, std::vector<Busorder_crossing>> groups;
  for (const auto& p : points) {
    groups[{p.edge, bits[p.net].bus}].push_back(p);
  }
  std::ofstream detail;
  if (!output.empty() && !stage.empty()) {
    std::filesystem::create_directories(output);
    detail.open(output + "/" + stage + ".tsv");
    detail << "edge\tbus\tbit\tnet\tx_dbu\ty_dbu\tunordered\n";
  }
  for (auto& [key, rows] : groups) {
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
      return std::tie(a.coordinate, a.bit, a.net) < std::tie(b.coordinate, b.bit, b.net);
    });
    std::set<int>                    lanes;
    std::set<std::pair<int, double>> occurrences;
    for (const auto& r : rows) {
      lanes.insert(r.bit);
      occurrences.emplace(r.bit, r.coordinate);
    }
    if (lanes.size() < 2) {
      continue;
    }
    ++sample.groups;
    const bool bad    = !ordered(rows);
    sample.unordered += bad;
    if (!cohort_initialized && stage == "before" && bad) {
      initial_unordered.emplace(key, lanes);
    }
    if (initial_unordered.contains(key) && initial_unordered.at(key) == lanes) {
      bool tied = false;
      for (size_t i = 1; i < rows.size(); ++i) {
        tied |= rows[i - 1].coordinate == rows[i].coordinate && rows[i - 1].bit != rows[i].bit;
      }
      if (bad) {
        ++sample.remaining;
      } else if (tied) {
        ++sample.ambiguous;
      } else {
        ++sample.resolved;
      }
    }
    const auto& boundary = edges[key.first];
    const int   axis     = 1 - boundary.axis;
    if (detail.is_open()) {
      for (const auto& r : rows) {
        const double x = axis == 0 ? r.coordinate : boundary.position, y = axis == 0 ? boundary.position : r.coordinate;
        detail << r.edge << '\t' << key.second << '\t' << r.bit << '\t' << r.net << '\t' << x << '\t' << y << '\t' << bad << '\n';
      }
    }
    // Separate branches of one bit do not define a single ordering lane.
    if (occurrences.size() != lanes.size()) {
      ++sample.branching;
      continue;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
      return std::tie(a.bit, a.net, a.pin) < std::tie(b.bit, b.net, b.pin);
    });
    rows.erase(std::unique(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.bit == b.bit; }), rows.end());
    auto [direction, added] = descending.emplace(key, false);
    if (added) {
      double asc = 0, desc = 0;
      for (size_t i = 1; i < rows.size(); ++i) {
        const double delta  = rows[i - 1].coordinate - rows[i].coordinate;
        asc                += std::pow(std::max(0., delta), 2);
        desc               += std::pow(std::max(0., -delta), 2);
      }
      direction->second = desc < asc;
    }
    if (direction->second) {
      std::reverse(rows.begin(), rows.end());
    }
    bool actionable = false;
    for (size_t i = 1; i < rows.size(); ++i) {
      const auto&  a    = rows[i - 1];
      const auto&  b    = rows[i];
      const int    from = d.pin2node[a.pin], to = d.pin2node[b.pin];
      const double violation  = std::max(0., a.coordinate + 1 - b.coordinate);
      const double scale      = std::max(d.layout.row_height, boundary.high - boundary.low);
      sample.penalty         += violation * violation / scale;
      if (from == to || (from >= d.num_movable && to >= d.num_movable)) {
        continue;
      }
      actionable = true;
      if (constraints) {
        const auto&  pos        = axis == 0 ? d.node_x : d.node_y;
        const auto&  size       = axis == 0 ? d.node_size_x : d.node_size_y;
        // FLUTE coordinates are integer DBU. A one-DBU gap avoids a
        // zero-force optimum with different bits collapsed at one coordinate;
        // it is an order margin, not a claim about routing track capacity.
        const double separation = (a.coordinate - pos[from]) - (b.coordinate - pos[to]) - size[from] + 1;
        constraints->push_back({
            from,
            to,
            axis == 0 ? dp::Axis::x : dp::Axis::y,
            separation,
            {false, options.weight / scale},
            "busorder"
        });
      }
      ++sample.constraints;
    }
    if (!actionable) {
      ++sample.immovable;
    }
  }
  if (detail.is_open() && !detail) {
    throw std::runtime_error("cannot write busorder crossings");
  }
  if (stage == "before") {
    cohort_initialized = true;
  }
  sample.initially_unordered  = initial_unordered.size();
  sample.no_longer_comparable = sample.initially_unordered - sample.remaining - sample.ambiguous - sample.resolved;
  sample.seconds              = elapsed(start);
  return sample;
}

std::vector<dp::RelativeOrder> Busorder::constraints(const dp::Design& d) {
  std::vector<dp::RelativeOrder> result;
  evaluate(d, "", 0, &result);
  // One cell can support several crossings; normalize duplicate exposure.
  std::vector<unsigned> degree(d.node_x.size());
  for (const auto& e : result) {
    ++degree[e.from];
    ++degree[e.to];
  }
  for (auto& e : result) {
    e.strength.weight /= std::sqrt(double(std::max(degree[e.from], degree[e.to])));
  }
  return result;
}

void Busorder::refine(dp::Design& physical, const dp::Options& base) {
  density_bins_x      = base.bins_x;
  density_bins_y      = base.bins_y;
  density_target      = base.target_density;
  density_scale_macro = base.scale_macro_density;
  const auto start    = Clock::now();
  auto       before   = evaluate(physical, "before", 0, nullptr);
  before.overflow     = dp::density_overflow(physical, base.bins_x, base.bins_y, base.target_density, base.scale_macro_density);
  history.push_back(before);
  auto                work = physical, best = physical;
  auto                best_sample = before;
  std::vector<double> criticality(work.node_x.size(), 1);
  for (size_t net = 0; net < work.net_weights.size(); ++net) {
    if (work.net_mask.empty() || work.net_mask[net]) {
      for (int k = work.flat_net2pin_start[net]; k < work.flat_net2pin_start[net + 1]; ++k) {
        const int node    = work.pin2node[work.flat_net2pin[k]];
        criticality[node] = std::max(criticality[node], work.net_weights[net]);
      }
    }
  }
  if (work.node_boxes.empty()) {
    work.node_boxes.assign(work.node_x.size(), {work.layout.xl, work.layout.yl, work.layout.xh, work.layout.yh});
  }
  for (int i = 0; i < work.num_movable; ++i) {
    const double reach = options.max_displacement_um * dbu / std::sqrt(criticality[i]);
    auto&        box   = work.node_boxes[i];
    box.xl             = std::max(box.xl, work.node_x[i] - reach);
    box.xh             = std::min(box.xh, work.node_x[i] + reach);
    box.yl             = std::max(box.yl, work.node_y[i] - reach);
    box.yh             = std::min(box.yh, work.node_y[i] + reach);
  }
  for (unsigned step = 0; step < options.iterations;) {
    auto orders = constraints(work);
    if (orders.empty() && options.weight > 0) {
      break;
    }
    work.order_edges = physical.order_edges;
    work.order_edges.insert(work.order_edges.end(), orders.begin(), orders.end());
    auto control                   = base;
    control.max_iterations         = std::min(options.refresh, options.iterations - step);
    control.min_iterations         = control.max_iterations;
    control.post_density_factor    = 0;
    control.schedule_iterations    = control.max_iterations;
    // This settling phase starts from an optimized placement: retain the final
    // wire smoothing rather than replaying the initial broad smoothing ramp.
    control.wirelength_gamma_end   = base.wirelength_gamma_end;
    control.wirelength_gamma_start = base.wirelength_gamma_end;
    control.order_weight           = std::min(1., double(step + control.max_iterations) / std::max(1u, options.iterations / 2));
    control.order_growth           = 1;
    control.max_step               = std::min(base.max_step, .05);
    control.legalize = control.detailed_place  = false;
    control.before_legalize                    = {};
    control.after_macro_legalize               = {};
    control.observer                           = {};
    control.progress_observer                  = {};
    control.checkpoint                         = {};
    control.checkpoint_interval                = 0;
    control.checkpoint_final                   = false;
    control.checkpoint_after_density           = false;
    control.terminal_update                    = {};
    control.terminal_interval                  = 0;
    control.macro_pull_update                  = {};
    control.macro_pull_interval                = 0;
    const auto result                          = dp::place(work, control);
    step                                      += result.iterations;
    auto sample                                = evaluate(work, "iteration_" + std::to_string(step), step, nullptr);
    sample.overflow = dp::density_overflow(work, base.bins_x, base.bins_y, base.target_density, base.scale_macro_density);
    history.push_back(sample);
    // Report actual RSMT order, not whether frozen proxy inequalities improved.
    const bool better
        = sample.resolved > best_sample.resolved
          || (sample.resolved == best_sample.resolved && sample.no_longer_comparable <= best_sample.no_longer_comparable
              && sample.penalty < best_sample.penalty);
    if (sample.hpwl <= before.hpwl * 1.02 && sample.overflow <= before.overflow + .01 && (better || options.weight == 0)) {
      best        = work;
      best_sample = sample;
    }
    if (!result.iterations) {
      break;
    }
  }
  for (int i = 0; i < physical.num_movable; ++i) {
    physical.node_x[i] = best.node_x[i];
    physical.node_y[i] = best.node_y[i];
  }
  auto after     = evaluate(physical, "after_gp", best_sample.iterations, nullptr);
  after.overflow = best_sample.overflow;
  after.seconds  = elapsed(start);
  history.push_back(after);
  write();
}

void Busorder::observe(const dp::Design& d, const std::string& stage) {
  auto sample = evaluate(d, stage, 0, nullptr);
  if (density_bins_x) {
    sample.overflow = dp::density_overflow(d, density_bins_x, density_bins_y, density_target, density_scale_macro);
  }
  history.push_back(sample);
  write();
}

void Busorder::write() const {
  if (output.empty()) {
    return;
  }
  std::filesystem::create_directories(output);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  json.StartObject();
  json.Key("enabled");
  json.Bool(options.enabled);
  json.Key("dbu_per_um");
  json.Double(dbu);
  json.Key("model");
  json.String("RSMT HV crossing proxies tied to supporting real pin coordinates; soft adjacent-bit order");
  json.Key("pruning");
  json.String("fixed initial minimum neighbor distance; alternate H then V");
  json.Key("initial_rectangles");
  json.Uint(initial_rectangles);
  json.Key("removed_horizontal");
  json.Uint(removed_h);
  json.Key("removed_vertical");
  json.Uint(removed_v);
  json.Key("build_seconds");
  json.Double(build_seconds);
  json.Key("weight");
  json.Double(options.weight);
  json.Key("iterations");
  json.Uint(options.iterations);
  json.Key("max_displacement_um");
  json.Double(options.max_displacement_um);
  json.Key("rectangles");
  json.StartArray();
  for (const auto& r : mesh) {
    json.StartObject();
    json.Key("bounds_dbu");
    json.StartArray();
    for (double v : {r.xl, r.yl, r.xh, r.yh}) {
      json.Double(v);
    }
    json.EndArray();
    json.Key("initial_cells");
    json.Uint(r.cells);
    json.EndObject();
  }
  json.EndArray();
  json.Key("history");
  json.StartArray();
  for (const auto& s : history) {
    json.StartObject();
    json.Key("stage");
    json.String(s.stage.c_str());
    json.Key("iterations");
    json.Uint(s.iterations);
    json.Key("groups");
    json.Uint(s.groups);
    json.Key("unordered");
    json.Uint(s.unordered);
    json.Key("constraints");
    json.Uint(s.constraints);
    json.Key("branching_groups");
    json.Uint(s.branching);
    json.Key("immovable_groups");
    json.Uint(s.immovable);
    json.Key("skipped_high_degree_nets");
    json.Uint(s.high_degree);
    json.Key("hpwl");
    json.Double(s.hpwl);
    json.Key("initially_unordered");
    json.Uint(s.initially_unordered);
    json.Key("initial_groups_now_ordered");
    json.Uint(s.resolved);
    json.Key("initial_groups_still_unordered");
    json.Uint(s.remaining);
    json.Key("initial_groups_tied_ambiguous");
    json.Uint(s.ambiguous);
    json.Key("initial_groups_no_longer_comparable");
    json.Uint(s.no_longer_comparable);
    json.Key("initial_groups_now_ordered_pct");
    json.Double(s.initially_unordered ? 100. * s.resolved / s.initially_unordered : 0.);
    json.Key("initial_groups_still_unordered_pct");
    json.Double(s.initially_unordered ? 100. * s.remaining / s.initially_unordered : 0.);
    json.Key("initial_groups_not_confirmed_ordered_pct");
    json.Double(s.initially_unordered ? 100. * (s.initially_unordered - s.resolved) / s.initially_unordered : 0.);
    json.Key("overflow");
    if (s.overflow >= 0) {
      json.Double(s.overflow);
    } else {
      json.Null();
    }
    json.Key("penalty");
    json.Double(s.penalty);
    json.Key("seconds");
    json.Double(s.seconds);
    json.EndObject();
  }
  json.EndArray();
  json.EndObject();
  std::ofstream file(output + "/report.json");
  file << buffer.GetString() << '\n';
  if (!file) {
    throw std::runtime_error("cannot write busorder report");
  }
}
}  // namespace livehd::placement
