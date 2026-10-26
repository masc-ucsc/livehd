// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "physical_emission.hpp"
#include "reason.hpp"
#include "semantic_region.hpp"

namespace livehd::domplace {

// The v1 physical eligibility of every frozen state, in frozen order:
// Reason::candidate or the code that retains its CMOS+DFF implementation (the
// README table). A candidate's clock control output must be a non-inverted
// native source that is one of frozen.inputs, and its design input name (from
// `input_names`, indexed like frozen.inputs; "" when the input is not a design
// input bit) must equal `clock`. An empty `input_names` marks a region other
// than the design top: its clock ports are not traced to the design clock
// root, so a state clocked by one is retained as definition-clock. Enables are
// already folded into D, so they stay candidates.
// Requires region.frozen; returns no rows without it.
//
// `gating` (indexed by source row, may be empty) carries the clock-gate fold
// (U21): a folded register whose root clock is the design input `clock` is a
// candidate with clock_root/gated_clock set (usyn::Physical_state); a
// refused gate retains the register with the fold's reason code. A candidate
// with a synchronous or asynchronous reset (U22) carries its storage reset:
// the RST net, polarity and value; one whose reset cannot reach RST retains
// with a reset-* code.
struct Eligibility {
  uint32_t                state  = 0;
  Reason                  reason = Reason::candidate;
  std::optional<uint32_t>             clock_root, gated_clock, gate_enable;  // gate_enable: any folded register
  std::optional<usyn::Physical_reset> reset;                                 // a candidate's storage reset (U22)
};
// The code of a source row that is a native barrier, not a register: a
// memory (its scope or role), an opaque/hierarchy row, a transparent latch or
// an ICG. Such a row is never a convertible state; nullopt for a register.
// physical_eligibility checks it first, and the report lists every barrier row
// of a region with it (a native memory has no state bit, so no endpoint row).
std::optional<Reason> barrier_reason(const synth::Source_state_table& table, const synth::Source_state& source);

std::vector<Eligibility> physical_eligibility(const usyn::Stateful_region& region, std::span<const std::string> input_names,
                                              std::string_view clock, std::span<const usyn::Physical_hint> gating = {});

}  // namespace livehd::domplace
