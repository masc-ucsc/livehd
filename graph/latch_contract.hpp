// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
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
  hhds::Pin_class net;                     // root net; invalid iff `implicit_clock`
  bool            rising         = false;  // true = commits on the net's RISE
  Net_role        role           = Net_role::Data;
  // A `reg x = 0` with no `clock_pin` driver commits on the MODULE's implicit
  // clock. That is a real commit class, not an unresolvable cone — returning
  // nullopt for it (the pre-M8 behavior) hid the single most common state
  // element in the tree from every consumer of this analysis.
  bool            implicit_clock = false;

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
  // `hier == true` scans the whole INSTANCE TREE and resolves each clock cone
  // through the instance bindings (`inp_edges()` threads a boundary), so a
  // leaf's clock PORT is not mistaken for a clock net of its own. That is what
  // the formal phase schedule needs: the leaves-first driver proves a def with
  // its clock ports unbound, and minion's prim_rf_*_preview family declares two
  // clock ports that every instantiation site ties to ONE net.
  explicit Design_clocks(hhds::Graph* g, bool hier = false, const ankerl::unordered_dense::set<hhds::Gid>* opaque = nullptr);

  [[nodiscard]] bool   is_clock(const hhds::Pin_class& root) const;
  [[nodiscard]] bool   is_clock(const hhds::Occurrence_pin& root) const;
  [[nodiscard]] size_t n_clock_inputs() const { return input_names_.size(); }
  [[nodiscard]] bool   has_implicit_clock() const { return implicit_clock_; }

  // Conventional clock spelling, token-wise (`clk`, `clock`, `core_clk`,
  // `clk_2x`). Public so the M8 pass and any future consumer share ONE notion.
  [[nodiscard]] static bool name_looks_like_clock(std::string_view name);

private:
  // A clock root, keyed by (owning body gid, class index). hhds::Class_index is
  // unique only INSIDE one graph body, and the hier ctor fills this registry
  // from EVERY body in the instance tree: with a bare Class_index key, an
  // ordinary data net in a child whose pin index happens to equal a clock root
  // recorded in another module answers `is_clock` — which picks the ENABLE of an
  // `clk & en` ICG as the clock operand and schedules the wrong edges.
  using Root_key = std::pair<hhds::Gid, uint64_t>;
  template <typename Pin>
  [[nodiscard]] static Root_key root_key(const Pin& p) {
    auto* g = p.get_graph();
    return {g == nullptr ? hhds::Gid_invalid : g->get_gid(), static_cast<uint64_t>(p.get_class_index().value)};
  }

  absl::flat_hash_set<Root_key>    roots_;
  absl::flat_hash_set<std::string> input_names_;
  bool                             implicit_clock_ = false;
};

// Root of a CONTROL cone (a `clock_pin` driver, or a latch's `enable`) plus the
// accumulated inversion parity, walking back through the identity and
// boolean-shaping nodes tolg and the readers emit (Mux(s,0,1), EQ(x,0/1), Not,
// the `x & 1` width mask, Get_mask/Sext, and a Clock_cell -> its clk_ref).
//
// This is THE one cone walker; every consumer must use it rather than keep a
// private copy, because a parity bit counted in two places is how a double
// negation survives (measured: `wire nclk = ~clk` was invisible to the encoder
// while the `posclk` pin form was handled, so the same machine came back PROVEN
// against posedge AND REFUTED against negedge).
//
// Hierarchy-aware by construction: it walks `inp_edges()`, which threads
// instance boundaries under a hierarchical walk.
struct Control_root {
  hhds::Pin_class net;
  bool            inverted = false;
};
struct Occurrence_control_root {
  hhds::Occurrence_pin net;
  bool                 inverted = false;
};
// `stop_at_clock_cell` leaves a `Clock_cell` as the root instead of
// canonicalizing through it to the reference clock -- what a caller that must
// visit every cell of a GATE CHAIN needs (each cell carries its own enable).
[[nodiscard]] Control_root            control_root(hhds::Pin_class p, bool stop_at_clock_cell = false);
[[nodiscard]] Occurrence_control_root control_root(hhds::Occurrence_pin p, bool stop_at_clock_cell = false);

