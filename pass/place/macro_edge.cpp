// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_edge.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace livehd::placement {
namespace {
// Axis-generic view: `lo/hi` along the snap axis, `a/b` across it.
struct Span {
  int64_t lo, hi, a, b;
};
int64_t round_up(int64_t value, int64_t origin, int64_t grid) {
  const auto offset = value - origin;
  return origin + (offset >= 0 ? (offset + grid - 1) / grid : -((-offset) / grid)) * grid;
}
int64_t round_down(int64_t value, int64_t origin, int64_t grid) {
  const auto offset = value - origin;
  return origin + (offset >= 0 ? offset / grid : -((-offset + grid - 1) / grid)) * grid;
}
int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// The admissible origin nearest `value` on the far side of it (at or above when `up`, at or below otherwise): on the
// origin grid and on the object's track lattice for this axis. nullopt when no lattice phase meets the grid.
std::optional<int64_t> admissible(const Edge_object& o, int axis, int64_t value, bool up, int64_t origin, int64_t grid) {
  const auto snapped = up ? round_up(value, origin, grid) : round_down(value, origin, grid);
  const auto period  = o.period[axis];
  if (period <= 0 || o.phases[axis].empty()) {
    return snapped;
  }
  std::optional<int64_t> best;
  for (const auto phase : o.phases[axis]) {
    auto k = floor_div(snapped - phase, period);
    if (up && phase + k * period < snapped) {
      ++k;
    }
    for (int step = 0; step < 64; ++step, k += up ? 1 : -1) {  // the lattice and the grid meet within lcm/period steps
      const auto candidate = phase + k * period;
      if (((candidate - origin) % grid + grid) % grid == 0) {
        if (!best || (up ? candidate < *best : candidate > *best)) {
          best = candidate;
        }
        break;
      }
    }
  }
  return best;
}
// One axis pass. Returns the macros moved on this axis.
std::vector<int64_t> snap_axis(std::vector<Edge_object>& objects, bool x_axis, const Edge_snap_limits& l,
                               Edge_snap_report& report) {
  const auto n       = objects.size();
  const auto core_lo = x_axis ? l.core_xl : l.core_yl;
  const auto core_hi = x_axis ? l.core_xh : l.core_yh;
  const auto halo    = x_axis ? l.halo_x : l.halo_y;
  const auto space   = x_axis ? l.space_x : l.space_y;
  const auto cross   = x_axis ? l.space_y : l.space_x;  // macro clearance across the axis
  const auto cross_h = x_axis ? l.halo_y : l.halo_x;    // obstacle clearance across the axis
  const auto max_gap = x_axis ? l.max_gap_x : l.max_gap_y;
  const auto span_of = [&](const Edge_object& o) { return x_axis ? Span{o.xl, o.xh, o.yl, o.yh} : Span{o.yl, o.yh, o.xl, o.xh}; };
  const auto set_lo  = [&](Edge_object& o, int64_t lo) {
    auto& a = x_axis ? o.xl : o.yl;
    auto& b = x_axis ? o.xh : o.yh;
    b       = lo + (b - a);
    a       = lo;
  };
  std::vector<int64_t> moved;
  if (max_gap <= 0 && !l.slivers) {
    return moved;
  }
  const int  axis     = x_axis ? 0 : 1;
  const auto mid      = core_lo + (core_hi - core_lo) / 2;
  const auto pin_edge = std::max(halo, x_axis ? l.pin_edge_x : l.pin_edge_y);
  // the core-edge clearance of object i's face toward the low or high core edge
  const auto edge_clear = [&](const Edge_object& o, bool low) {
    const unsigned face = x_axis ? (low ? 1u : 2u) : (low ? 4u : 8u);
    return (o.pin_faces & face) != 0 ? pin_edge : halo;
  };
  // Push-back: a movable macro whose pinned face sits closer to its core edge than pin_edge moves away from the edge
  // to that clearance. A movable macro in the way on the far side is pushed along by the same amount (a cascade along
  // the axis, keeping every macro channel); a fixed obstacle or the opposite core edge stops it and the whole cascade
  // is rolled back. Movable standard cells are not obstacles: they are legalized afterwards.
  if (pin_edge > halo) {
    std::function<bool(size_t, int64_t, bool, int)> push = [&](size_t i, int64_t new_lo, bool low, int depth) -> bool {
      const auto s   = span_of(objects[i]);
      const auto len = s.hi - s.lo;
      if (depth > 64 || (low ? new_lo + len > core_hi - halo : new_lo < core_lo + halo)) {
        return false;
      }
      for (size_t j = 0; j < n; ++j) {
        if (j == i) {
          continue;
        }
        const auto o   = span_of(objects[j]);
        const auto gap = objects[j].macro ? cross : cross_h;
        if (!(o.a < s.b + gap && s.a < o.b + gap) || (low ? o.lo < s.hi : o.hi > s.lo)) {
          continue;  // clear across the axis, or not on the far side
        }
        const auto need = objects[j].macro ? space : halo;
        if (low ? o.lo >= new_lo + len + need : o.hi <= new_lo - need) {
          continue;
        }
        if (!objects[j].macro || !objects[j].movable) {
          return false;
        }
        const auto j_lo = admissible(objects[j], axis, low ? new_lo + len + need : new_lo - need - (o.hi - o.lo), low, core_lo, l.grid);
        if (!j_lo || !push(j, *j_lo, low, depth + 1)) {
          return false;
        }
      }
      set_lo(objects[i], new_lo);
      return true;
    };
    for (int side = 0; side < 2; ++side) {
      const bool          low = side == 0;
      std::vector<size_t> order;
      for (size_t i = 0; i < n; ++i) {
        const auto s = span_of(objects[i]);
        if (objects[i].macro && objects[i].movable && ((s.lo + s.hi) / 2 < mid) == low) {
          order.push_back(i);
        }
      }
      std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
        const auto a = span_of(objects[i]), b = span_of(objects[j]);
        const auto ka = low ? -a.lo : a.hi, kb = low ? -b.lo : b.hi;
        return ka != kb ? ka < kb : i < j;
      });
      for (const auto i : order) {
        const auto s     = span_of(objects[i]);
        const auto clear = edge_clear(objects[i], low);
        if (clear <= halo || (low ? s.lo >= core_lo + clear : s.hi <= core_hi - clear)) {
          continue;
        }
        const auto len    = s.hi - s.lo;
        const auto target = admissible(objects[i], axis, low ? core_lo + clear : core_hi - clear - len, low, core_lo, l.grid);
        if (!target || (low ? *target <= s.lo : *target >= s.lo)) {
          continue;
        }
        std::vector<int64_t> before(n);
        for (size_t j = 0; j < n; ++j) {
          before[j] = span_of(objects[j]).lo;
        }
        if (!push(i, *target, low, 0)) {
          for (size_t j = 0; j < n; ++j) {
            set_lo(objects[j], before[j]);
          }
          continue;
        }
        for (size_t j = 0; j < n; ++j) {
          const auto now = span_of(objects[j]).lo;
          if (now != before[j]) {
            report.distance += std::abs(now - before[j]);
            ++report.moves;
            moved.push_back(static_cast<int64_t>(j));
          }
        }
      }
    }
  }
  // Low side: macros whose center is below the core middle slide toward core_lo.
  for (int side = 0; side < 2; ++side) {
    const bool          low = side == 0;
    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i) {
      const auto s = span_of(objects[i]);
      if (objects[i].macro && ((s.lo + s.hi) / 2 < mid) == low) {
        order.push_back(i);
      }
    }
    std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
      const auto a = span_of(objects[i]), b = span_of(objects[j]);
      const auto ka = low ? a.lo : -a.hi, kb = low ? b.lo : -b.hi;
      return ka != kb ? ka < kb : i < j;
    });
    std::vector<char> anchored(n, 0);
    for (const auto i : order) {
      const auto s     = span_of(objects[i]);
      // The core edge (keeping the halo inside the core, or pin_edge for a pinned face) anchors the chain.
      const auto edge  = edge_clear(objects[i], low);
      int64_t    limit = low ? core_lo + edge : core_hi - edge;
      bool       chain = true;
      int64_t    anchor_clear = edge;  // the clearance the tightest anchor imposes (a sliver is narrower than it)
      bool       anchor_macro = false;
      unsigned   anchor_faces = 0;
      for (size_t j = 0; j < n; ++j) {
        if (j == i) {
          continue;
        }
        const auto o   = span_of(objects[j]);
        const auto gap = objects[j].macro ? cross : cross_h;
        if (!(o.a < s.b + gap && s.a < o.b + gap)) {
          continue;  // clear across the axis: not in the path
        }
        const auto clear = objects[j].macro ? space : halo;
        if (low ? o.hi <= s.lo : o.lo >= s.hi) {
          const auto candidate = low ? o.hi + clear : o.lo - clear;
          const bool tighter   = low ? candidate > limit : candidate < limit;
          if (tighter) {
            limit        = candidate;
            chain        = anchored[j];
            anchor_clear = clear;
            anchor_macro = objects[j].macro;
            anchor_faces = objects[j].pin_faces;
          } else if (candidate == limit && anchored[j]) {
            chain = true;
          }
        }
      }
      const auto gap = low ? s.lo - limit : limit - s.hi;
      if (gap < 0) {
        continue;  // already overlapping its anchor
      }
      if (gap == 0) {
        anchored[i] = chain;
        continue;
      }
      // the face of this macro toward the anchor, and the anchor's face toward it
      const unsigned own_face   = x_axis ? (low ? 1u : 2u) : (low ? 4u : 8u);
      const unsigned other_face = x_axis ? (low ? 2u : 1u) : (low ? 8u : 4u);
      const bool     pinned     = anchor_macro && (objects[i].pin_faces & own_face) && (anchor_faces & other_face);
      const bool     edge_move  = chain && gap <= max_gap;
      const bool     sliver     = l.slivers && gap < anchor_clear && !pinned;
      if ((!edge_move && !sliver) || !objects[i].movable) {
        continue;
      }
      // Stay on the origin grid and the track lattice without crossing the anchor's clearance.
      const auto target = admissible(objects[i], axis, low ? limit : limit - (s.hi - s.lo), low, core_lo, l.grid);
      if (!target) {
        continue;
      }
      const auto delta = low ? s.lo - *target : *target - s.lo;
      if (delta <= 0) {
        continue;
      }
      set_lo(objects[i], *target);
      anchored[i]      = chain;
      report.distance += delta;
      ++report.moves;
      moved.push_back(static_cast<int64_t>(i));
    }
  }
  return moved;
}
}  // namespace

Edge_snap_report snap_macros_to_edges(std::vector<Edge_object>& objects, const Edge_snap_limits& limits) {
  if (limits.grid <= 0 || limits.core_xh <= limits.core_xl || limits.core_yh <= limits.core_yl || limits.halo_x < 0
      || limits.halo_y < 0 || limits.space_x < 0 || limits.space_y < 0 || limits.max_gap_x < 0 || limits.max_gap_y < 0) {
    throw std::invalid_argument("invalid macro edge snap limits");
  }
  Edge_snap_report  report;
  std::vector<char> touched(objects.size(), 0);
  for (const bool x_axis : {true, false}) {
    for (const auto i : snap_axis(objects, x_axis, limits, report)) {
      touched[static_cast<size_t>(i)] = 1;
    }
  }
  report.moved = std::accumulate(touched.begin(), touched.end(), int64_t{0});
  return report;
}
}  // namespace livehd::placement
