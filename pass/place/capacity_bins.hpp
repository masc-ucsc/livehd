// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Geometry and occupancy accounting for the legalization `bin` partition role
// (synth.place.legalize.bin.*): uniform tiles with usable capacity,
// before/after occupancy and a hard density cap. Membership is by area
// apportioned across every overlapped bin. Bins are column-major:
// index = x * ny + y. The caller fills `capacity` (terminal legalization
// deposits its free row strips after macro-halo and WELLTAP/SPACER
// reservation); SAT legalization still keeps its own Density_model.
struct Capacity_bin {
  physical::Rect bounds;
  double         capacity = 0, before = 0, after = 0;  // usable area; logic-cell area before/after
  double         density(bool after_moves) const { return capacity > 0 ? (after_moves ? after : before) / capacity : 0; }
};
struct Capacity_bins {
  static constexpr double epsilon = 1e-6;
  static double           overlap(const physical::Rect& a, const physical::Rect& b) {
    return std::max(0., std::min(a.xh, b.xh) - std::max(a.xl, b.xl)) * std::max(0., std::min(a.yh, b.yh) - std::max(a.yl, b.yl));
  }
  static physical::Rect footprint(const Ann_place& p) {
    return {double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)};
  }

  unsigned                  nx = 0, ny = 0;
  physical::Rect            bounds;
  double                    dx = 0, dy = 0;
  std::vector<Capacity_bin> bins;

  Capacity_bins() = default;
  Capacity_bins(const physical::Rect& area, unsigned bins_x, unsigned bins_y);
  size_t              index(unsigned x, unsigned y) const { return size_t(x) * ny + y; }
  // (x, y) bin coordinates of the cell center, clamped to the grid.
  std::pair<int, int> bin_xy(const Ann_place& p) const {
    return {std::clamp(int(std::floor((p.x + .5 * p.width - bounds.xl) / dx)), 0, int(nx) - 1),
            std::clamp(int(std::floor((p.y + .5 * p.height - bounds.yl) / dy)), 0, int(ny) - 1)};
  }
  // f(bin, overlap area) for every bin with a positive overlap.
  template <typename F>
  void each(const physical::Rect& r, F f) const {
    if (overlap(bounds, r) <= 0) {
      return;
    }
    const int x0 = std::clamp(int(std::floor((r.xl - bounds.xl) / dx)), 0, int(nx) - 1);
    const int x1 = std::clamp(int(std::ceil((r.xh - bounds.xl) / dx)) - 1, 0, int(nx) - 1);
    const int y0 = std::clamp(int(std::floor((r.yl - bounds.yl) / dy)), 0, int(ny) - 1);
    const int y1 = std::clamp(int(std::ceil((r.yh - bounds.yl) / dy)) - 1, 0, int(ny) - 1);
    for (int x = x0; x <= x1; ++x) {
      for (int y = y0; y <= y1; ++y) {
        const size_t b = index(x, y);
        const double a = overlap(r, bins[b].bounds);
        if (a > 0) {
          f(b, a);
        }
      }
    }
  }
  void deposit(const Ann_place& p, bool after) {
    each(footprint(p), [&](size_t b, double a) { (after ? bins[b].after : bins[b].before) += a; });
  }
  double total_capacity() const;
  // Would placing `a` (and optionally `b`, jointly) keep every touched bin's
  // `after` occupancy within limit * capacity?
  bool   fits(const Ann_place& a, const Ann_place* b, double limit) const;
  // Every bin's `after` occupancy is within limit * capacity.
  bool   within_cap(double limit) const;
  // Strictly below limit * capacity: the bin can still receive area.
  bool   has_room(size_t i, double limit) const {
    const auto& b = bins[i];
    return b.capacity > 0 && b.after < limit * b.capacity - epsilon;
  }
  // f(bin) for the bins at Chebyshev distance `ring` from (cx, cy), column-major;
  // ring 1 is the eight neighbors.
  template <typename F>
  void each_ring(int cx, int cy, int ring, F f) const {
    for (int x = std::max(0, cx - ring); x <= std::min(int(nx) - 1, cx + ring); ++x) {
      for (int y = std::max(0, cy - ring); y <= std::min(int(ny) - 1, cy + ring); ++y) {
        if (std::max(std::abs(x - cx), std::abs(y - cy)) == ring) {
          f(index(x, y));
        }
      }
    }
  }
  // f(half-plane) for the unvisited exterior of that ring: left, right, below
  // and above, when bins remain there. The upper half-plane starts `reach_y`
  // below its bin edge, where a cell origin still overlaps that bin.
  template <typename F>
  void each_ring_exterior(int cx, int cy, int ring, double reach_y, F f) const {
    if (cx - ring > 0) {
      f(physical::Rect{-INFINITY, -INFINITY, bounds.xl + (cx - ring) * dx, INFINITY});
    }
    if (cx + ring + 1 < int(nx)) {
      f(physical::Rect{bounds.xl + (cx + ring + 1) * dx, -INFINITY, INFINITY, INFINITY});
    }
    if (cy - ring > 0) {
      f(physical::Rect{-INFINITY, -INFINITY, INFINITY, bounds.yl + (cy - ring) * dy});
    }
    if (cy + ring + 1 < int(ny)) {
      f(physical::Rect{-INFINITY, bounds.yl + (cy + ring + 1) * dy - reach_y, INFINITY, INFINITY});
    }
  }
};
}  // namespace livehd::placement
