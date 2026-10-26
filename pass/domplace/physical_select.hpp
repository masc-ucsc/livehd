// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "domino_reference.hpp"
#include "native_cost.hpp"
#include "physical_emission.hpp"

namespace livehd::domplace {

struct Select_options {
  enum class Policy : uint8_t { area, all };
  Policy                       policy   = Policy::area;
  bool                         identity = false;   // also convert one-literal endpoints
  std::string                  clock;              // the selected domain's design input (port name)
  uint64_t                     work = 50'000'000;  // the selection's own slice of the caller's budget
  usyn::Function_search_limits functions;          // the logical search's limits; the output is forced positive
};

struct Select_context {
  const Domino_reference*        reference          = nullptr;
  const usyn::Native_cost_model* cmos               = nullptr;  // required by Policy::area
  double                         cmos_register_area = 0;        // the register the CMOS map would use
};

// Post-freeze choice between each eligible state's provisional domino stage
// and its original CMOS+DFF, in frozen order, deterministic and bounded by
// options.work. A candidate's DominoLatch formula and table are rechecked;
// constant and duplicate inputs are cofactored away, and a complemented output
// is resynthesized with a positive output under the frozen Gate_constraints
// and options.functions: polarity-illegal when that search finishes without a
// formula, polarity-search when it stops at its own limits.
// Rails follow gate_rails; every complemented rail producer, a converted
// state's Q included, is charged one A_NOT. Policy::area converts only when
// A_domino + A_rails < A_register + the Liberty estimate of the D-cone logic
// above the rail cut that no other root (output, other state's D) and no rail
// producer's own cone reaches; a tie retains CMOS. Once the work runs out every remaining candidate retains
// CMOS with `selection-budget`. Every converted state names a variant-0
// master interface. nullopt refuses the region: no frozen netlist, a two-phase
// netlist, a phase-one Domino cell, or an inconsistent frozen cell.
std::optional<usyn::Physical_emission> select_physical(const usyn::Stateful_region& region,
                                                       std::span<const std::string> input_names, const Select_options& options,
                                                       const Select_context& context, usyn::Budget& work);

}  // namespace livehd::domplace
