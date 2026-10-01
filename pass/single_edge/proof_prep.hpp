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

// Both `--lib` splices below take an instance of a `--lib` cell from the body
// the ENCODER would use for it: the design's own definition when the design's
// library holds one (spliced by id through its own IO, correct by
// construction; on a Verilog side this is the elaborated copy of the model),
// and the `--lib` model only for a BODY-LESS instance, bound by port NAME
// through the instance's IO (graph/inline_sub.hpp). Putting the model in place
// of a body the design carries would compare the model, not the design (an lg:
// netlist with its own bypassed clock gate PROVED against the gated RTL). Each
// returns "" on success, or the refusal for the first instance that cannot be
// spliced (a model lacking a port or stating a width differently): the caller
// must NOT query, since leaving that cell for a later pass is how an id-bound
// splice once dropped a clock gate's output and PROVED a netlist with a
// swapped gate.

// Inline the COMBINATIONAL `--lib` cells on clock cones, so phase analysis sees
// buffer/inverter polarity and gates instead of an opaque cell.
[[nodiscard]] std::string inline_clock_lib_cells(const Cell_models& sub_lib, hhds::Graph* graph);

// Inline STATEFUL `--lib` cells (mapped DFFs), so their state is real body state
// the flop cut can correspond, named after the instance for single-flop models.
[[nodiscard]] std::string inline_stateful_lib_cells(const Cell_models& sub_lib, hhds::Graph* impl_g);

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
// `side_g` (the side's top, or nullptr) is a host even when the other side
// names its top differently. Returns the number of instances spliced, and adds
// to `absorbed` the name of each definition an instance of which was spliced.
size_t inline_instances_missing_from_other_side(const Cell_models&                               sub_lib,
                                                const std::vector<std::shared_ptr<hhds::Graph>>& side_graphs,
                                                const std::vector<std::shared_ptr<hhds::Graph>>& other_graphs, hhds::Graph* side_g,
                                                absl::flat_hash_set<std::string>* absorbed = nullptr);

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
//      a splice the model cannot bind by port name sets `error`;
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
