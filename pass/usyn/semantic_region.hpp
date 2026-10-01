// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "endpoint_netlist.hpp"
#include "source_state.hpp"

namespace livehd::usyn {

struct Semantic_region {
  Xag_region                logic;
  // Owned RAW provenance and controls. Its Lid values refer to the imported
  // source Lnet, never to a rewritten XAG or an exported Lnet. Use logic.state
  // for current Q/D signals and state_bits to recover source correspondence.
  // Logical control output indices stay aligned with logic.outputs through
  // optimization and export; these protect clock/reset/value cones from deletion.
  synth::Source_state_table source;
  // Indexed by logic.state: index into source.bits, independent of row order.
  std::vector<uint32_t>     state_bits;
  std::vector<uint32_t>     eligible;
  std::vector<uint32_t>     domains;  // indexed by logic.state; unknown clocks never merge
};

struct Semantic_result {
  Status                         status = Status::invalid;
  std::optional<Semantic_region> region;
  std::string                    reason;
};

// write_logical_module's worst-case node reservation for an exported Lnet:
// one gate plus one memoized inverse per Lnet id, one Flop per latch, one
// Concat per control, plus IO. Every logical stage sizes against it, so a
// region the search admits is never refused only after its complete search.
constexpr uint64_t logical_emission_nodes(uint64_t net_size, uint64_t latches, uint64_t controls, uint64_t inputs,
                                          uint64_t outputs) {
  return 2 * net_size + latches + controls + inputs + outputs + 4;
}
// Largest XAG (search graph) whose exported Lnet still fits that reservation
// under max_nodes: export maps each live XAG node to one Lnet id and adds at
// most one inverter per latch D/output plus a constant. Zero = nothing fits.
constexpr uint32_t logical_search_nodes(uint32_t max_nodes, uint64_t latches, uint64_t controls, uint64_t inputs,
                                        uint64_t outputs) {
  const auto fixed = logical_emission_nodes(latches + outputs + 2, latches, controls, inputs, outputs);
  return fixed >= max_nodes ? 0 : static_cast<uint32_t>((max_nodes - fixed) / 2);
}

// Import a RAW Lnet with its matching source snapshot. Normalize backend D
// polarity, preserve bit/stage names, and classify from semantics, not names.
// Colliding names retain their spelling plus source node/stage/bit identity;
// the owned source snapshot retains the original names unchanged.
// Every ordinary register must be translated completely; refusing incomplete
// input prevents mapping/Liberty choices from silently excluding endpoints.
// Special native barriers remain recorded in source. Target validation emits
// source-located diagnostics; malformed correspondence returns no artifact.
Semantic_result import_semantic_region(const synth::Lnet& net, const synth::Source_state_table& source, synth::State_target target,
                                       Budget& work, uint32_t max_nodes = 2000000);

struct Stateful_region {
  Logical_region                  selected;
  // Same RAW provenance convention as Semantic_region::source. Current state
  // signals are selected.logic.state, with this state_bits correspondence.
  synth::Source_state_table       source;
  std::vector<uint32_t>           state_bits;
  // Present on every successful synthesis result. Low-level writer callers
  // may still supply an unselected logical region without a frozen artifact.
  std::optional<Endpoint_netlist> frozen{};
};

struct Stateful_result {
  Status                         status = Status::invalid;
  std::optional<Stateful_region> region;
  Logical_report                 report;
  std::string                    reason;
};

// The semantic entry to the independent logical pipeline. CMOS retains all
// ordinary state, including noneligible edges; DOMINO applies strict source
// validation. Neither target invokes ABC, SATOPT or an equivalence prover.
// `structural` pays every mandatory step -- semantic import, the logical
// region's admission and identity baseline plus any structural pricing or
// rebuild (synthesize_logical_region), and freezing the published selection --
// with plain spends only, so its charge is a deterministic function of the
// input and its search credits. Only
// `search` credits select among legal results: a starved search publishes the
// minimal (identity) selection, and only a structural refusal publishes none.
Stateful_result synthesize_stateful_region(const synth::Lnet& net, const synth::Source_state_table& source,
                                           synth::State_target target, const Logical_options& options, Budget& structural,
                                           Budget& search);
// One ledger for both parts (structural == search).
Stateful_result synthesize_stateful_region(const synth::Lnet& net, const synth::Source_state_table& source,
                                           synth::State_target target, const Logical_options& options, Budget& work);

}  // namespace livehd::usyn
