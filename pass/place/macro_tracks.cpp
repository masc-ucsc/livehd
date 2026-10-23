// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_tracks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
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
    // Without a phase serving every single-axis pin, keep the one serving the
    // most and stop requiring the rest: such macros placed before alignment
    // existed, and alignment must not turn them into placement failures.
    int64_t  best_phase = -1;
    unsigned best_count = 0;
    for (int64_t phase = 0; phase < p.period[axis]; phase += grid) {
      bool        valid  = true;
      unsigned    misses = 0, served = 0;
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
          served += interior;
        }
      }
      if (valid) {
        p.phases[axis].push_back({phase, misses, std::move(coverage)});
      } else if (best_phase < 0 || served > best_count) {
        best_phase = phase;
        best_count = served;
      }
    }
    if (p.phases[axis].empty()) {
      std::erase_if(p.pins, [&](const Pin& pin) {
        const auto served = [&](const auto& a) { return accessible(a, best_phase, false).first; };
        return !pin.mixed && pin.access.front().x == (axis == 0) && std::none_of(pin.access.begin(), pin.access.end(), served);
      });
      --axis;  // re-derive this axis' phases (and coverage) from the kept pins
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
std::function<bool(const std::vector<ph::Component>&)> make_rigid_macro_track_checker(const ph::Technology& tech,
                                                                                      const ph::Floorplan&  layout) {
  using Key = std::tuple<std::string, ph::Orientation, int64_t, int64_t>;
  return [&tech,
          &layout,
          profiles = std::map<std::pair<std::string, ph::Orientation>, Profile>{},
          results  = std::map<std::vector<Key>, bool>{}](const std::vector<ph::Component>& cells) mutable {
    if (cells.empty()) {
      throw std::invalid_argument("empty rigid routing-track proposal");
    }
    int64_t xl = cells.front().place.x, yl = cells.front().place.y;
    for (const auto& c : cells) {
      xl = std::min(xl, c.place.x);
      yl = std::min(yl, c.place.y);
    }
    std::vector<Key> key;
    for (const auto& c : cells) {
      key.emplace_back(c.macro, c.place.orientation, c.place.x - xl, c.place.y - yl);
    }
    std::sort(key.begin(), key.end());
    if (const auto found = results.find(key); found != results.end()) {
      return found->second;
    }
    const auto grid      = std::max<int64_t>(1, std::llround(tech.manufacturing_grid * tech.dbu));
    int64_t    period[2] = {grid, grid};
    struct Member {
      const Profile* p;
      int64_t        offset[2];
    };
    std::vector<Member> members;
    for (const auto& [name, orientation, x, y] : key) {
      if (x % grid || y % grid) {
        return results.emplace(key, false).first->second;
      }
      const auto id = std::pair{name, orientation};
      if (!profiles.contains(id)) {
        auto c              = cells.front();
        c.macro             = name;
        c.place.orientation = orientation;
        profiles.emplace(id, profile(tech, layout, c));
      }
      const auto& p = profiles.at(id);
      members.push_back({
          &p,
          {x, y}
      });
      for (int axis = 0; axis < 2; ++axis) {
        const auto factor = p.period[axis] / std::gcd(period[axis], p.period[axis]);
        if (factor > 65536 / period[axis]) {
          throw std::runtime_error("rigid macro routing-track period exceeds bounded phase search");
        }
        period[axis] *= factor;
      }
    }
    // Intersect single-axis phase domains before combining mixed-layer pin
    // coverage. Equivalent coverage needs only one representative per axis.
    std::set<std::string> coverage[2];
    for (int axis = 0; axis < 2; ++axis) {
      for (int64_t phase = 0; phase < period[axis]; phase += grid) {
        std::string served;
        bool        valid = true;
        for (const auto& m : members) {
          const auto  value  = (phase + m.offset[axis] % m.p->period[axis]) % m.p->period[axis];
          const auto& phases = m.p->phases[axis];
          const auto  found
              = std::lower_bound(phases.begin(), phases.end(), value, [](const auto& entry, int64_t v) { return entry.value < v; });
          if (found == phases.end() || found->value != value) {
            valid = false;
            break;
          }
          for (char pin : found->coverage) {
            served.push_back(pin & 1);
          }
        }
        if (valid) {
          coverage[axis].insert(std::move(served));
        }
      }
      if (coverage[axis].empty()) {
        return results.emplace(key, false).first->second;
      }
    }
    for (const auto& x : coverage[0]) {
      for (const auto& y : coverage[1]) {
        bool valid = true;
        for (size_t i = 0; i < x.size(); ++i) {
          valid &= (x[i] | y[i]) != 0;
        }
        if (valid) {
          return results.emplace(key, true).first->second;
        }
      }
    }
    return results.emplace(key, false).first->second;
  };
}

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
  const auto                                                 profile_of = [&](const auto& c) -> const Profile& {
    const auto key = std::pair{c.macro, c.place.orientation};
    if (!profiles.contains(key)) {
      profiles.emplace(key, profile(tech, layout, c));
    }
    return profiles.at(key);
  };
  const auto checked = [&](const auto& c) {
    const auto& m = tech.macros.at(c.macro);
    if (m.klass.starts_with("CORE") || m.virtual_proxy || (!move_macros && !fixed(c))) {
      return false;
    }
    return !(profile_of(c).pins.empty() && grid == 1);
  };
  // Repack ignores later movable blockers only because they are placed after
  // this macro; proxies and pinless macros never are, so they stay obstacles.
  std::vector<bool> repacked(cells.size(), false);
  for (size_t i = 0; i < cells.size(); ++i) {
    repacked[i] = repack && !fixed(cells[i]) && checked(cells[i]);
  }
  for (size_t i = 0; i < cells.size(); ++i) {
    auto& c = cells[i];
    if (!checked(c)) {
      continue;
    }
    const auto& p = profile_of(c);
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
      if (j == i || (j > i && repacked[j])) {
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
          if (j == i || (j > i && repacked[j])) {
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

void legalize_rigid_macro_tracks(const ph::Technology& tech, const ph::Floorplan& layout, std::vector<ph::Component>& cells,
                                 const std::vector<uint64_t>& ids, int64_t hx, int64_t hy, bool move) {
  if (ids.size() != cells.size()) {
    throw std::invalid_argument("rigid track group id size mismatch");
  }
  ph::Technology                   proxy_tech = tech;
  std::vector<ph::Component>       proxies;
  std::vector<std::vector<size_t>> members;
  std::map<uint64_t, size_t>       groups;
  for (size_t i = 0; i < cells.size(); ++i) {
    if (!ids[i]) {
      proxies.push_back(cells[i]);
      members.push_back({i});
    } else if (const auto found = groups.find(ids[i]); found != groups.end()) {
      members[found->second].push_back(i);
    } else {
      groups.emplace(ids[i], proxies.size());
      proxies.push_back(cells[i]);
      members.push_back({i});
    }
  }
  const auto fixed = [](const auto& c) { return c.place.status == ph::Status::FIXED || c.place.status == ph::Status::LOCKED; };
  std::map<std::pair<std::string, ph::Orientation>, Profile> member_profiles;
  for (size_t j = 0; j < proxies.size(); ++j) {
    if (!ids[members[j].front()]) {
      continue;
    }
    auto&   proxy = proxies[j];
    int64_t xl = proxy.place.x, yl = proxy.place.y;
    int64_t xh = xl + proxy.place.width, yh = yl + proxy.place.height;
    for (const auto i : members[j]) {
      const auto& c = cells[i];
      if (tech.macros.at(c.macro).klass.starts_with("CORE") || fixed(c) != fixed(proxy)) {
        throw std::invalid_argument("rigid track group contains incompatible placement objects");
      }
      xl = std::min(xl, c.place.x);
      yl = std::min(yl, c.place.y);
      xh = std::max(xh, c.place.x + c.place.width);
      yh = std::max(yh, c.place.y + c.place.height);
    }
    ph::Macro master;
    master.klass  = "BLOCK";
    master.width  = double(xh - xl) / tech.dbu;
    master.height = double(yh - yl) / tech.dbu;
    for (const auto i : members[j]) {
      const auto& c   = cells[i];
      const auto& m   = tech.macros.at(c.macro);
      const auto  key = std::pair{c.macro, c.place.orientation};
      if (!member_profiles.contains(key)) {
        member_profiles.emplace(key, profile(tech, layout, c));
      }
      const auto& supported = member_profiles.at(key).pins;
      for (const auto& [name, pin] : m.pins) {
        // Preserve the standalone abstract's supported-pin contract. Reusing
        // every raw pin can choose a different best-effort phase for the
        // union and discard pins that an original member still requires.
        if (std::none_of(supported.begin(), supported.end(), [&](const auto& p) { return p.name == name; })) {
          continue;
        }
        auto transformed = pin;
        for (auto& shape : transformed.shapes) {
          const auto a
              = ph::orient_point(shape.rect.xl + m.origin_x, shape.rect.yl + m.origin_y, m.width, m.height, c.place.orientation);
          const auto b
              = ph::orient_point(shape.rect.xh + m.origin_x, shape.rect.yh + m.origin_y, m.width, m.height, c.place.orientation);
          const double dx = double(c.place.x - xl) / tech.dbu;
          const double dy = double(c.place.y - yl) / tech.dbu;
          shape.rect      = {dx + std::min(a.first, b.first),
                             dy + std::min(a.second, b.second),
                             dx + std::max(a.first, b.first),
                             dy + std::max(a.second, b.second)};
        }
        master.pins.emplace(std::to_string(i) + ":" + name, std::move(transformed));
      }
    }
    auto name = "__livehd_rigid_tracks_" + std::to_string(ids[members[j].front()]);
    while (proxy_tech.macros.contains(name)) {
      name += "_";
    }
    proxy_tech.macros.emplace(name, std::move(master));
    proxy.macro             = name;
    proxy.place.x           = xl;
    proxy.place.y           = yl;
    proxy.place.width       = xh - xl;
    proxy.place.height      = yh - yl;
    proxy.place.orientation = ph::Orientation::N;
  }
  const auto before = proxies;
  legalize_macro_tracks(proxy_tech, layout, proxies, hx, hy, move);
  auto result = cells;
  for (size_t j = 0; j < proxies.size(); ++j) {
    const int64_t dx = proxies[j].place.x - before[j].place.x;
    const int64_t dy = proxies[j].place.y - before[j].place.y;
    for (const auto i : members[j]) {
      result[i].place.x += dx;
      result[i].place.y += dy;
    }
  }
  // Verify original member pin profiles independently before writing anything.
  // Pin phases that cannot coexist inside a frozen group reject atomically.
  auto audit = result;
  for (auto& c : audit) {
    c.place.status = ph::Status::FIXED;
  }
  legalize_macro_tracks(tech, layout, audit, hx, hy, false);
  cells = std::move(result);
}

}  // namespace livehd::placement
