// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include "gui_session.hpp"

namespace livehd::placement::gui {
// The routing-track constraint on a macro's lower-left origin (see Macro_tracks), looked up by the node's master and
// orientation. nullptr: unconstrained (not a macro, a master with no track data, or an orientation not profiled).
inline const Macro_tracks* track_lattice(const Scene& scene, size_t node, const std::string& orient) {
  if (node >= scene.nodes.size()) {
    return nullptr;
  }
  const auto found = scene.track_lattices.find(scene.nodes[node].master);
  if (found == scene.track_lattices.end()) {
    return nullptr;
  }
  for (const auto& lattice : found->second) {
    if (lattice.orient == orient) {
      return &lattice;
    }
  }
  return nullptr;
}

namespace detail {
inline bool on_lattice(const std::vector<std::int64_t>& phases, std::int64_t period, std::int64_t value) {
  if (phases.empty() || period <= 0) {
    return true;
  }
  return std::ranges::any_of(phases, [&](std::int64_t phase) { return ((value - phase) % period + period) % period == 0; });
}
// The nearest allowed value on one axis (the value itself when the axis is unconstrained).
inline std::int64_t nearest_on_lattice(const std::vector<std::int64_t>& phases, std::int64_t period, double value) {
  if (phases.empty() || period <= 0) {
    return std::llround(value);
  }
  std::int64_t best = 0;
  double       gap  = INFINITY;
  for (const auto phase : phases) {
    const auto center = std::llround((value - double(phase)) / double(period));
    for (auto k = center - 1; k <= center + 1; ++k) {
      const auto candidate = phase + k * period;
      if (std::abs(double(candidate) - value) < gap) {
        gap  = std::abs(double(candidate) - value);
        best = candidate;
      }
    }
  }
  return best;
}
}  // namespace detail

// "" when the origin (x, y) DBU puts the macro's signal pins on tracks (or nothing is known); else the axis that misses.
inline std::string track_miss(const Scene& scene, size_t node, const std::string& orient, double x, double y) {
  const auto* lattice = track_lattice(scene, node, orient);
  if (!lattice) {
    return {};
  }
  if (!detail::on_lattice(lattice->phases_x, lattice->period_x, std::llround(x))) {
    return "x";
  }
  if (!detail::on_lattice(lattice->phases_y, lattice->period_y, std::llround(y))) {
    return "y";
  }
  return {};
}

// The origin nearest to (x, y) that track_miss accepts.
inline std::array<double, 2> track_nearest(const Scene& scene, size_t node, const std::string& orient, double x, double y) {
  const auto* lattice = track_lattice(scene, node, orient);
  if (!lattice) {
    return {std::round(x), std::round(y)};
  }
  return {double(detail::nearest_on_lattice(lattice->phases_x, lattice->period_x, x)), double(detail::nearest_on_lattice(lattice->phases_y, lattice->period_y, y))};
}

// What is known of the constraint, for a person or an agent.
inline std::string track_describe(const Scene& scene, size_t node, const std::string& orient) {
  const auto* lattice = track_lattice(scene, node, orient);
  if (!lattice) {
    return "unconstrained: no routing-track data for this macro's pins in this orientation";
  }
  return std::format("{} signal pins on tracks; origin = phase + k*period; x period {} DBU with {} phases, y period {} DBU with {} phases", lattice->pins,
                     lattice->period_x, lattice->phases_x.size(), lattice->period_y, lattice->phases_y.size());
}
}  // namespace livehd::placement::gui
