// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// The legalization `bin` partition role (synth.place.legalize.bin.*): uniform
// tiles with usable capacity, before/after occupancy and a hard density cap.
// Membership is explicit, by area apportioned across every overlapped bin.
// Bins are column-major: index = x * ny + y.
struct Capacity_bin {
  physical::Rect bounds;
  // Usable area (row sites minus macro keepouts and fixed WELLTAP/SPACER
  // footprints); before/after count logic-cell area.
  double         capacity = 0, before = 0, after = 0;
};
inline double rect_overlap(const physical::Rect& a, const physical::Rect& b) {
  return std::max(0., std::min(a.xh, b.xh) - std::max(a.xl, b.xl)) * std::max(0., std::min(a.yh, b.yh) - std::max(a.yl, b.yl));
}
inline physical::Rect place_rect(const Ann_place& p) {
  return {double(p.x), double(p.y), double(p.x + p.width), double(p.y + p.height)};
}
struct Capacity_bins {
  static constexpr double   epsilon = 1e-6;
  unsigned                  nx = 0, ny = 0;
  physical::Rect            bounds;
  double                    dx = 0, dy = 0;
  std::vector<Capacity_bin> bins;

  Capacity_bins() = default;
  Capacity_bins(const physical::Rect& area, unsigned bins_x, unsigned bins_y)
      : nx(bins_x), ny(bins_y), bounds(area), dx((area.xh - area.xl) / bins_x), dy((area.yh - area.yl) / bins_y) {
    bins.resize(size_t(nx) * ny);
    for (unsigned x = 0; x < nx; ++x) {
      for (unsigned y = 0; y < ny; ++y) {
        bins[index(x, y)].bounds = {bounds.xl + x * dx, bounds.yl + y * dy, bounds.xl + (x + 1) * dx, bounds.yl + (y + 1) * dy};
      }
    }
  }
  size_t index(unsigned x, unsigned y) const { return size_t(x) * ny + y; }
  // Bin containing the cell center, clamped to the grid.
  std::pair<int, int> cell_of(const Ann_place& p) const {
    return {std::clamp(int(std::floor((p.x + .5 * p.width - bounds.xl) / dx)), 0, int(nx) - 1),
            std::clamp(int(std::floor((p.y + .5 * p.height - bounds.yl) / dy)), 0, int(ny) - 1)};
  }
  // f(bin, overlap area) for every bin with a positive overlap.
  template <typename F>
  void each(const physical::Rect& r, F f) const {
    if (rect_overlap(bounds, r) <= 0) {
      return;
    }
    const int x0 = std::clamp(int(std::floor((r.xl - bounds.xl) / dx)), 0, int(nx) - 1);
    const int x1 = std::clamp(int(std::ceil((r.xh - bounds.xl) / dx)) - 1, 0, int(nx) - 1);
    const int y0 = std::clamp(int(std::floor((r.yl - bounds.yl) / dy)), 0, int(ny) - 1);
    const int y1 = std::clamp(int(std::ceil((r.yh - bounds.yl) / dy)) - 1, 0, int(ny) - 1);
    for (int x = x0; x <= x1; ++x) {
      for (int y = y0; y <= y1; ++y) {
        const size_t b = index(x, y);
        const double a = rect_overlap(r, bins[b].bounds);
        if (a > 0) {
          f(b, a);
        }
      }
    }
  }
  void   add_capacity(const physical::Rect& usable) {
    each(usable, [&](size_t b, double a) { bins[b].capacity += a; });
  }
  void   deposit(const Ann_place& p, bool after) {
    each(place_rect(p), [&](size_t b, double a) { (after ? bins[b].after : bins[b].before) += a; });
  }
  double density(size_t i, bool after) const {
    const auto& b = bins[i];
    return b.capacity > 0 ? (after ? b.after : b.before) / b.capacity : 0;
  }
  double total_capacity() const {
    double total = 0;
    for (const auto& b : bins) {
      total += b.capacity;
    }
    return total;
  }
  // Would placing `a` (and optionally `b`, jointly) keep every touched bin's
  // `after` occupancy within limit * capacity?
  bool fits(const Ann_place& a, const Ann_place* b, double limit) const {
    bool ok = true;
    each(place_rect(a), [&](size_t bin, double area) {
      const auto&  tile   = bins[bin];
      const double other  = b ? rect_overlap(place_rect(*b), tile.bounds) : 0;
      ok                 &= tile.after + area + other <= limit * tile.capacity + epsilon;
    });
    if (b) {
      each(place_rect(*b), [&](size_t bin, double area) {
        const auto& tile  = bins[bin];
        ok               &= tile.after + area + rect_overlap(place_rect(a), tile.bounds) <= limit * tile.capacity + epsilon;
      });
    }
    return ok;
  }
  // Strictly below limit * capacity: the bin can still receive area.
  bool has_room(size_t i, double limit) const {
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
};
}  // namespace livehd::placement
