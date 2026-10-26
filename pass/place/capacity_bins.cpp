// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "capacity_bins.hpp"

#include <algorithm>

namespace livehd::placement {
Capacity_bins::Capacity_bins(const physical::Rect& area, unsigned bins_x, unsigned bins_y)
    : nx(bins_x), ny(bins_y), bounds(area), dx((area.xh - area.xl) / bins_x), dy((area.yh - area.yl) / bins_y) {
  bins.resize(size_t(nx) * ny);
  for (unsigned x = 0; x < nx; ++x) {
    for (unsigned y = 0; y < ny; ++y) {
      bins[index(x, y)].bounds = {bounds.xl + x * dx, bounds.yl + y * dy, bounds.xl + (x + 1) * dx, bounds.yl + (y + 1) * dy};
    }
  }
}

double Capacity_bins::total_capacity() const {
  double total = 0;
  for (const auto& b : bins) {
    total += b.capacity;
  }
  return total;
}

bool Capacity_bins::fits(const Ann_place& a, const Ann_place* b, double limit) const {
  bool ok = true;
  each(footprint(a), [&](size_t bin, double area) {
    const auto&  tile   = bins[bin];
    const double other  = b ? overlap(footprint(*b), tile.bounds) : 0;
    ok                 &= tile.after + area + other <= limit * tile.capacity + epsilon;
  });
  if (b) {
    each(footprint(*b), [&](size_t bin, double area) {
      const auto& tile  = bins[bin];
      ok               &= tile.after + area + overlap(footprint(a), tile.bounds) <= limit * tile.capacity + epsilon;
    });
  }
  return ok;
}

bool Capacity_bins::within_cap(double limit) const {
  return std::none_of(bins.begin(), bins.end(), [&](const auto& b) { return b.after > limit * b.capacity + epsilon; });
}
}  // namespace livehd::placement
