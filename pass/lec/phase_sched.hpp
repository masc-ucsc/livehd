// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"  // also brings in ankerl::unordered_dense (the Gid set below)

// todo/livehd/2f-lec + 2f-latch M10 — the HIERARCHY-AWARE FORMAL PHASE SCHEDULE.
//
// This is a READ-ONLY analysis. It colors nothing, rewrites no LGraph,
// synthesizes no phase counter and threads no timing state through module
// ports; `pass.single_edge` (M8) stays a standalone LGraph->LGraph transform and
// the independent Icarus oracle. What this produces is metadata the ENCODER
// consults: for one source period, which of four ordered MICROSTEPS each
// instantiated state endpoint commits in.
//
//   0 Close_low   active-LOW clock-role latches close (immediately before rise)
//   1 Rise        posedge flops, posedge memory ports, data-gated latches
//   2 Close_high  active-HIGH clock-role latches close (immediately before fall)
//   3 Fall        negedge flops, negedge memory ports
//
// A batch evaluates from the state committed by every EARLIER microstep of the
// same period and commits its own endpoints simultaneously. Microsteps 0+1
// share one symbolic input snapshot and 2+3 share another (a latch-close
// microstep shares the snapshot of its adjacent edge), so `rise` and `fall` may
// legitimately sample different input values -- which is exactly the property
// `phase_posneg_pipeline` pins.
//
// WHY IT MUST BE DESIGN-WIDE. The leaves-first driver proves every def with that
// def as its own top, where its clock PORTS are unbound free inputs. minion's
// prim_rf_*_preview family declares two clock ports that all 32 instantiation
// sites tie to one net; resolving clocks per-def would fail closed on the whole
// family. The walk here is hierarchical (`forward_hier` + `inp_edges()`
// threading), so a leaf's clock port resolves through the instance binding to
// the net the PARENT drives -- one root, one schedule.
namespace livehd::lec {

enum class Phase : uint8_t {
  Close_low  = 0,
  Rise       = 1,
  Close_high = 2,
  Fall       = 3,
};

inline constexpr int kMicrosteps = 4;

[[nodiscard]] const char* phase_name(Phase p);

// NOTE on WHICH microsteps actually run. A latch-close microstep exists only for
// a CLOCK-ROLE latch (or a clock guard sampled there); with none, it would carry
// the same state and the same input snapshot as the edge batch beside it, so the
// caller drops it and a plain posedge/negedge design costs TWO encodes per
// period, not four. The two OBSERVATION points are then "the first active
// microstep" (it sees the previous period's fall batch committed) and "the first
// active microstep after the rise" — which is exactly "after rise and after
// fall" whether or not the close microsteps are present. See query.cpp's
// `steps` / `obs_a` / `obs_b`.

struct Phase_endpoint {
  Phase       phase            = Phase::Rise;
  // A LATCH whose window is controlled by the root clock. Its gate is TIMING,
  // not data: the encoder must ABSORB it (commit unconditionally in the close
  // microstep, din = the transparent arm of tolg's hold mux) rather than AND it
  // into the enable. AND-ing it evaluates the clock as a free data input once
  // per microstep, which is the measured `always @(posedge 'hx)` failure M8 hit.
  bool        clock_role_latch = false;
  // An ALWAYS-TRANSPARENT latch: no enable pin at all (tolg wires none when the
  // reg is written on every path) or a constant-true one. Its window never
  // closes, so it stores nothing -- it is a COMBINATIONAL BUFFER, and cgen
  // already emits it as `always_comb` with a blocking assign. Encoding it as a
  // flop-with-enable would insert a full period of delay that the hardware does
  // not have.
  bool        transparent      = false;
  // Microstep the endpoint's clock guard is SAMPLED in (parity of the CELL, not
  // of the consumer): an ordinary gate samples before the root rise, an inverted
  // one before the root fall. One sample serves every consumer in the period.
  Phase       guard_sample     = Phase::Close_low;
  // Key of the sampled-guard state cut, shared by every consumer of the same
  // cell. Empty when the endpoint's clock is ungated. The cut is always WRITTEN
  // (at `guard_sample`) before it is READ (at the consumer's microstep) inside
  // one period, so its power-on value is dead -- which is why a key that fails
  // to match across the miter costs precision, never soundness.
  std::string guard_key;
};

struct Phase_plan {
  // A NAMED REFUSAL (`ok == false`) decides nothing: formal must fail closed,
  // regardless of formal.strict, exactly like an encoder refusal.
  bool        ok = true;
  std::string error;

  // The 4-microstep schedule is REQUIRED. False means every endpoint commits at
  // the root rise, i.e. the legacy one-step-is-one-period encoding is exact and
  // must be kept byte-for-byte (that is the blast-radius argument).
  bool multi = false;

  absl::flat_hash_map<std::string, Phase_endpoint> ep;  // box_node_key(node) -> endpoint

  // guard_key -> the enable cone(s) to AND. A CHAIN of gates contributes one
  // entry per cell (`gate(gate(clk,en0),en1)` -> {en0, en1}), canonicalized to
  // ONE sampled guard on the root clock.
  absl::flat_hash_map<std::string, std::vector<hhds::Occurrence_pin>> guard_cones;

