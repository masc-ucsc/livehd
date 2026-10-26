// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "region_mesh.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
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
}  // namespace

void validate_region_options(const Region_options& o) {
  if (!o.max_cells || o.max_cells > 1000000 || !std::isfinite(o.merge_um) || o.merge_um < 0) {
    throw std::invalid_argument("invalid region cell cap/merge distance");
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

std::vector<Region_edge> region_edges(const std::vector<Region_rect>& mesh) {
  std::vector<Region_edge> edges;
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

int region_of(const std::vector<Region_rect>& mesh, double x, double y) {
  for (size_t i = 0; i < mesh.size(); ++i) {
    const auto& r = mesh[i];
    if (r.xl <= x && x < r.xh && r.yl <= y && y < r.yh) {
      return int(i);
    }
  }
  return -1;
}

std::optional<int> Region_mesh::rect_of(double x, double y) const {
  const int i = region_of(rects, x, y);
  return i < 0 ? std::nullopt : std::optional<int>(i);
}

Region_mesh Region_mesh::build(const dp::Design& d, const std::vector<int>& macros, const std::vector<Region_io>& ios,
                               const Region_options& options, double dbu) {
  using Clock      = std::chrono::steady_clock;
  const auto start = Clock::now();
  validate_region_options(options);
  d.validate();
  if (!(std::isfinite(dbu) && dbu > 0)) {
    throw std::invalid_argument("invalid region mesh units");
  }
  for (int i = 0; i < d.num_movable; ++i) {
    if (!d.node_macro.empty() && d.node_macro[i]) {
      throw std::invalid_argument("region mesh requires fixed macros");
    }
  }
  Region_mesh         result;
  auto&               mesh    = result.rects;
  auto&               members = result.members;
  result.options              = options;
  const auto&         l       = d.layout;
  std::vector<double> raw[2];
  for (int i : macros) {
    if (i < d.num_movable || i >= int(d.node_x.size())) {
      throw std::invalid_argument("invalid region mesh fixed macro index");
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
  std::vector<std::vector<int>> initial(nx * ny);
  for (int i = 0; i < d.num_movable; ++i) {
    const double x = d.node_x[i] + d.node_size_x[i] / 2, y = d.node_y[i] + d.node_size_y[i] / 2;
    const size_t ix = std::clamp<std::ptrdiff_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin() - 1, 0, nx - 1);
    const size_t iy = std::clamp<std::ptrdiff_t>(std::upper_bound(ys.begin(), ys.end(), y) - ys.begin() - 1, 0, ny - 1);
    initial[iy * nx + ix].push_back(i);
  }
  std::set<std::pair<int, double>> candidates;
  for (size_t i = 1; i + 1 < xs.size(); ++i) {
    candidates.emplace(0, xs[i]);
  }
  for (size_t i = 1; i + 1 < ys.size(); ++i) {
    candidates.emplace(1, ys[i]);
  }
  const auto refine = [&](auto&& self, Region_rect box, std::vector<int> cells, unsigned depth) -> void {
    if (cells.size() <= options.max_cells) {
      box.cells = cells.size();
      mesh.push_back(box);
      members.push_back(std::move(cells));
      return;
    }
    if (depth >= 48) {
      throw std::runtime_error("region mesh cannot split coincident cell centers below the cell cap");
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
    self(self, a, std::move(low), depth + 1);
    self(self, b, std::move(high), depth + 1);
  };
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      refine(refine, {xs[x], ys[y], xs[x + 1], ys[y + 1], 0}, std::move(initial[y * nx + x]), 0);
    }
  }
  result.initial_rectangles = mesh.size();
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
      std::vector<bool>             consumed(mesh.size());
      std::vector<Region_rect>      next;
      std::vector<std::vector<int>> next_members;
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
        next_members.push_back(std::move(members[i]));
        next_members.back().insert(next_members.back().end(), members[j].begin(), members[j].end());
      }
      for (size_t i = 0; i < mesh.size(); ++i) {
        if (!consumed[i]) {
          next.push_back(mesh[i]);
          next_members.push_back(std::move(members[i]));
        }
      }
      mesh    = std::move(next);
      members = std::move(next_members);
      ++(axis == 0 ? result.removed_v : result.removed_h);
    }
  }
  uint64_t total = 0;
  for (size_t i = 0; i < mesh.size(); ++i) {
    if (mesh[i].cells > options.max_cells || mesh[i].cells != members[i].size()) {
      throw std::runtime_error("region mesh cell cap violated");
    }
    std::sort(members[i].begin(), members[i].end());
    total += mesh[i].cells;
  }
  if (total != unsigned(d.num_movable)) {
    throw std::runtime_error("region mesh cell count mismatch");
  }
  result.edges         = region_edges(mesh);
  result.build_seconds = std::chrono::duration<double>(Clock::now() - start).count();
  return result;
}

void Region_mesh::write(const std::string& dir) const {
  if (dir.empty()) {
    return;
  }
  std::filesystem::create_directories(dir);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  json.StartObject();
  json.Key("max_cells");
  json.Uint(options.max_cells);
  json.Key("merge_um");
  json.Double(options.merge_um);
  json.Key("initial_rectangles");
  json.Uint(initial_rectangles);
  json.Key("removed_horizontal");
  json.Uint(removed_h);
  json.Key("removed_vertical");
  json.Uint(removed_v);
  json.Key("build_seconds");
  json.Double(build_seconds);
  json.Key("edges");
  json.Uint64(edges.size());
  json.Key("rectangles");
  json.StartArray();
  for (const auto& r : rects) {
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
  json.EndObject();
  std::ofstream file(dir + "/region_mesh.json");
  file << buffer.GetString() << '\n';
  if (!file) {
    throw std::runtime_error("cannot write region mesh report");
  }
}
}  // namespace livehd::placement
