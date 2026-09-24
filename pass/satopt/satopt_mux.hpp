// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The per-bit mux facts of satopt (satopt.hpp): candidates filtered by the
// rejection seeds, one proof network (a RAW Lnet over the bit-blasted proof
// cones) handed to a backend prover, and the LGraph rewrite of the proven
// facts. The prover is a callback, so this sweep carries no backend.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "lnet.hpp"
#include "satopt.hpp"

namespace livehd::satopt {

// Proves which outputs of `net` are constant zero, one flag per output in
// output order; nullopt when nothing could be attempted (no prover session).
using Prove_const0 = std::function<std::optional<std::vector<bool>>(const livehd::synth::Lnet& net)>;

struct Mux_prover {
  Prove_const0 prove_const0;
  uint64_t     salt = 0;  // the prover's code identity, part of the proof cache key
};

// The bit-level prover a caller gets when it passes none: pass/abc registers
// ABC's (`&fraig`) at static initialization. Null when nothing registered one
// (an ABC-free build proves no mux facts).
void              register_mux_prover(const Mux_prover& prover);
const Mux_prover* registered_mux_prover();

// Combinational proofs: primary inputs and every state/opaque output are free
// symbols. An unsupported cone or an exhausted budget yields no facts; a search
// the budget (`meter`, null = unlimited) or a size cap cut short is reported
// `complete=false` and never reused as a full one.
std::shared_ptr<const Satopt_result> mux_satopt(hhds::Graph* graph, const Mux_prover& prover, std::string_view cache_dir = {},
                                                bool all_regions = false, Profile profile = Profile::synthesis,
                                                Meter* meter = nullptr);

// Proves per-bit mux arm facts and applies them as an LGraph rewrite: each
// proven arm bit is replaced by its constant or by the other arm's
// (complemented) bit, on that arm's input only. Run on the colored source
// right before partitioning, so every mapper sees the simplified arms.
// `all_regions=false` keeps only muxes whose arm or control logic comes from
// another color.
// With Profile::shared a rewrite also keeps every observable check (a latch Q
// stays a direct arm, a Hotmux is never left foldable without an exclusivity
// proof, arms keep their integer value) and the pass stays idempotent on an
// uncolored graph.
Mux_satopt optimize_muxes(hhds::Graph* graph, const Mux_prover& prover, std::string_view cache_dir = {}, bool all_regions = false,
                          Profile profile = Profile::synthesis, Meter* meter = nullptr);

// Hotmux collapse (todo/livehd/2s-satopt I). A Hotmux whose `unique if`
// exclusivity is proven -- globally, never under a parent's context -- is
// stamped proven, the obligation formal would discharge; cprop's mux sharing
// (pass/cprop/cprop_mux.cpp: region collection, composed activation
// predicates, grouped identical terminals and its own gain rule) then absorbs
// it into its enclosing selection. A Hotmux already carrying a runtime check,
// or one the prover cannot clear, stays a region boundary with its obligation.
// Never-active arms and arms equal to another under their activation are the
// selector and mux-fact proofs' (constants and hotmux stages). Returns the
// Hotmuxes newly stamped.
struct Collapse_satopt {
  uint64_t candidates = 0, queries = 0, proven = 0, refuted = 0, unknown = 0, budget_skips = 0, nodes_removed = 0;
};
Collapse_satopt collapse_hotmuxes(hhds::Graph* graph, Profile profile, Meter* meter = nullptr);

}  // namespace livehd::satopt