// Hierarchy-aware driver of a named sink. `get_driver_of_sink_name` is
// class-local and stops at a sub's GraphIO pin; `inp_edges()` resolves across
// the instance boundary, which is what a flop deep in an instance needs.
[[nodiscard]] hhds::Pin_class      sink_driver_hier(const hhds::Node_class& n, std::string_view sink_name);
[[nodiscard]] hhds::Occurrence_pin sink_driver_hier(const hhds::Occurrence_node& n, std::string_view sink_name);

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
  hhds::Pin_class              clock;                   // the clock operand, resolved to its root
  bool                         clock_inverted = false;  // `~clk & en` gates the FALLING edge
  std::vector<hhds::Pin_class> enables;                 // the remaining operands (constants excluded)
  // 2^N clock division. v1 only ever produces 1; a `Clock_cell` carrying any
  // other value is refused BY NAME at every lowering rather than approximated,
  // because a divider's INITIAL PHASE has to become part of its identity or two
  // 180-degrees-apart div-by-2s compare equal -- the clock-blindness
  // false-PROVEN class these guards exist to stop.
  int                          div = 1;
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
//   1. a materialized `Clock_cell` node                 (clock_cell_cone)
//   2. an INSTANTIATED gate cell, re-rooted read-only   (sub_icg_cone)
//   3. an inline `<clock> & <enables>` cone in the body (resolve_icg)
//
// Entry 2 is a QUERY, never a rewrite: it only succeeds when the def's enable
// reduces to one of the def's own INPUT PORTS, because only then does it name a
// pin the PARENT already drives. A deeper in-def enable cone stays refused here
// and remains `materialize_clock_cells`' job -- that one CLONES the cone into
// the caller, manufacturing a parent pin that does not otherwise exist, which is
// why materializing is still a separate mutating step.
//
// WHAT THE CONE DOES NOT CARRY: the enable's SAMPLE POINT. A real ICG holds its
// enable in a latch transparent on the opposite phase, and the returned cone
// abstracts that latch away -- a consumer that commits `enables` AT the
// reference edge reads a value the cell sampled half a period earlier. Consumers
// that need the sample point (inou.cgen.sim) still fold the cell structurally
// first; the rest treat the enable as an edge-sampled qualifier.
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
  hhds::Pin_class clk_in;       // the def's clock INPUT pin
  hhds::Pin_class out;          // the def's clock OUTPUT pin
  hhds::Pin_class enable_cone;  // root of the latched enable cone, inside the def
  // The ACTIVE-LOW GATE FLAVOUR: `clk | ~en_latch` (enable latched while the
  // clock is HIGH) rather than `clk & en_latch` (latched while it is LOW). Both
  // gate the SAME reference edges -- neither output moves while the enable is
  // deasserted -- so this is NOT an inverted clock and must not flip any
  // consumer's edge. It moves the enable's SAMPLE POINT to the phase before the
  // reference FALL, which is observable: `phase_clock_gate_invert`'s enable is a
  // register that commits at the rise, so sampling it pre-rise reads the
  // previous period's value -- a wrong verdict, not an UNKNOWN.
  bool            invert = false;
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

// The value a latch passes THROUGH while its window is open. Peels tolg's hold
// mux (`gate ? d : q`) when `din` still carries one; once
// cprop::canonicalize_latch_holds has stripped it (the open condition moves to
// `enable`), `din` -- a graph input, a const, or a residual value mux with no
// Q arm -- IS the through-value and is returned as-is. Invalid only when `n`
// is not a latch or a hold mux has no non-Q arm (fail closed).
[[nodiscard]] hhds::Pin_class      latch_transparent_arm(const hhds::Node_class& n);
[[nodiscard]] hhds::Occurrence_pin latch_transparent_arm(const hhds::Occurrence_node& n);

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

