// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
namespace livehd::placement {
using Steiner_location = std::pair<int64_t, int64_t>;
struct Steiner_topology {
  std::vector<Steiner_location>          locations;
  std::vector<size_t>                    terminals;  // Input order, including coincident pins.
  std::vector<std::pair<size_t, size_t>> edges;
};
void             initialize_flute_tables();
// Geometric FLUTE topology in integer DBU, before track snapping/obstacle
// avoidance. Edges can require a bend. Does not constitute legal metal.
// Up to 1000 distinct locations; invalid/out-of-range input throws.
Steiner_topology steiner_topology(const std::vector<Steiner_location>& pins);
}  // namespace livehd::placement
