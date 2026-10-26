// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "busorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

#include "cell_criticality.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
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
  if (!o.iterations || o.iterations > 2000 || !o.refresh || o.refresh > o.iterations || !std::isfinite(o.weight) || o.weight < 0
      || o.weight > 100 || !std::isfinite(o.max_displacement_um) || o.max_displacement_um <= 0) {
    throw std::invalid_argument("invalid busorder iterations/refresh/weight/displacement");
  }
}

Busorder::Busorder(const dp::Design& d, const Region_mesh& mesh, const std::vector<Bus_bit>& identities, Busorder_route router,
                   double units, Busorder_options policy, std::string directory)
    : options(policy), dbu(units), bits(identities), route(std::move(router)), regions(mesh), output(std::move(directory)) {
  validate_busorder_options(options);
  d.validate();
  if (!(std::isfinite(dbu) && dbu > 0) || bits.size() + 1 != d.flat_net2pin_start.size() || !route) {
    throw std::invalid_argument("invalid busorder design/units/router");
  }
}

std::vector<Busorder_crossing> Busorder::crossings(const dp::Design& d, unsigned& high_degree) const {
  const auto&                        edges = regions.edges;
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
    const auto& boundary = regions.edges[key.first];
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
  auto work = physical, best = physical;
  auto best_sample = before;
  limit_displacement(work, node_weight_criticality(work), options.max_displacement_um * dbu);
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
  json.Uint(regions.initial_rectangles);
  json.Key("removed_horizontal");
  json.Uint(regions.removed_h);
  json.Key("removed_vertical");
  json.Uint(regions.removed_v);
  json.Key("build_seconds");
  json.Double(regions.build_seconds);
  json.Key("weight");
  json.Double(options.weight);
  json.Key("iterations");
  json.Uint(options.iterations);
  json.Key("max_displacement_um");
  json.Double(options.max_displacement_um);
  json.Key("rectangles");
  json.StartArray();
  for (const auto& r : regions.rects) {
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
