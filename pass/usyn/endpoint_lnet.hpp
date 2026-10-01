// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "endpoint.hpp"
#include "xag_lnet.hpp"

namespace livehd::usyn {

struct Cmos_endpoint {
  uint32_t          state_index = 0;
  Endpoint_solution solution;
};

// Expand selected endpoint formulas into one logical CMOS network for optional
// whole-network technology mapping. Keep every original register, boundary
// order, name and initialization; phase-1 cells add only combinational logic.
// No DOMINO or extra latch is emitted here. State without a supplied selection
// is preserved, allowing the caller's explicitly classified state barriers.
// Eligibility/clock/reset metadata belongs to the region driver, not Lnet.
// Invalid correspondence or resource refusal returns no partial netlist.
Lnet_result expand_cmos(const Xag_region& region, std::span<const Cmos_endpoint> endpoints, const Endpoint_options& options,
                        Budget& work, uint32_t max_nodes = 2000000);

}  // namespace livehd::usyn
