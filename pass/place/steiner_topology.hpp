// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "rules_cc/cc/runfiles/runfiles.h"
namespace livehd::placement {
using Steiner_location = std::pair<int64_t, int64_t>;
struct Steiner_topology {
  std::vector<Steiner_location>          locations;
  std::vector<size_t>                    terminals;  // Input order, including coincident pins.
  std::vector<std::pair<size_t, size_t>> edges;
};
// Bazel runfiles of the running test, else of this executable; throws when absent.
std::unique_ptr<rules_cc::cc::runfiles::Runfiles> runfiles();

void             initialize_flute_tables();
// Geometric FLUTE topology in integer DBU, before track snapping/obstacle
// avoidance. Edges can require a bend. Does not constitute legal metal.
// Up to 1000 distinct locations; invalid/out-of-range input throws.
Steiner_topology steiner_topology(const std::vector<Steiner_location>& pins);
// Driver-aware placement RC tree. Prim-Dijkstra trades shared wire length for
// source-to-sink distance (alpha 0.3), then shares overlapping adjacent edges.
// The geometric router above remains independent of this timing estimate.
// Same input bounds as steiner_topology; driver indexes the input pins.
Steiner_topology timing_steiner_topology(const std::vector<Steiner_location>& pins, size_t driver);
namespace detail {
// Internal empty-rectangle visibility graph, in deterministic sweep order.
// Locations must be distinct; used by the timing tree and its regression checks.
std::vector<std::vector<size_t>> timing_neighbors(const std::vector<Steiner_location>& locations);
}  // namespace detail
}  // namespace livehd::placement
