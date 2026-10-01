// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "endpoint.hpp"
#include "endpoint_pairs.hpp"
#include "residual.hpp"
#include "xag_lnet.hpp"

namespace livehd::usyn {

// A selected cell's native inputs refer to Logical_region::logic.graph.
// Nonnegative producers instead name an earlier cell's logical output (whose
// complement is also available). Formula literals choose the required rail.
// Unlike an Endpoint_solution's search cuts, these bindings survive residual
// rewrites, including replacements by a constant or a complemented signal.
struct Logical_cell {
  Gate_formula          formula;
  Gate_formula::Metrics metrics;
  std::vector<Xsignal>  inputs;
  std::vector<int32_t>  producers;
  uint32_t              phase = 1;
  bool                  latch = false;
};

// Exact implementation identity for a static-fed phase-1 cell. Ignore output
// polarity because either rail is available. Unknown domains share only within
// one state owner. Returns no key on invalid topology or budget refusal.
std::optional<std::vector<uint64_t>> phase_cell_key(const Logical_cell& cell, uint32_t state, uint32_t domain, Budget& work);

struct Logical_endpoint {
  uint32_t                  state_index = 0;
  std::string               name;
  std::vector<Logical_cell> cells;
  bool                      whole_cone = false;
  std::string               origin;
};

struct Logical_cost {
  bool     operator==(const Logical_cost&) const = default;
  uint64_t static_logic = 0, inverters = 0, domino = 0;
  uint64_t total() const { return static_logic + inverters + domino; }
};

struct Logical_region {
  // Complete behavioral CMOS expansion: original state/ports, no phase-1
  // storage. Selected cells remain separately available for typed emission.
  Xag_region                    logic;
  std::vector<Logical_endpoint> endpoints;
  Logical_cost                  cost;
};

struct Logical_options {
  Endpoint_options endpoint;
  // AND/XOR weights are taken from endpoint.cost so regional acceptance and
  // residual search use the same proxy. Other residual budgets are independent.
  Residual_options residual;
  uint32_t         max_nodes         = 2000000;
  uint64_t         endpoint_work     = 16000000;
  uint32_t         pair_candidates   = 32;  // 0 disables coordinated pair reselection
  uint32_t         pair_trials       = 64;  // total popped candidates, including revisits/admission failures
  uint32_t         pair_choices      = 4;   // additional interface pool per endpoint (0 disables, maximum 8)
  uint32_t         pair_inputs       = 16;  // common independent basis, not per-endpoint support
  uint64_t         pair_work         = 16000000;
  bool             optimize_residual = true;
  bool             feedback          = true;
};

struct Pair_report {
  uint64_t joint_recode_windows = 0, joint_recode_partitions = 0, joint_recode_encodings = 0;
  uint64_t joint_recode_phases = 0, joint_recode_attempts = 0, joint_recode_retained = 0;
  uint64_t joint_recode_bytes = 0, joint_recode_combinations = 0, joint_recode_wins = 0;
  uint64_t joint_care_windows = 0, joint_care_partitions = 0, joint_care_phases = 0, joint_care_attempts = 0;
  uint64_t joint_care_retained = 0, joint_care_bytes = 0, joint_care_combinations = 0, joint_care_wins = 0;
  uint64_t joint_source_pairs                   = 0;
  bool     operator==(const Pair_report&) const = default;
  uint64_t candidates = 0, attempts = 0, combinations = 0, wins = 0, work = 0;
  uint64_t choices = 0, choice_combinations = 0;
  uint64_t trials = 0, refreshes = 0, requeues = 0, stale_skips = 0;
  uint64_t shared_nodes = 0, more_than_two = 0, domain_skips = 0;
  uint64_t support_skips = 0, window_skips = 0, gain_skips = 0, bounded_windows = 0;
  uint64_t fanout_windows = 0, fanout_ports = 0, fanout_skips = 0, fanout_wins = 0;
  uint64_t joint_windows = 0, joint_partitions = 0, joint_candidates = 0, joint_divisors = 0, joint_combinations = 0,
           joint_wins = 0;
  bool     exhausted  = false;
};

// Disjoint logical-region budget charges, including failed/rolled-back work.
// Admission is the structural part: admission and boundary validation, the
// identity baseline's validation, expansion and pricing, and any structural
// pricing or rebuild of an initial selection the search could not price.
// The other stages are the search. Selection includes initial expansion and
// pricing; residual/feedback include their rebinding, validation and pricing.
// No translation, freeze, I/O or tmap.
struct Logical_work {
  bool     operator==(const Logical_work&) const = default;
  uint64_t admission = 0, selection = 0, pairs = 0, residual = 0, feedback = 0, cleanup = 0;
  uint64_t total() const { return admission + selection + pairs + residual + feedback + cleanup; }
};

struct Logical_report {
  bool                         operator==(const Logical_report&) const = default;
  std::vector<Endpoint_report> initial;
  Residual_report              residual;
  Pair_report                  pairs;
  Logical_work                 work;
  Logical_cost                 before, after_pairs, after_residual, after;
  uint64_t                     feedback_attempts = 0, feedback_wins = 0;
  // Endpoints still published as the credit-free identity endpoint they fell
  // back to because their search could not complete one (all of them when the
  // selection exceeded the node limits and the baseline was rebuilt); a later
  // pair or feedback win that replaces one is not counted.
  uint64_t                     identity_fallbacks = 0;
  uint32_t                     feedback_rounds    = 0;
  bool                         residual_accepted  = false;
  bool                         exhausted          = false;
};

struct Logical_result {
  Status                        status = Status::invalid;
  std::optional<Logical_region> region;
  Logical_report                report;
  std::string                   reason;
};

// The caller supplies semantic eligibility, never inferred from register names
// or Lnet alone. All unselected state and external outputs remain protected.
// Select endpoints, optimize the live shared residual, then reconsider affected
// endpoints once. Every accepted change preserves boundary functions and must
// pass the total mixed-network cost check, including explicit static NOTs.
// No ABC, solver, SATOPT invocation, or clock/state conversion occurs here.
// Optional domains are indexed by source.state. Empty/unknown domains disable
// cross-endpoint trials; graph topology and register names cannot infer clocks.
//
// Two ledgers. `structural` pays the mandatory steps: admission, boundary
// validation and the minimal legal selection -- every eligible endpoint as its
// identity endpoint -- with its validation, expansion and pricing, plus any
// structural pricing or rebuild of a selection the search could not price
// (report.work.admission). Only its refusal (or invalid input) publishes no
// region. `search` pays the endpoint searches and every later stage; a search
// that cannot complete an endpoint keeps its identity; one that runs out while
// pricing keeps its selection (a selection of identities is the priced
// baseline; one with a searched cell is priced on `structural`); a selection
// beyond the node limits publishes the baseline, its copy charged to
// `structural`; and later stages keep their complete incumbent. So a starved
// search degrades to the minimal selection instead of failing, and its credit
// floor never includes structural work.
Logical_result synthesize_logical_region(const Xag_region& source, std::span<const uint32_t> eligible,
                                         const Logical_options& options, Budget& structural, Budget& search,
                                         std::span<const uint32_t> domains = {});
// One ledger for both parts (structural == search): a search that exhausts it
// while pricing a selection with a searched cell refuses the region.
Logical_result synthesize_logical_region(const Xag_region& source, std::span<const uint32_t> eligible,
                                         const Logical_options& options, Budget& work, std::span<const uint32_t> domains = {});

}  // namespace livehd::usyn
