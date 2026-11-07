// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_edge.hpp"

#include <algorithm>
#include <numeric>
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
  if (max_gap <= 0) {
    return moved;
  }
  const auto mid = core_lo + (core_hi - core_lo) / 2;
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
      // The core edge (keeping the halo inside the core) anchors the chain.
      int64_t    limit = low ? core_lo + halo : core_hi - halo;
      bool       chain = true;
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
            limit = candidate;
            chain = anchored[j];
          } else if (candidate == limit && anchored[j]) {
            chain = true;
          }
        }
      }
      const auto gap = low ? s.lo - limit : limit - s.hi;
      if (!chain || gap < 0) {
        continue;  // not anchored to the edge, or already overlapping its anchor
      }
      if (gap == 0) {
        anchored[i] = 1;
        continue;
      }
      if (gap > max_gap || !objects[i].movable) {
        continue;
      }
      // Stay on the origin grid without crossing the anchor's clearance.
      const auto target = low ? round_up(limit, core_lo, l.grid) : round_down(limit - (s.hi - s.lo), core_lo, l.grid);
      const auto delta  = low ? s.lo - target : target - s.lo;
      if (delta <= 0) {
        continue;
      }
      set_lo(objects[i], target);
      anchored[i]      = 1;
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
