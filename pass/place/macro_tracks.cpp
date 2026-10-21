// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_tracks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <tuple>

namespace livehd::placement {
namespace {
namespace ph = physical;
struct Access {
  bool    x;
  double  low, high, width;
  int64_t start, pitch, count;
};
struct Pin {
  std::string         name;
  std::vector<Access> access;
  bool                mixed = false;
};
struct Profile {
  struct Phase {
    int64_t     value;
    unsigned    misses;
    std::string coverage;
  };
  std::vector<Pin>   pins;
  int64_t            period[2] = {1, 1};
  std::vector<Phase> phases[2];
};
// Line center must be inside the pin. Full-width landing is preferred when the
// abstract permits it; narrower abstracts still admit an interior connection.
std::pair<bool, bool> accessible(const Access& a, double origin, bool finite) {
  const auto center = origin + (a.low + a.high) / 2;
  auto       k      = std::llround((center - a.start) / a.pitch);
  if (finite) {
    k = std::clamp<int64_t>(k, 0, a.count - 1);
  }
  const auto line      = double(a.start) + k * a.pitch;
  const auto clearance = std::min(line - origin - a.low, origin + a.high - line);
  return {clearance > 1e-7, clearance + 1e-7 >= a.width / 2};
}
Profile profile(const ph::Technology& tech, const ph::Floorplan& fp, const ph::Component& c) {
  Profile    p;
  const auto grid = std::max<int64_t>(1, std::llround(tech.manufacturing_grid * tech.dbu));
  if (grid > 65536 || (tech.manufacturing_grid > 0 && std::abs(grid - tech.manufacturing_grid * tech.dbu) > 1e-7)) {
    throw std::runtime_error("LEF manufacturing grid is not representable in bounded placement DBU");
  }
  p.period[0] = p.period[1] = grid;
  const auto& m             = tech.macros.at(c.macro);
  for (const auto& [name, pin] : m.pins) {
    if (pin.use == "POWER" || pin.use == "GROUND") {
      continue;
    }
    Pin candidate{name, {}};
    for (const auto& shape : pin.shapes) {
      const auto found = tech.layers.find(shape.layer);
      if (found == tech.layers.end()) {
        continue;
      }
      const auto& layer = found->second;
      if (layer.direction != "VERTICAL" && layer.direction != "HORIZONTAL") {
        continue;
      }
      const bool x = layer.direction == "VERTICAL";
      const auto a
          = ph::orient_point(shape.rect.xl + m.origin_x, shape.rect.yl + m.origin_y, m.width, m.height, c.place.orientation);
      const auto b
          = ph::orient_point(shape.rect.xh + m.origin_x, shape.rect.yh + m.origin_y, m.width, m.height, c.place.orientation);
      const double low  = std::min(x ? a.first : a.second, x ? b.first : b.second) * tech.dbu;
      const double high = std::max(x ? a.first : a.second, x ? b.first : b.second) * tech.dbu;
      if (high <= low) {
        continue;
      }
      bool       imported = false;
      const auto add      = [&](int64_t start, int64_t pitch, int64_t count) {
        if (pitch <= 0 || count <= 0) {
          return;
        }
        candidate.access.push_back({x, low, high, layer.width * tech.dbu, start, pitch, count});
        auto&      period = p.period[x ? 0 : 1];
        const auto factor = pitch / std::gcd(period, pitch);
        if (factor > 65536 / period) {
          throw std::runtime_error("macro routing-track period exceeds bounded phase search: " + c.macro);
        }
        period *= factor;
      };
      for (const auto& track : fp.tracks) {
        if (track.layer == shape.layer && track.axis == (x ? "X" : "Y")) {
          imported = true;
          add(track.start, track.step, track.count);
        }
      }
      if (!imported && layer.has_offset) {
        add(std::llround((x ? layer.offset_x : layer.offset_y) * tech.dbu),
            std::llround((x ? layer.pitch_x : layer.pitch_y) * tech.dbu),
            std::numeric_limits<int32_t>::max());
      }
    }
    if (!candidate.access.empty()) {
      candidate.mixed = std::any_of(candidate.access.begin(), candidate.access.end(), [&](const auto& a) {
        return a.x != candidate.access.front().x;
      });
      p.pins.push_back(std::move(candidate));
    }
  }
  for (int axis = 0; axis < 2; ++axis) {
    for (int64_t phase = 0; phase < p.period[axis]; phase += grid) {
      bool        valid  = true;
      unsigned    misses = 0;
      std::string coverage;
      for (const auto& pin : p.pins) {
        bool interior = false, landing = false;
        for (const auto& a : pin.access) {
          if (a.x != (axis == 0)) {
            continue;
          }
          const auto [inside, full]  = accessible(a, phase, false);
          interior                  |= inside;
          landing                   |= full;
        }
        if (pin.mixed) {
          coverage.push_back(static_cast<char>(interior + 2 * landing));
        } else if (pin.access.front().x == (axis == 0)) {
          valid  &= interior;
          misses += !landing;
        }
      }
      if (valid) {
        p.phases[axis].push_back({phase, misses, std::move(coverage)});
      }
    }
    if (p.phases[axis].empty()) {
      throw std::runtime_error("conflicting macro pin routing-track phases: " + c.macro);
    }
  }
  return p;
}
std::optional<std::string> inaccessible(const Profile& p, int64_t x, int64_t y) {
  for (const auto& pin : p.pins) {
    if (std::none_of(pin.access.begin(), pin.access.end(), [&](const auto& a) { return accessible(a, a.x ? x : y, true).first; })) {
      return pin.name;
    }
  }
  return {};
}
struct Box {
  int64_t xl, yl, xh, yh;
};
Box box(const ph::Technology& tech, const ph::Component& c, int64_t hx, int64_t hy) {
  if (tech.macros.at(c.macro).klass.starts_with("CORE")) {
    hx = hy = 0;
  }
  return {c.place.x - hx, c.place.y - hy, c.place.x + c.place.width + hx, c.place.y + c.place.height + hy};
}
bool intersects(const Box& a, const Box& b) { return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh; }
struct Candidate {
  int64_t     value;
  unsigned    misses;
  std::string coverage;
};
std::vector<Candidate> candidates(const Profile& p, int axis, int64_t old, int64_t low, int64_t high,
                                  const std::vector<int64_t>* edges = nullptr) {
  std::vector<Candidate> result;
  for (const auto& phase : p.phases[axis]) {
    const auto add = [&](int64_t value) {
      if (value >= low && value <= high) {
        result.push_back({value, phase.misses, phase.coverage});
      }
    };
    if (edges) {
      for (int64_t edge : *edges) {
        const auto center = static_cast<int64_t>(std::floor(double(edge - phase.value) / p.period[axis]));
        add(phase.value + center * p.period[axis]);
        add(phase.value + (center + 1) * p.period[axis]);
      }
    } else {
      const auto center = std::llround(double(old - phase.value) / p.period[axis]);
      for (int delta = -4; delta <= 4; ++delta) {
        add(phase.value + (center + delta) * p.period[axis]);
      }
    }
  }
  std::sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
    return std::tuple{a.misses, std::abs(a.value - old), a.value} < std::tuple{b.misses, std::abs(b.value - old), b.value};
  });
  std::map<std::string, Candidate> representatives;
  for (const auto& candidate : result) {
    representatives.try_emplace(candidate.coverage, candidate);
  }
  if (representatives.size() > 64) {
    throw std::runtime_error("macro mixed-layer phase coverage exceeds bounded search");
  }
  if (result.size() > 64) {
    result.resize(64);
  }
  for (const auto& [coverage, candidate] : representatives) {
    (void)coverage;
    if (std::none_of(result.begin(), result.end(), [&](const auto& kept) { return kept.value == candidate.value; })) {
      result.push_back(candidate);
    }
  }
  return result;
}
// Preserve a legal macro packing while its individual origins move onto routing
// phases. Every pair retains one original separating axis. Forward/backward
// difference bounds make compact strips move together instead of blocking one
// another; the original frame is changed only after the complete proof passes.
bool coherent_macro_tracks(const ph::Technology& tech, const ph::Floorplan& layout, std::vector<ph::Component>& cells, int64_t hx,
                           int64_t hy) {
  if (cells.empty() || cells.size() > 4096 || layout.rows.empty()) {
    return false;
  }
  const auto fixed = [&](size_t i) {
    const auto& c = cells[i];
    const auto& m = tech.macros.at(c.macro);
    return c.place.status == ph::Status::FIXED || c.place.status == ph::Status::LOCKED || m.klass.starts_with("CORE")
           || m.virtual_proxy;
  };
  Box core{std::numeric_limits<int64_t>::max(),
           std::numeric_limits<int64_t>::max(),
           std::numeric_limits<int64_t>::min(),
           std::numeric_limits<int64_t>::min()};
  for (const auto& row : layout.rows) {
    core.xl = std::min(core.xl, row.x);
    core.yl = std::min(core.yl, row.y);
    core.xh = std::max(core.xh, row.x + row.count * row.step);
    core.yh = std::max<int64_t>(core.yh, row.y + std::llround(tech.sites.at(row.site).height * tech.dbu));
  }
  std::map<std::pair<std::string, ph::Orientation>, Profile> cache;
  std::vector<const Profile*>                                profiles(cells.size(), nullptr);
  for (size_t i = 0; i < cells.size(); ++i) {
    if (!fixed(i)) {
      const auto key = std::pair{cells[i].macro, cells[i].place.orientation};
      if (!cache.contains(key)) {
        cache.emplace(key, profile(tech, layout, cells[i]));
      }
      profiles[i] = &cache.at(key);
      if (std::any_of(profiles[i]->pins.begin(), profiles[i]->pins.end(), [](const auto& p) { return p.mixed; })) {
        return false;
      }
    }
  }
  struct Edge {
    size_t  from, to;
    int64_t distance;
  };
  std::vector<Edge> edges[2];
  std::vector<Box>  boxes;
  for (const auto& c : cells) {
    boxes.push_back(box(tech, c, hx, hy));
  }
  for (size_t i = 0; i < cells.size(); ++i) {
    for (size_t j = i + 1; j < cells.size(); ++j) {
      if (fixed(i) && fixed(j)) {
        continue;
      }
      const auto& a    = boxes[i];
      const auto& b    = boxes[j];
      const bool  left = a.xh <= b.xl, right = b.xh <= a.xl, bottom = a.yh <= b.yl, top = b.yh <= a.yl;
      if (!left && !right && !bottom && !top) {
        return false;
      }
      const int64_t xgap = left ? b.xl - a.xh : right ? a.xl - b.xh : -1;
      const int64_t ygap = bottom ? b.yl - a.yh : top ? a.yl - b.yh : -1;
      const int     axis = xgap >= ygap ? 0 : 1;
      const size_t  from = (axis == 0 ? left : bottom) ? i : j, to = from == i ? j : i;
      const auto&   f        = cells[from].place;
      const auto&   t        = cells[to].place;
      const auto&   fb       = boxes[from];
      const auto&   tb       = boxes[to];
      const int64_t distance = axis == 0 ? fb.xh - f.x + t.x - tb.xl : fb.yh - f.y + t.y - tb.yl;
      edges[axis].push_back({from, to, distance});
    }
  }
  const auto original = cells;
  for (int axis = 0; axis < 2; ++axis) {
    std::vector<size_t> order(cells.size());
    std::iota(order.begin(), order.end(), 0);
    const auto old = [&](size_t i) { return axis == 0 ? original[i].place.x : original[i].place.y; };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return std::pair{old(a), a} < std::pair{old(b), b}; });
    std::vector<std::vector<Edge>> forward(cells.size()), backward(cells.size());
    for (const auto& edge : edges[axis]) {
      forward[edge.from].push_back(edge);
      backward[edge.to].push_back(edge);
    }
    std::vector<int64_t> low(cells.size()), high(cells.size()), position(cells.size());
    const auto           snap = [&](size_t i, int64_t lo, int64_t hi, int64_t target) -> std::optional<int64_t> {
      if (lo > hi) {
        return {};
      }
      if (fixed(i)) {
        return old(i) >= lo && old(i) <= hi ? std::optional<int64_t>{old(i)} : std::nullopt;
      }
      const auto& p           = *profiles[i];
      unsigned    best_misses = std::numeric_limits<unsigned>::max();
      for (const auto& phase : p.phases[axis]) {
        best_misses = std::min(best_misses, phase.misses);
      }
      std::optional<int64_t> best;
      for (const auto& phase : p.phases[axis]) {
        if (phase.misses != best_misses) {
          continue;
        }
        const auto first = static_cast<int64_t>(std::ceil(double(lo - phase.value) / p.period[axis]));
        const auto last  = static_cast<int64_t>(std::floor(double(hi - phase.value) / p.period[axis]));
        if (first > last) {
          continue;
        }
        const auto k     = std::clamp<int64_t>(std::llround(double(target - phase.value) / p.period[axis]), first, last);
        const auto value = phase.value + k * p.period[axis];
        if (!best || std::pair{std::abs(value - target), value} < std::pair{std::abs(*best - target), *best}) {
          best = value;
        }
      }
      return best;
    };
    for (size_t i : order) {
      const auto& c = original[i];
      low[i]        = fixed(i) ? old(i) : (axis == 0 ? core.xl + hx : core.yl + hy);
      high[i]       = fixed(i) ? old(i) : (axis == 0 ? core.xh - c.place.width - hx : core.yh - c.place.height - hy);
    }
    for (size_t i : order) {
      for (const auto& edge : backward[i]) {
        low[i] = std::max(low[i], low[edge.from] + edge.distance);
      }
      const auto value = snap(i, low[i], high[i], low[i]);
      if (!value) {
        return false;
      }
      low[i] = *value;
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const auto i = *it;
      for (const auto& edge : forward[i]) {
        high[i] = std::min(high[i], high[edge.to] - edge.distance);
      }
      const auto value = snap(i, low[i], high[i], high[i]);
      if (!value) {
        return false;
      }
      high[i] = *value;
    }
    for (size_t i : order) {
      int64_t bound = low[i];
      for (const auto& edge : backward[i]) {
        bound = std::max(bound, position[edge.from] + edge.distance);
      }
      const auto value = snap(i, bound, high[i], old(i));
      if (!value) {
        return false;
      }
      position[i] = *value;
    }
    for (size_t i : order) {
      (axis == 0 ? cells[i].place.x : cells[i].place.y) = position[i];
    }
  }
  bool valid = true;
  for (size_t i = 0; i < cells.size(); ++i) {
    if (!fixed(i)) {
      const auto b  = box(tech, cells[i], hx, hy);
      valid        &= b.xl >= core.xl && b.yl >= core.yl && b.xh <= core.xh && b.yh <= core.yh
                      && !inaccessible(*profiles[i], cells[i].place.x, cells[i].place.y);
    }
    for (size_t j = 0; j < i; ++j) {
      if (!fixed(i) || !fixed(j)) {
        valid &= !intersects(box(tech, cells[i], hx, hy), box(tech, cells[j], hx, hy));
      }
    }
  }
  if (!valid) {
    cells = original;
  }
  return valid;
}

}  // namespace
void legalize_macro_tracks_impl(const ph::Technology& tech, const ph::Floorplan& layout, std::vector<ph::Component>& cells,
                                int64_t halo_x, int64_t halo_y, bool move_macros, bool repack) {
  if (layout.rows.empty()) {
    return;
  }
  Box core{std::numeric_limits<int64_t>::max(),
           std::numeric_limits<int64_t>::max(),
           std::numeric_limits<int64_t>::min(),
           std::numeric_limits<int64_t>::min()};
  for (const auto& row : layout.rows) {
    const auto height = std::llround(tech.sites.at(row.site).height * tech.dbu);
    core.xl           = std::min(core.xl, row.x);
    core.yl           = std::min(core.yl, row.y);
    core.xh           = std::max(core.xh, row.x + row.count * row.step);
    core.yh           = std::max<int64_t>(core.yh, row.y + height);
  }
  const auto fixed = [](const auto& c) { return c.place.status == ph::Status::FIXED || c.place.status == ph::Status::LOCKED; };
  std::vector<size_t> blockers;
  for (size_t i = 0; i < cells.size(); ++i) {
    if (fixed(cells[i]) || !tech.macros.at(cells[i].macro).klass.starts_with("CORE")) {
      blockers.push_back(i);
    }
  }
  const auto grid = std::max<int64_t>(1, std::llround(tech.manufacturing_grid * tech.dbu));
  std::map<std::pair<std::string, ph::Orientation>, Profile> profiles;
  for (size_t i = 0; i < cells.size(); ++i) {
    auto& c = cells[i];
    if (tech.macros.at(c.macro).klass.starts_with("CORE") || tech.macros.at(c.macro).virtual_proxy || (!move_macros && !fixed(c))) {
      continue;
    }
    const auto key = std::pair{c.macro, c.place.orientation};
    if (!profiles.contains(key)) {
      profiles.emplace(key, profile(tech, layout, c));
    }
    const auto& p = profiles.at(key);
    if (p.pins.empty() && grid == 1) {
      continue;
    }
    if (fixed(c)) {
      if (c.place.x % grid || c.place.y % grid) {
        throw std::runtime_error("fixed macro origin misses LEF manufacturing grid: " + c.name);
      }
      if (const auto pin = inaccessible(p, c.place.x, c.place.y)) {
        throw std::runtime_error("fixed macro pin misses routing tracks: " + c.name + ":" + *pin);
      }
      continue;
    }
    const auto           original = c.place;
    bool                 placed   = false;
    // Native macro legalization can leave an infeasible local cluster. Retry
    // at exact blocker/core edges on the same routing/manufacturing lattice;
    // the bounded nearest search remains the common path.
    std::vector<int64_t> x_edges{core.xl + halo_x, core.xh - c.place.width - halo_x};
    std::vector<int64_t> y_edges{core.yl + halo_y, core.yh - c.place.height - halo_y};
    for (size_t j : blockers) {
      if (j == i || (repack && j > i && !fixed(cells[j]))) {
        continue;
      }
      const auto obstacle = box(tech, cells[j], halo_x, halo_y);
      x_edges.push_back(obstacle.xl - c.place.width - halo_x);
      x_edges.push_back(obstacle.xh + halo_x);
      y_edges.push_back(obstacle.yl - c.place.height - halo_y);
      y_edges.push_back(obstacle.yh + halo_y);
    }
    for (int pass = 0; pass < 2 && !placed; ++pass) {
      const auto xs = candidates(p, 0, original.x, core.xl + halo_x, core.xh - c.place.width - halo_x, pass ? &x_edges : nullptr);
      const auto ys = candidates(p, 1, original.y, core.yl + halo_y, core.yh - c.place.height - halo_y, pass ? &y_edges : nullptr);
      struct Position {
        int64_t  x, y;
        unsigned misses;
      };
      std::vector<Position> positions;
      for (const auto& x : xs) {
        for (const auto& y : ys) {
          unsigned misses = x.misses + y.misses;
          for (const auto& pin : p.pins) {
            if (pin.mixed) {
              misses += std::none_of(pin.access.begin(), pin.access.end(), [&](const auto& a) {
                return accessible(a, a.x ? x.value : y.value, true).second;
              });
            }
          }
          positions.push_back({x.value, y.value, misses});
        }
      }
      std::sort(positions.begin(), positions.end(), [&](const auto& a, const auto& b) {
        return std::tuple{a.misses, std::abs(a.x - original.x) + std::abs(a.y - original.y), a.x, a.y}
               < std::tuple{b.misses, std::abs(b.x - original.x) + std::abs(b.y - original.y), b.x, b.y};
      });
      for (const auto& candidate : positions) {
        if (inaccessible(p, candidate.x, candidate.y)) {
          continue;
        }
        c.place.x          = candidate.x;
        c.place.y          = candidate.y;
        const auto area    = box(tech, c, halo_x, halo_y);
        bool       blocked = false;
        for (size_t j : blockers) {
          if (j == i || (repack && j > i && !fixed(cells[j]))) {
            continue;
          }
          if (intersects(area, box(tech, cells[j], halo_x, halo_y))) {
            blocked = true;
            break;
          }
        }
        if (!blocked) {
          placed = true;
          break;
        }
      }
    }
    if (!placed) {
      c.place = original;
      throw std::runtime_error("no legal macro routing-track alignment near " + c.name);
    }
  }
}
void legalize_macro_tracks(const ph::Technology& tech, const ph::Floorplan& layout, std::vector<ph::Component>& cells,
                           int64_t halo_x, int64_t halo_y, bool move_macros) {
  const auto original = cells;
  try {
    // Validate every immutable macro before an early movable failure can enter
    // a recovery path. Recovery treats fixed geometry solely as obstacles.
    if (move_macros) {
      legalize_macro_tracks_impl(tech, layout, cells, halo_x, halo_y, false, false);
    }
    legalize_macro_tracks_impl(tech, layout, cells, halo_x, halo_y, move_macros, false);
  } catch (const std::runtime_error& failure) {
    cells = original;
    if (!move_macros || !std::string(failure.what()).starts_with("no legal macro routing-track alignment near ")) {
      throw;
    }
    if (coherent_macro_tracks(tech, layout, cells, halo_x, halo_y)) {
      return;
    }
    cells = original;
    // An overlapping native cluster cannot be repaired while every unprocessed
    // movable macro remains an obstacle. Repack transactionally against fixed
    // geometry and completed macros; preserve the original frame on failure.
    try {
      legalize_macro_tracks_impl(tech, layout, cells, halo_x, halo_y, move_macros, true);
    } catch (...) {
      cells = original;
      throw;
    }
  }
}

}  // namespace livehd::placement