  // Counts, for diagnostics and the schedule SIGNATURE that rides the verdict
  // cache key (a cached verdict computed under a different schedule is not a
  // verdict for this one).
  int         n_flop_rise         = 0;
  int         n_flop_fall         = 0;
  int         n_latch_low         = 0;  // clock-role, closes before the rise
  int         n_latch_high        = 0;  // clock-role, closes before the fall
  int         n_latch_data        = 0;  // data-gated: ordinary tick semantics, rise batch
  int         n_latch_transparent = 0;  // always-open: a combinational buffer, not state
  int         n_mem_rise          = 0;
  int         n_mem_fall          = 0;
  int         n_guards            = 0;  // distinct sampled clock guards
  int         n_guard_low         = 0;  // ...of those, sampled before the RISE (ordinary gate)
  int         n_guard_high        = 0;  // ...sampled before the FALL (the active-low gate flavour)
  int         n_roots             = 0;  // distinct clock roots any endpoint commits on
  std::string root_clock;               // the resolved root clock net name ("" = implicit)

  // The encoder needs the plan (latch cells it otherwise refuses, or a gate
  // chain it otherwise cannot canonicalize) even when there is no SUB-PERIOD
  // requirement. `multi` is the narrower question: does one source period have
  // to become four microsteps.
  // A design with TWO OR MORE unrelated clock roots keeps the encoder's existing
  // detected-edge path: it has no total order to schedule against, and taking it
  // over at P == 1 would silently replace real edge DETECTION with "commits every
  // step" (measured on tests/equiv/mclk_derived, whose second domain is a gated
  // internal net -- it refuted). A multi-root design that also needs microsteps
  // is refused by name instead; see plan_phases.
  [[nodiscard]] bool needs_plan() const {
    if (multi) {
      return true;  // a latch / negedge endpoint has NO other encoding; a multi-root one is refused BY NAME
    }
    if (n_latch_data > 0 || n_latch_transparent > 0) {
      return true;  // likewise: the legacy encoder refuses a Latch cell outright
    }
    // Gate FOLDING is the one thing the legacy path can already do (M9 encodes a
    // Clock_cell exactly at P == 1), so taking it over buys only the chain
    // canonicalization -- not worth displacing the detected-edge model.
    return n_roots <= 1 && n_guards > 0;
  }

  // TWO OR MORE unrelated clock roots. The schedule has no total order to offer
  // (and refuses outright if microsteps are required), but a LATCH still has no
  // other encoding -- so the plan owns latches here and every flop / memory
  // stays on the encoder's detected-edge path. Taking those over at P == 1 would
  // replace real edge DETECTION with "commits every step", which refuted
  // tests/equiv/mclk_derived.
  [[nodiscard]] bool multi_root() const { return n_roots > 1; }

  [[nodiscard]] std::string signature() const;
  [[nodiscard]] std::string describe() const;
};

// Build the schedule for `g`, descending the instance tree. `collapse_defs` is
// the encoder's proven/trusted collapse set (formal.lec.collapse + .trust): such
// a def is a BOX that models its own state, so its internals are neither
// scheduled here nor encoded there. Passing it matters -- minion trusts
// `prim_clk_gate`, whose body holds a latch that would otherwise make every
// design containing one look like it needs a phase schedule.
// ---------------------------------------------------------------------------
// THE CLOCK FOREST (2f-lec "Clock-graph propagation"). Built ONCE per design,
// TOP-DOWN, before any definition is proven.
//
// Resolving a clock bottom-up from each endpoint is not enough, and the two ways
// it falls short are both common:
//
//   * A Pyrope `reg x = 0` has NO `clock_pin` driver at all — it commits on the
//     module's own clock. There is no cone to walk, so it becomes a synthetic
//     root of its own.
//   * A cone that stops at a boundary (a collapsed child is deliberately opaque)
//     resolves to the CHILD's port rather than to the net the parent drives, so
//     one net acquires as many "roots" as it has port spellings. minion names
//     the same clock `clk_i` in 85 defs and `clock` in 6.
//
// Measured on minion `core_top`, which declares exactly ONE clock port in both
// Pyrope and Verilog: the analysis reported THREE roots, `{<implicit>, clk_i,
// clock}` — and the two sides disagreed (3 vs 2), which no design property can.
//
// So propagate downward instead: the selected top's clock INPUTS are the roots;
// every child's clock port takes the root of the net its parent drives into it;
// and a module's IMPLICIT clock is the root that reaches that module. A
// `Clock_cell` is a DERIVATION of a root — gate, inversion, division — never a
// new root, which is what keeps "find all the splits" a property of the
// SCHEDULE rather than of the clock forest.
//
// Keyed by def graph NAME (per side; the two designs have their own graphs). The
// port name "" is that def's implicit clock. A def whose instantiation sites
// disagree gets no entry and falls back to per-endpoint resolution.
struct Clock_forest {
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, std::string>> port_root;

  [[nodiscard]] const std::string* find(std::string_view def, std::string_view port) const {
    auto d = port_root.find(std::string{def});
    if (d == port_root.end()) {
      return nullptr;
    }
    auto p = d->second.find(std::string{port});
    return p == d->second.end() ? nullptr : &p->second;
  }
  [[nodiscard]] bool empty() const { return port_root.empty(); }
};

[[nodiscard]] Phase_plan plan_phases(hhds::Graph* g, const absl::flat_hash_map<std::string, bool>* collapse_defs = nullptr,
                                     const Clock_forest* forest = nullptr);

}  // namespace livehd::lec
