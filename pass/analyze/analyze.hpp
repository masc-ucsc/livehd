// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass.analyze — READ-ONLY structural diagnosis of an lg: library.
//
// WHY THIS EXISTS. Every "why won't this simulate?" investigation used to be a
// bisection: run `lhd sim`, read the ONE diagnostic it fail-fasts on, guess the
// class from a node name, fix, re-run (minion takes ~45 minutes per round).
// Worse, the diagnostic names a node — `memory_29184:mask.ex_mask_rf_q` — and
// nothing said whether that was a memory back-edge, a false loop through an
// instance, or a packed-slice artifact, so a plan could be written against the
// wrong class and measure as "no change" (which is exactly what happened to
// todo_sim_pipeline.md's Phase 1: it predicted 5 of minion's comb-loops and
// cleared 0).
//
// This pass answers the whole question at once, over the WHOLE hierarchy, in one
// invocation, and it never fails fast: it reports EVERY finding in EVERY
// definition. It transforms nothing.
//
// The checks are deliberately reimplementations of the questions the downstream
// passes ask, not calls into them: `inou.cgen.sim` refuses on the FIRST offender
// and stops, so asking it "what else is wrong?" is impossible by construction.
//
// TWO LIMITS, stated up front because a survey tool that overstates its reach is
// worse than none:
//
//  1. PER DEFINITION. The loop check's "settled" model cuts a `Sub` call, so a
//     cycle that closes THROUGH an instance chain is invisible to it and will be
//     reported as FALSE. Verilator, which inlines across the hierarchy before it
//     looks, can see such a loop — measured: it reports UNOPTFLAT on minion's
//     `minion_dcache_top` where this pass says FALSE. Treat "FALSE" as "false at
//     this level of hierarchy", and cross-check a design that matters.
//  2. THE lg: LIBRARY, NOT THE EMITTER'S GRAPH. `inou.cgen.sim` mutates the
//     graph before it schedules (inline_clock_gate_cells, flatten_false_loop_subs),
//     so it can find a cycle this pass does not, and
//     vice versa. A disagreement is a finding about those transforms.

#include <cstdint>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::analyze {

// Which checks to run. `loops` and `clocks` are read-only; `colors` RUNS
// Color_acyclic on a scratch copy of the colour attribute, so it is the only
// one that writes anything, and it writes only `attrs::color`.
struct Opts {
  bool loops   = true;
  bool clocks  = true;
  bool colors  = true;
  bool verbose = false;  // per-element rows, not just the offenders
};

// ---------------------------------------------------------------------------
// CHECK 1 — combinational loops.

// How an on-cycle node got there. The distinction is the entire point: the same
// `combinational-loop` diagnostic covers four unrelated repairs.
enum class Loop_kind : uint8_t {
  memory,    // a Memory cell on the cycle: read/forward -> write. Sequential in
             // hardware; a word-level cycle only because the cell is one node.
  sub,       // a Sub instance on the cycle: the classic FALSE loop through an
             // atomic call (o1=a+b feeds the caller which feeds o2=c*d).
  set_mask,  // a packed field-write chain reading its own later version.
  get_mask,  // a packed slice READ on the cycle (the read half of the above).
  other,     // a genuine bit-level loop, or a shape none of the above explains.
};

struct Loop_finding {
  std::string def;          // definition name
  std::string node;         // debug name of a representative on-cycle node
  Loop_kind   kind = Loop_kind::other;
  int         n_on_cycle = 0;  // comb nodes on ANY word-level cycle in this def
  int         n_comb     = 0;  // comb nodes in the def, for scale
  // Does the cycle survive when a Memory read and a Sub call are treated as the
  // boundaries they are? TRUE means the design really has a settle loop (an
  // event-driven simulator would iterate it, and verilator would say
  // UNOPTFLAT). FALSE means the design is fine and the SINGLE-PASS SCHEDULE is
  // what cannot order it — a false positive, and the single most useful bit in
  // this whole report.
  bool        real = false;
  // Every distinct kind present on this def's cycles, most specific first. A def
  // whose cycle passes through both a Memory and a Sub needs both repairs.
  std::vector<Loop_kind> kinds;
};

// ---------------------------------------------------------------------------
// CHECK 2 — clock endpoints.

// How a state element's clock arrives. Ordered by how much machinery it needs:
// everything up to `gated_inline` is what `inou.cgen.sim` folds today.
enum class Clock_kind : uint8_t {
  implicit,       // no clock_pin: the module's own clock
  plain_input,    // a graph input, ungated
  plain_internal, // an internal net that is a clock root (an alias, `clock = clk_i`)
  gated_inline,   // `<clock> & <enables>` visible in THIS definition
  gated_chain,    // a chain of such gates, still all in this definition
  gate_cell,      // the clock is a Sub OUTPUT whose def IS a recognized ICG cell.
                  // cgen_sim inlines those before scheduling, so this is FINE —
                  // measured on minion, all 372 such sites emit clean.
  from_sub,       // a Sub OUTPUT whose def is NOT a recognized gate: opaque
  gates_child_port,  // THIS def gates a clock and feeds it into a CHILD's clock
                  // port. The child sees a plain input and commits every tick,
                  // so the gate becomes dead code — a SILENT miscompile, not a
                  // refusal, and the one this survey exists to surface.
  inverted,       // resolves to a clock but with inverted parity (negedge domain)
  divided,        // a Clock_cell with div != 1
  unresolved,     // nothing above: a derived clock this analysis cannot name
};

struct Clock_finding {
  std::string def;
  std::string element;      // debug name of the flop/latch/memory
  std::string op;           // "Flop" | "Latch" | "Memory" | ...
  Clock_kind  kind = Clock_kind::unresolved;
  int         n_guards   = 0;  // enables folded out of the gate cone
  int         chain_depth = 0;  // gate cells walked (1 == a single gate)
  std::string root;         // resolved clock root, when there is one
};

// ---------------------------------------------------------------------------
// CHECK 3 — colouring validity.

// Which invariant of Color_acyclic broke. See the header of check_colors().
enum class Color_defect : uint8_t {
  uncolored,       // a partitionable node left at NO_COLOR
  cyclic_dag,      // the quotient graph over colours has a directed cycle
  state_spans,     // one partition holds a state element AND its own consumer
};

struct Color_finding {
  std::string  def;
  Color_defect defect = Color_defect::uncolored;
  std::string  detail;      // the offending node / the colour pair on the cycle
  int          n_parts = 0;
  int          n_nodes = 0;
};

struct Report {
  std::vector<Loop_finding>  loops;
  std::vector<Clock_finding> clocks;
  std::vector<Color_finding> colors;
  int n_defs = 0;
  int n_state_elements = 0;
};

[[nodiscard]] const char* to_string(Loop_kind k);
[[nodiscard]] const char* to_string(Clock_kind k);
[[nodiscard]] const char* to_string(Color_defect d);

// Is this clock kind one `inou.cgen.sim` can lower today? Reported alongside
// each finding so the output separates "needs work" from "already fine".
[[nodiscard]] bool sim_can_lower(Clock_kind k);

// Run the enabled checks over `g` (ONE definition) and append to `rep`.
void analyze_def(hhds::Graph* g, const Opts& opts, Report& rep);

// One JSON object per line, plus a final summary object. Stable field names so
// a shell can jq it.
[[nodiscard]] std::string report_json(const Report& rep);

}  // namespace livehd::analyze
