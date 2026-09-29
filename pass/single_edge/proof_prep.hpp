// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Proof preparation: what an equivalence check does to BOTH sides before its
// first query so the two designs are compared in one single-edge time base.
//
// `lhd lec` owned this as a set of file-local helpers, which meant every other
// client of the engine (first among them a since-removed pass.synth publication
// gate) queried designs `lhd lec` would have normalized first, and got a
// different answer. The concrete failure: a latch-based integrated clock gate
// leaves every gated flop with a DERIVED clock unless the gate is recognized and
// folded into a flop enable. The flop-cut induction encoder refuses a derived
// clock, the auto ladder drops into the phase-schedule BMC frame, and that frame
// seeds each side's differently shaped state (one packed register vs its
// bit-blasted cells) with independent synthetic power-on values -- so a correct
// netlist was REFUTED on a register that is never even clocked. Any client that
// shares this preparation proves what `lhd lec` proves.

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::single_edge {

// Behavioral models of explicit `--lib` cells, keyed by their definition gid.
using Cell_models = absl::flat_hash_map<hhds::Gid, hhds::Graph*>;

// 2f-latch M9: recognize instantiated clock gates as `Clock_cell` in `top` and
// every def. Returns the number recognized.
int materialize_clock_cells_all(hhds::Graph* top, const std::vector<hhds::Graph*>& defs);

// Inline the COMBINATIONAL `--lib` cells on clock cones, so phase analysis sees
// buffer/inverter polarity and gates instead of an opaque cell.
void inline_clock_lib_cells(const Cell_models& sub_lib, hhds::Graph* graph);

// Inline STATEFUL `--lib` cells (mapped DFFs), so their state is real body state
// the flop cut can correspond, named after the instance for single-flop models.
void inline_stateful_lib_cells(const Cell_models& sub_lib, hhds::Graph* impl_g);

// Inline integrated clock gates across `top` and each def and fold the gated
// clock into a flop enable where that is a pure P=1 retype. Returns {cells
// inlined, defs folded}; a def that took a gate but could not fold is added to
// `unfolded` so the caller keeps it out of the edge-normalization scan.
std::pair<int, int> inline_clock_gates_and_fold(hhds::Graph* top, const std::vector<hhds::Graph*>& defs,
                                                absl::flat_hash_set<hhds::Graph*>*             unfolded,
                                                const std::function<bool(const hhds::Graph*)>& is_boxed = {});

// One side dropped part of the other's hierarchy (a synthesis partition fuses a
// flat view into region modules the RTL never had): inline, into each def the
// other side still has, every instance whose definition the other side lacks,
// so both sides expose comparable machine state under their hierarchical names.
// Returns the number of instances spliced.
size_t inline_instances_missing_from_other_side(const Cell_models&                               sub_lib,
                                                const std::vector<std::shared_ptr<hhds::Graph>>& side_graphs,
                                                const std::vector<std::shared_ptr<hhds::Graph>>& other_graphs,
                                                hhds::Graph*                                     side_g);

// Outcome of prepare_time_base. Nothing here throws: each caller decides what a
// decline or an error means for it (`lhd lec` refuses; the publication gate
// treats it as an inconclusive proof and keeps the baseline).
struct Time_base {
  std::vector<std::string> recipe_steps;  // what was done, in order, for the caller's report
  bool                     declined     = false;  // edge normalization declined on a side
  bool                     declined_ref = false;  // ... the ref side (else the impl)
  std::string              decline_reason;
  bool                     applied      = false;  // both sides rewritten into ONE time base
  int                      slots        = 1;      // P of that time base
  int                      ref_latches  = 0;
  int                      impl_latches = 0;
  std::string              error;       // hard failure AFTER planning: the caller must not query
  std::string              error_hint;
};

// Put `ref` and `impl` into one single-edge time base, symmetrically:
//   1. inline stateful and clock-cone `--lib` cells on each top and each def
//      that is not itself a `--lib` model (models are shared by both sides);
//   2. recognize clock-gate cells as `Clock_cell` on BOTH sides (a gate seen on
//      one side only would compare a Clock_cell against a Sub);
//   3. inline+fold the remaining gates into flop enables, dropping any def that
//      could not fold from `ref_defs`/`impl_defs`;
//   4. dry-run edge normalization on both sides, and when either needs it apply
//      the max P to BOTH -- a one-sided lowering compares two time bases.
// `quiet_decline` suppresses the refusal diagnostics of the probe: pass it when
// a decline is recoverable (the encoder's read-only phase schedule takes over).
// The caller scales a BMC bound by `slots` when `applied`.
Time_base prepare_time_base(const Cell_models& sub_lib, hhds::Graph* ref, std::vector<hhds::Graph*>& ref_defs, hhds::Graph* impl,
                            std::vector<hhds::Graph*>& impl_defs, bool quiet_decline,
                            const std::function<bool(const hhds::Graph*)>& is_boxed = {});

}  // namespace livehd::single_edge
