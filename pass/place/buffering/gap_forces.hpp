// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/placement.hpp"
#include "negotiation.hpp"

namespace livehd::placement::buffering {
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 1
using Gap_force_payload = DreamPlace::native::PinTiming;
#else
// Diagnostic carrier only; no native force API is claimed on this dependency.
struct Gap_force_payload {
  std::vector<int>    driver;
  std::vector<double> weight;
  double              coefficient                                = .25;
  bool                operator==(const Gap_force_payload&) const = default;
};
#endif
struct Gap_force_result {
  Gap_force_payload timing;
  size_t applied = 0, unbound_driver = 0, virtual_driver = 0, fixed_driver = 0, no_active_force = 0, suppressed_pairs = 0;
  bool   supported_api = false;
};
// Build one complete timing payload for a synchronous GP checkpoint. Targets
// are converted from microns to native design units. Old gap targets/suppression
// disappear on every rebuild. Critical/window targets freeze suppression of
// straight pairs pointing away from the gap at THIS checkpoint; the resulting
// objective stays differentiable between checkpoints. Native target fractions
// share the original per-driver timing weight and remain bounded.
//
// Existing physical driver pins require explicit owner bindings. Virtual buffer
// stages cannot be bound before pool activation and remain explicitly counted;
// no force is silently redirected to the original driver. Missing/weightless
// owners are reported. A dependency without the gap API reports unsupported.
// No reservations, density, placement, graph, or optimizer state is mutated.
Gap_force_result bind_gap_forces(const DreamPlace::native::Design&, const Gap_force_payload&,
                                 const std::map<std::string, int>& physical_drivers, const std::vector<Gap_entry_pull>&,
                                 double native_units_per_um, double maximum_fraction = .25);
}  // namespace livehd::placement::buffering