// The structural clock/reset interface of a definition. A clock input is a
// port from which state in the definition subtree takes an edge, directly or
// through a recognized gate. A reset input is a port from which that state's
// reset derives; active_low tells how the port must be interpreted at the call
// site. Neither classification depends on the port spelling.
//
// The cache is caller-owned because graph preparation can inline gate cells or
// materialize occurrences. Sharing a process-global answer across those
// rewrites would make the result depend on which consumer queried first.
struct Reset_input_port {
  uint32_t port_id    = 0;
  bool     active_low = false;

  friend bool operator==(const Reset_input_port&, const Reset_input_port&) = default;
};

struct Reset_input_ports {
  std::vector<Reset_input_port> ports;
  // False means some reset-bearing state in the subtree is driven by a cone
  // that cannot be reduced to one declared input. A caller may still use the
  // listed ports for clock correctness, but must not use them to skip the
  // instance during simulation.
  bool                          complete = true;
};

struct Clock_input_ports {
  absl::flat_hash_set<uint32_t> ports;
  // Same discipline as Reset_input_ports::complete, and for the same reason:
  // an empty port set means "nothing in the subtree takes an edge" ONLY when
  // the walk got all the way down. A body-less callee, a mutually
  // instantiating pair, or a clock cone that does not reduce to a declared
  // input all produce an empty-looking answer that a caller must NOT read as
  // "there is nothing to gate here".
  bool                          complete = true;
};

struct Clock_port_cache {
  // node_hash_map: both memos hand out references that outlive later queries
  // (callers bind `const auto& ports = ...` and the walk itself recurses while
  // an outer frame holds one). A flat map moves its values on rehash.
  absl::node_hash_map<const hhds::Graph*, Clock_input_ports> clock_memo;
  absl::flat_hash_set<const hhds::Graph*>                    clock_busy;
  absl::node_hash_map<const hhds::Graph*, Reset_input_ports> reset_memo;
  absl::flat_hash_set<const hhds::Graph*>                    reset_busy;
};

[[nodiscard]] const Clock_input_ports& clock_input_interface(const std::shared_ptr<hhds::Graph>& def, Clock_port_cache& cache);
// The port set alone, for the consumers that only ask "does this port clock
// state inside?" and have no gating decision riding on completeness.
[[nodiscard]] const absl::flat_hash_set<uint32_t>& clock_input_ports(const std::shared_ptr<hhds::Graph>& def,
                                                                     Clock_port_cache&                   cache);
[[nodiscard]] const Reset_input_ports& reset_input_ports(const std::shared_ptr<hhds::Graph>& def, Clock_port_cache& cache);

// Insert Clock_cell(en = __valid | reset_asserted) on the structural clock
// inputs of conditionally activated Sub instances. This is a post-lowering
// library phase: call lowering has the composed __valid value, but callee
// bodies (which identify their clock/reset ports) are not all complete yet.
//
// An instance whose clock or reset interface came back INCOMPLETE is refused
// with a `time` error rather than skipped: this phase replaced tolg's
// name-based gate, so declining to gate one silently lets its state advance
// while __valid is false. A resolved-but-stateless callee is skipped quietly.
//
// Idempotent; returns the number of cells inserted.
[[nodiscard]] int gate_activation_clocks(hhds::Graph* g, std::string_view from_pass, Clock_port_cache& cache);

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
  bool        needed          = false;
  int         n_latches       = 0;  // Latch cells
  int         n_negedge_flops = 0;  // Flop/Fflop with posclk known-false
  int         n_icg_flops     = 0;  // Flop/Fflop on a recognized `<clock> & <enables>` gate
  int         n_clock_nets    = 0;  // distinct clock roots flops commit on (implicit counts as one)
  std::string why;                  // human-readable trigger reason ("" when not needed)
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
