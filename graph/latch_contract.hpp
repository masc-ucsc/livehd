// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

// todo/livehd/2f-latch M3 — the shared COMMIT-CLASS analysis and the CONTRACT
// CHECK that guards its precondition. ONE analysis, three intended consumers
// (sim, LEC, timing), so it lives here rather than inside any one of them.
//
// WHY A CONTRACT AT ALL. LiveHD models a latch as a flop-with-enable that
// commits at the CLOSING edge of its transparent window. That is an
// ABSTRACTION WITH A PRECONDITION, not a semantics: it is sound exactly when
// nothing observes the timing INSIDE the transparent window. Designs that do
// observe it (time borrowing, self-timed loops) must be REJECTED with a named
// error — never silently approximated, because the approximation is a
// plausible-looking wrong answer rather than a crash.
namespace livehd::latch_contract {

// The commit class of a state element: WHICH net commits it, and on WHICH edge.
//   posedge flop        -> (clock, RISE)
//   negedge flop        -> (clock, FALL)
//   transparent-HIGH latch (enable active high) -> (enable, FALL)
//   transparent-LOW  latch (enable active low)  -> (enable, RISE)
//
// For a latch the "net" is the ENABLE net — a latch's gate IS its enable
// (user ruling 2026-07-20); there is no separate clock identity.
//
// `net` is the ROOT driver the enable/clock cone resolves to, walked back
// through the identity and boolean-shaping nodes tolg inserts, so two latches
// written `if clk` and `if !clk` resolve to the SAME net with OPPOSITE parity —
// which is what makes a master/slave pair recognizable as opposite phases
// rather than two unrelated elements.
// What the committing net IS. A latch gated by a CLOCK (`if clk { m = d }`,
// the master half of a master/slave pair) and a latch gated by DATA
// (`en = (c==0) or (c==4)`, the latch_verify_hold shape) are structurally
// identical up to the root the cone resolves to, yet M8 must slot them
// differently: a clock-gated latch takes its slot from its polarity, a
// data-gated one is an ordinary flop-with-enable in the domain's LAST slot.
// Collapse the two and a transparent-low polarity canary goes blind.
enum class Net_role : uint8_t {
  Clock,  // the root is a clock: the implicit one, a net some flop clocks on, or a conventional spelling
  Data,   // ordinary computed logic
};

struct Commit_class {
  hhds::Pin_class net;             // root net; invalid iff `implicit_clock`
  bool            rising = false;  // true = commits on the net's RISE
  Net_role        role   = Net_role::Data;
  // A `reg x = 0` with no `clock_pin` driver commits on the MODULE's implicit
  // clock. That is a real commit class, not an unresolvable cone — returning
  // nullopt for it (the pre-M8 behavior) hid the single most common state
  // element in the tree from every consumer of this analysis.
  bool implicit_clock = false;

  // Stable identity of the committing (net, edge) pair, for grouping elements
  // into slots. Derived STRUCTURALLY — never from a nid or traversal order —
  // because M8 assigns slots on both sides of a miter independently and a
  // disagreement is a false REFUTED (see the task page, condition 2).
  [[nodiscard]] std::string net_key() const;
  [[nodiscard]] std::string key() const { return net_key() + (rising ? "+" : "-"); }
};

// The set of ROOT nets this design uses as CLOCKS. Built once per graph (an
// O(N) scan) and handed to every commit_class_of call, so classifying N nodes
// stays O(N) rather than O(N^2).
//
// A net is a clock when a `Flop`/`Fflop` clocks on it, or — when the design
// wires no flop straight to an input (a latch-only or pure-ICG design) — when
// its name is a conventional clock spelling. The fallback matters: without it
// `latch_master_slave` (two latches on `clk`, no flop) would classify its gate
// as DATA and both halves would land in the same slot.
class Design_clocks {
public:
  explicit Design_clocks(hhds::Graph* g);

  [[nodiscard]] bool   is_clock(const hhds::Pin_class& root) const;
  [[nodiscard]] size_t n_clock_inputs() const { return input_names_.size(); }
  [[nodiscard]] bool   has_implicit_clock() const { return implicit_clock_; }

