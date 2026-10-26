// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/placement.hpp"
#include "negotiation.hpp"

namespace livehd::placement::buffering {
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
using Density_footprint = DreamPlace::native::DensityReservation;
#else
// Diagnostic state only; the published dependency has no reservation kernel.
struct Density_footprint {
  std::string owner;
  double      x = 0, y = 0, width = 0, height = 0;
  int         materialized_node                          = -1;
  bool        operator==(const Density_footprint&) const = default;
};
#endif
struct Density_reservation_state {
  std::vector<Density_footprint>                  footprints;
  // Net -> uniquely named prospective/repair-owned buffer instances.
  std::map<std::string, std::vector<std::string>> owners;
  bool                                            operator==(const Density_reservation_state&) const = default;
};
struct Density_binding_result {
  Density_reservation_state proposal;
  size_t                    replaced = 0, retired = 0, unmet = 0;
  size_t                    virtual_buffers = 0, materialized_buffers = 0;
  double                    virtual_area = 0, materialized_area = 0;
  bool                      supported_api = false;
};
// Pure checkpoint preparation from freshly negotiated finite alternatives.
// Ledger coverage and actual physical bodies are checked before conversion.
// Successful replacements/explicit retirements release their previous demand;
// unmet and unmanaged owners stay protected. Every materialized instance needs
// a fresh explicit binding in the target Design. Returned areas use native
// length units squared, not site-rounded legal margin or capacity subtraction.
//
// The caller commits this complete payload together with gap forces and its
// graph/pool transaction, after full timing/rank/electrical checks. This does
// not create/remove nodes, certify graph edits, or change density/filler policy.
Density_binding_result bind_reservation_density(const DreamPlace::native::Design&, const Density_reservation_state&,
                                                const std::vector<Allocation_net>&, const Allocation_result&,
                                                double native_units_per_um, double site_dbu_per_um,
                                                const std::map<std::string, int>& materialized_instances = {},
                                                const std::set<std::string>&      retired_nets           = {});
}  // namespace livehd::placement::buffering