  // Conventional clock spelling, token-wise (`clk`, `clock`, `core_clk`,
  // `clk_2x`). Public so the M8 pass and any future consumer share ONE notion.
  [[nodiscard]] static bool name_looks_like_clock(std::string_view name);

private:
  absl::flat_hash_set<hhds::Class_index> roots_;
  absl::flat_hash_set<std::string>       input_names_;
  bool                                   implicit_clock_ = false;
};

// A recognized INTEGRATED CLOCK GATE cone: `<clock> & <enables...>` driving a
// state element's clock_pin.
//
// This is THE most common latch structure in real hardware, and modelling it as
// a gated CLOCK is both unsound here (the encoder has no clock-identity model,
// so a gated clock and an ungated one look identical) and wasteful (it needs a
// per-step commit term). A gated clock is exactly a FLOP ENABLE: the flop
// commits on the reference clock's edge iff the enables are true at that edge.
// So the right move is to recognize the pattern and rewrite it into the enable,
// which is what the encoder already models natively.
struct Icg_cone {
  hhds::Pin_class              clock;    // the clock operand, resolved to its root
  bool                         clock_inverted = false;  // `~clk & en` gates the FALLING edge
  std::vector<hhds::Pin_class> enables;  // the remaining operands (constants excluded)
  // 2^N clock division. v1 only ever produces 1; a `Clock_cell` carrying any
  // other value is refused BY NAME at every lowering rather than approximated,
  // because a divider's INITIAL PHASE has to become part of its identity or two
  // 180-degrees-apart div-by-2s compare equal -- the clock-blindness
  // false-PROVEN class these guards exist to stop.
  int div = 1;
};

// Decode `clock_pin`'s driver as an ICG cone, or nullopt when it is not one.
//
// THE TRAP this exists to contain: in the canonical `clk & en` BOTH operands are
// graph inputs, so "is it an input?" cannot tell the clock from the enable —
// getting that backwards classifies the ENABLE as the clock and silently
// produces no gating at all. The clock operand is identified by
// `Design_clocks::is_clock` (a net some flop already clocks on, else a
// conventional spelling), and an AMBIGUOUS cone — no clock operand, or more
// than one — returns nullopt so the caller fails closed rather than guessing.
[[nodiscard]] std::optional<Icg_cone> resolve_icg(const hhds::Pin_class& clock_pin, const Design_clocks& clocks);

// ---------------------------------------------------------------------------
// 2f-latch M9 — `Clock_cell`, the ONE recognized clock operator.
//
// `resolve_icg` above recognizes a gate that is VISIBLE IN THE BODY. Real
// designs INSTANTIATE the gate, so the flop's `clock_pin` is an opaque `Sub`
// output and nothing downstream can see it is a gate at all. The functions
// below close that: `clock_op_of` is the ONE recognizer (every consumer calls
// it, so a shape recognized for LEC is recognized for sim by construction) and
// `materialize_clock_cells` rewrites what it recognizes into `Clock_cell`
// nodes, which is the representation each consumer then lowers its own way.

// Read a materialized `Clock_cell` node back into a cone. `clk_ref` becomes
// `clock` (resolved to its root, folding any inversion into `clock_inverted`),
// `en` becomes the single enable. Returns nullopt when `n` is not a Clock_cell.
[[nodiscard]] std::optional<Icg_cone> clock_cell_cone(const hhds::Node_class& n, const Design_clocks& clocks);

// THE shared recognizer. Resolves `clock_pin`'s driver to a clock operation via
// whichever entry point applies:
//   1. a materialized `Clock_cell` node                (clock_cell_cone)
//   2. an inline `<clock> & <enables>` cone in the body (resolve_icg)
// A `Sub`-boundary instance is NOT resolved here -- it cannot be, because its
// enable is a cone of the DEF's ports and has no meaning as a parent pin until
// `materialize_clock_cells` re-roots it. That is the whole reason materializing
// is a separate step rather than a pure query.
[[nodiscard]] std::optional<Icg_cone> clock_op_of(const hhds::Pin_class& clock_pin, const Design_clocks& clocks);

// Is `def` an ICG cell -- i.e. does its body match `clk_o = clk_port & <enable
// cone>` where the enable is held by a latch transparent on the OPPOSITE phase
// of that same clock port? Returns the matched shape when it is.
//
// Clock identity here is STRUCTURAL, never the port name: the clock is the port
// that is BOTH AND-ed into the output AND gates the latch's transparency
// window. A gate cell's own body holds no flop, so `Design_clocks` has no root
// to offer inside the def and a name heuristic would be the only alternative --
// which is exactly what M9 retires.
struct Icg_def_match {
  hhds::Pin_class clk_in;         // the def's clock INPUT pin
  hhds::Pin_class out;            // the def's clock OUTPUT pin
  hhds::Pin_class enable_cone;    // root of the latched enable cone, inside the def
  bool            invert = false; // body is `clk | ~en_latch` (active-low gate) rather than `clk & en_latch`
};
[[nodiscard]] std::optional<Icg_def_match> match_icg_def(hhds::Graph* def);

// Rewrite every recognized clock gate in `g` into a `Clock_cell`.
//
// For an INSTANTIATED cell this CLONES the def's combinational enable cone into
// `g`, re-rooted on the instance's actual drivers, and drops the instance. The
// clone is what makes the cell usable by every consumer without inlining the
// def: minion's `prim_clk_gate` latches `en_i | dft_i.scanmode` (scan forces
// the clock ON, and `dft_i` is a packed struct that graph flows keep as one
// FLAT bus), so the enable is a CONE of two ports, not a wire, and binding it
// to a single parent pin is impossible without re-rooting it.
//
// WHAT IS AND IS NOT PULLED IN. Only the comb enable cone crosses the boundary;
// the enable LATCH does not, because the cell encodes the glitch-free CONTRACT
// ("en is sampled at clk_ref's active edge") instead of the implementation.
// That is why this is not the inlining `inline_clock_gate_cells` does and why
// it does not reintroduce the state comparison that made a TRUSTED gate refute:
// no state element and no new compare point crosses -- the cone is a function
// of nets the parent already drives.
//
// `is_boxed` still applies to the DEF-BODY match: a caller may declare a def
// out of scope entirely. Returns the number of cells materialized.
[[nodiscard]] int materialize_clock_cells(hhds::Graph* g, std::string_view from_pass,
                                          const std::function<bool(const hhds::Graph*)>& is_boxed = {});

// The value a latch passes THROUGH while its window is open: the arm of tolg's
// hold mux (`gate ? d : q`) that is not the latch's own Q. Invalid when `n` is
// not a latch or carries no hold mux (the raw yosys D/EN shape, where `din` is
// already the transparent value).
[[nodiscard]] hhds::Pin_class latch_transparent_arm(const hhds::Node_class& n);

// Inline every instance whose output drives a state element's `clock_pin` and
// whose def contains a Latch — i.e. an INTEGRATED CLOCK GATE CELL.
//
// Real designs do not spell an ICG inline; they instantiate a cell
// (`prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));`), so the gate
// structure sits ONE MODULE LEVEL AWAY and the flop's clock_pin is just an
// opaque `Sub` output. Nothing downstream can see that it is a gate: the
// encoder models it as committing every step, and the M8 fold has no `And` cone
// to recognize.
//
// Inlining the cell — and ONLY such a cell — brings the gate into the parent
// body, where `resolve_icg` recognizes it and the M8 lowering rewrites it into
// the flop's ENABLE. Inlining is unconditionally semantics-preserving, so this
// is safe to run before any analysis, and it is idempotent (an inlined cell is
// no longer a Sub). Returns the number of cells inlined.
//
// `is_boxed` (optional) names the defs the caller has BLACKBOXED — `lhd lec`
// passes its `formal.lec.trust` / collapse predicate. Such a cell is left
// alone, and that is a soundness requirement, not a nicety: trust means "assume
// these two implementations equal, do not compare them", so inlining one pulls
// its internals into the compared cone and a difference trust was covering
// becomes a REFUTATION. Measured on minion, whose `prim_clk_gate` is trusted:
// its enable latch carries no reset, the two front-ends default it 0 vs 1, and
// comparing it turns an honest UNKNOWN into `not equivalent` on a design whose
// two clock gates are line-for-line the same logic. A don't-care is not a
// disproof.
[[nodiscard]] int inline_clock_gate_cells(hhds::Graph* g, std::string_view from_pass,
                                          const std::function<bool(const hhds::Graph*)>& is_boxed = {});

// Commit class of `n`, or nullopt when `n` is not a state element (or its
// controlling cone could not be resolved to a root). `clocks` supplies the
// net-role context; passing nullptr builds a throwaway one from `n`'s graph
// (fine for a one-shot query, quadratic in a loop).
[[nodiscard]] std::optional<Commit_class> commit_class_of(const hhds::Node_class& n, const Design_clocks* clocks = nullptr);

// ---------------------------------------------------------------------------
// M8 trigger. `pass.single_edge` is CONDITIONAL: it does not run at all unless
// the design actually mixes commit classes. A plain posedge single-clock design
// never enters that code path, is never re-emitted, and therefore cannot change
// a verdict — which is the whole blast-radius argument for landing the pass
// without re-baselining the corpus.
//
// This is the ONE scan; do not add a private copy (encode.cpp, cgen_sim.cpp and
// semdiff.cpp already keep three, and that drift is the bug M3 meant to prevent).
struct Single_edge_need {
  bool needed = false;
  int  n_latches       = 0;  // Latch cells
  int  n_negedge_flops = 0;  // Flop/Fflop with posclk known-false
  int  n_icg_flops     = 0;  // Flop/Fflop on a recognized `<clock> & <enables>` gate
  int  n_clock_nets    = 0;  // distinct clock roots flops commit on (implicit counts as one)
  std::string why;           // human-readable trigger reason ("" when not needed)
};

[[nodiscard]] Single_edge_need needs_single_edge(hhds::Graph* g, const Design_clocks* clocks = nullptr);

// Enforce the no-time-borrowing precondition on every latch in `g`. Emits a
// directed `latch-contract` error naming the offending path for each violation
// and returns false if any fired.
//
// Rules enforced (see the .cpp for why rules 3 and 4 from the task page are
// deliberately NOT here):
//   A. a latch's ENABLE may not depend on its own Q      (self-timed gate)
//   B. a latch's D may not depend on its own Q           (transparent self-update)
//      — EXCEPT through its own hold mux, which tolg synthesizes for every
//        Pyrope/slang latch and which is not a real path
//   C. no combinational data path between two latches that are SIMULTANEOUSLY
//      transparent (same root net, same parity) — this is where time borrowing
//      lives. A master/slave pair sits on OPPOSITE parities and is accepted.
bool check(hhds::Graph* g);

}  // namespace livehd::latch_contract
