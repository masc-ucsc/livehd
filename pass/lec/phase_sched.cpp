// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "phase_sched.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace livehd::lec {

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

// box_node_key lives in encode.cpp (one definition, shared identity).
std::string box_node_key(const hhds::Node_class& n);
std::string box_node_key(const hhds::Occurrence_node& n);

const char* phase_name(Phase p) {
  switch (p) {
    case Phase::Close_low : return "close_low";
    case Phase::Rise      : return "rise";
    case Phase::Close_high: return "close_high";
    case Phase::Fall      : return "fall";
  }
  return "?";
}

namespace {

bool posclk_is_false(const hhds::Occurrence_node& n) {
  auto pc = lc::sink_driver_hier(n, "posclk");
  return pc.is_known_false();
}

// A resolved clock cone: the ROOT net it hangs off, the accumulated edge parity,
// and every enable collected along a CHAIN of gates.
//
// Chains matter and are not hypothetical: `phase_clock_gate_chain` gates the
// gate. `latch_contract::clock_op_of` canonicalizes straight to the reference
// clock, which is right for a commit CLASS but would silently drop the inner
// cell's enable here, so this walker steps one cell at a time
// (`control_root(..., stop_at_clock_cell=true)`).
struct Clock_chain {
  hhds::Occurrence_pin              root;                       // invalid => unresolved / implicit
  bool                              inverted          = false;  // edge parity accumulated from cone inversions
  // The ACTIVE-LOW gate flavour (`clk | ~en_latch`) latches its enable while the
  // reference clock is HIGH, so the guard must be sampled before the FALL. It
  // does NOT change any consumer's edge (see Icg_cone::invert).
  bool                              guard_before_fall = false;
  bool                              gated             = false;  // at least one enable was collected
  bool                              refused           = false;  // a div != 1 (or a cycle) was hit
  std::string                       why;                        // refusal text
  std::vector<hhds::Occurrence_pin> guards;                     // enables to AND into the commit condition
  // Original inline-gate enables before ICG-specific transparent-arm peeling.
  // A latch whose own window is `clk & held_en` consumes held_en LIVE; replacing
  // it with the ICG latch's transparent input samples the wrong half-period.
  std::vector<hhds::Occurrence_pin> live_guards;
};

// Structural digest of a cone, used to give the SAMPLED GUARD a name. Two
// front-ends reading the same RTL produce the same shape, so the key matches
// across the miter whenever the cones really are the same; when they are not,
// the two sides simply thread separate guard cuts, which is sound because a
// guard is always SAMPLED before it is READ inside one period (its power-on
// value is dead).
std::string cone_digest(const hhds::Occurrence_pin& p, int depth = 0) {
  if (p.is_invalid()) {
    return "-";
  }
  if (gu::is_graph_input_pin(p)) {
    return "i:" + std::string(gu::pin_name_of(p));
  }
  if (p.is_const()) {
    return "k:" + gu::const_of(p).to_string();
  }
  if (depth >= 8) {
    return "...";
  }
  auto        n = p.get_master_node();
  std::string s{Ntype::get_name(gu::type_op_of(n))};
  s += "(";
  std::vector<std::string> kids;
  for (const auto& e : n.inp_edges()) {
    kids.push_back(std::to_string(static_cast<int>(e.sink.get_port_id())) + "=" + cone_digest(e.driver, depth + 1));
  }
  std::sort(kids.begin(), kids.end());  // pin-id keyed, so commutative operands still agree
  for (size_t i = 0; i < kids.size(); ++i) {
    s += (i != 0 ? "," : "") + kids[i];
  }
  return s + ")";
}

Clock_chain resolve_chain(hhds::Occurrence_pin clk, const lc::Design_clocks& clocks, int depth = 0);

// Is `p` a path that reaches a real clock? Used to pick the clock operand of an
// inline `<clock> & <enables>` cone: BOTH operands are ordinary nets, so "is it
// an input?" cannot tell them apart -- getting that backwards classifies the
// ENABLE as the clock and produces no gating at all (measured, see resolve_icg).
bool reaches_clock(const hhds::Occurrence_pin& p, const lc::Design_clocks& clocks, int depth) {
  if (depth >= 6) {
    return false;
  }
  const auto cr = lc::control_root(p, /*stop_at_clock_cell=*/true);
  if (cr.net.is_invalid()) {
    return false;
  }
  if (clocks.is_clock(cr.net)) {
    return true;
  }
  if (gu::is_graph_input_pin(cr.net) || cr.net.is_const()) {
    return false;
  }
  auto       n  = cr.net.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Clock_cell) {
    return true;  // a gate's output is a clock by construction
  }
  if (op != Ntype_op::And) {
    return false;
  }
  for (const auto& e : n.inp_edges()) {
    if (!e.driver.is_const() && reaches_clock(e.driver, clocks, depth + 1)) {
      return true;
    }
  }
  return false;
}

Clock_chain resolve_chain(hhds::Occurrence_pin clk, const lc::Design_clocks& clocks, int depth) {
  Clock_chain ch;
  auto        cur = clk;
  for (int hops = 0; hops < 16 && !cur.is_invalid(); ++hops) {
    const auto cr = lc::control_root(cur, /*stop_at_clock_cell=*/true);
    ch.inverted   = ch.inverted != cr.inverted;
    if (cr.net.is_invalid()) {
      return ch;
    }
    if (gu::is_graph_input_pin(cr.net) || cr.net.is_const()) {
      ch.root = cr.net;
      return ch;
    }
    auto       n  = cr.net.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Clock_cell) {
      if (auto d = lc::sink_driver_hier(n, "div"); d.is_const()) {
        const auto& dv = gu::const_of(d);
        const int  iv = dv.is_just_i64() ? static_cast<int>(dv.to_just_i64()) : 0;
        if (iv != 1) {
          // A divider's INITIAL PHASE is part of its identity; recording only
          // `div=2` makes two 180-degrees-apart dividers compare equal, which is
          // the clock-blindness false-PROVEN class. Named refusal, never an
          // approximation.
          ch.refused = true;
          ch.why = "is clocked by a Clock_cell with div=" + std::to_string(iv) + ", which is not implemented (v1 is div=1 only)";
          return ch;
        }
      }
      if (auto inv = lc::sink_driver_hier(n, "invert"); inv.is_const() && !inv.is_known_false()) {
        ch.guard_before_fall = true;
      }
      if (auto en = lc::sink_driver_hier(n, "en"); !en.is_invalid() && !en.is_const()) {
        ch.guards.push_back(en);
        ch.gated = true;
      }
      cur = lc::sink_driver_hier(n, "clk_ref");
      continue;
    }
    if ((op == Ntype_op::And || op == Ntype_op::Or) && depth < 6) {
      // An INLINE clock gate, in either flavour:
      //   `clk & en`   -- ordinary ICG, enable latched while the clock is LOW
      //   `clk | ~en`  -- the active-low flavour, enable latched while it is HIGH
      // Both gate the SAME reference edges; only the enable's SAMPLE POINT
      // differs (see Icg_cone::invert). Recognizing only the And form left the
      // Or form's gated net as an internal "root", so a def holding one looked
      // like it had TWO unrelated clocks and was refused -- measured on minion's
      // prim_write_commit_rst_en (x6 parameterizations), which PROVES in 20ms
      // standalone and came back UNKNOWN in context.
      //
      // Exactly one operand may reach a clock; a constant operand is a WIDTH
      // MASK, not an enable (`x & 1` is how the slang reader spells a 1-bit
      // boolean control).
      hhds::Occurrence_pin              clk_op;
      std::vector<hhds::Occurrence_pin> ens;
      int                               n_clock = 0;
      for (const auto& e : n.inp_edges()) {
        if (e.driver.is_const()) {
          continue;
        }
        if (reaches_clock(e.driver, clocks, depth + 1)) {
          ++n_clock;
          clk_op = e.driver;
        } else {
          ens.push_back(e.driver);
        }
      }
      if (n_clock == 1 && !ens.empty()) {
        ch.live_guards.insert(ch.live_guards.end(), ens.begin(), ens.end());
        for (auto& en : ens) {
          // THE L1 ERROR. A real ICG latches its enable on the opposite phase
          // purely to suppress glitches: the latch closes exactly when the
          // consumer samples, so the consumer sees the value the latch was
          // passing THROUGH. Using the latch's held Q instead reads the PREVIOUS
          // period's enable -- a persistent full-cycle error, and invisible to a
          // symmetric check. `materialize_clock_cells` avoids it by cloning the
          // comb enable cone; an INLINED cell brings the latch itself, so undo
          // it here the same way `pass.single_edge` does.
          const auto er = lc::control_root(en, /*stop_at_clock_cell=*/true);
          if (er.net.is_invalid() || gu::is_graph_input_pin(er.net) || er.net.is_const()) {
            continue;
          }
          auto en_node = er.net.get_master_node();
          if (gu::type_op_of(en_node) != Ntype_op::Latch) {
            continue;
          }
          if (auto arm = lc::latch_transparent_arm(en_node); !arm.is_invalid()) {
            en = arm;
          }
        }
        ch.guards.insert(ch.guards.end(), ens.begin(), ens.end());
        ch.gated = true;
        // `clk | ~en` is the active-low flavour: its enable is sampled before
        // the FALL, not before the rise.
        if (op == Ntype_op::Or) {
          ch.guard_before_fall = true;
        }
        cur = clk_op;
        continue;
      }
      ch.root = cr.net;  // a mask or an ambiguous cone: this node IS the root
      return ch;
    }
    ch.root = cr.net;  // some other derived clock: the caller decides what to do
    return ch;
  }
  return ch;
}

// One canonical name per root net. The CLOCK FOREST is consulted first: it maps
// a def's clock PORT (and its implicit clock, port "") to the root the design's
// instance bindings prove it carries, so `clk_i` in one def and `clock` in
// another are one root rather than two. Without it — an unmapped def, or no
// forest at all — the port's own name stands, which is the pre-propagation
// behaviour. An internal root falls back to a per-graph id; a design whose clock
// is internal logic is refused anyway.
//
// `owner` is the def the ENDPOINT lives in, needed for the implicit clock: a
// `reg x = 0` has no cone at all, so the only thing that identifies its clock is
// which module it is in.
std::string root_key(const hhds::Occurrence_pin& root, const hhds::Occurrence_node* owner, const Clock_forest* forest) {
  if (root.is_invalid()) {
    if (forest != nullptr && owner != nullptr) {
      if (auto* r = forest->find(owner->get_graph()->get_name(), "")) {
        return *r;  // the module's own clock, resolved top-down
      }
    }
    return "\x01implicit";
  }
  if (gu::is_graph_input_pin(root)) {
    std::string nm{gu::pin_name_of(root)};
    if (forest != nullptr) {
      if (auto* r = forest->find(root.get_graph()->get_name(), nm)) {
        return *r;
      }
    }
    return nm;
  }
  return "\x02" + std::to_string(static_cast<uint64_t>(root.get_class_index().value));
}

}  // namespace

std::string Phase_plan::signature() const {
  return "phase1:" + root_clock + ":" + std::to_string(n_flop_rise) + "/" + std::to_string(n_flop_fall) + "/"
         + std::to_string(n_latch_low) + "/" + std::to_string(n_latch_high) + "/" + std::to_string(n_latch_data) + "/"
         + std::to_string(n_mem_rise) + "/" + std::to_string(n_mem_fall) + "/" + std::to_string(n_guards);
}

std::string Phase_plan::describe() const {
  std::string s  = "root=" + (root_clock.empty() ? std::string("<implicit>") : root_clock);
  s             += ", flops rise/fall=" + std::to_string(n_flop_rise) + "/" + std::to_string(n_flop_fall);
  s             += ", clock latches low/high=" + std::to_string(n_latch_low) + "/" + std::to_string(n_latch_high);
  s             += ", data latches=" + std::to_string(n_latch_data);
  s             += ", transparent latches=" + std::to_string(n_latch_transparent);
  s             += ", mem rise/fall=" + std::to_string(n_mem_rise) + "/" + std::to_string(n_mem_fall);
  s             += ", sampled clock guards=" + std::to_string(n_guards);
  return s;
}

Phase_plan plan_phases(hhds::Graph* g, const absl::flat_hash_map<std::string, bool>* collapse_defs,
                       const Clock_forest* clock_forest) {
  Phase_plan plan;
  if (g == nullptr) {
    return plan;
  }
  // Match the encoder's ambient opacity EXACTLY (encode.cpp builds the same set
  // the same way, from a hierarchical fast_hier scan so a collapse def reached
  // only below a flattened parent is still found).
  // FAST REJECT. plan_phases materializes the flattened instance tree
  // (forward_hier), and prove_equal is re-entered per def, per engine and on
  // every tier-2 retry -- so on a large hierarchy that walk is the analysis's
  // whole cost, paid over and over for designs that have nothing to schedule.
  // fast_hier is O(depth) and needs no topological order: if the tree holds no
  // Latch, no negedge state and no Clock_cell anywhere, there is nothing this
  // analysis could say. It deliberately IGNORES the opaque scope (fast_hier
  // cannot honor one), which only ever makes it fall through to the full walk.
  {
    bool interesting = false;
    for (auto n : g->grouped_hierarchy().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Latch || op == Ntype_op::Clock_cell) {
        interesting = true;
        break;
      }
      if ((op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Memory) && posclk_is_false(n)) {
        interesting = true;
        break;
      }
    }
    if (!interesting) {
      if (std::getenv("LEC_PHASE_PLAN") != nullptr) {
        std::fprintf(stderr,
                     "[LEC_PLAN] %-46s FAST-REJECT (no latch / negedge / Clock_cell anywhere)\n",
                     std::string{g->get_name()}.c_str());
      }
      return plan;  // ok, !multi, !needs_plan -- the legacy encoding is exact
    }
  }
  ankerl::unordered_dense::set<hhds::Gid> opaque_subs;
  if (collapse_defs != nullptr) {
    for (auto sn : g->grouped_hierarchy().nodes()) {
      if (gu::type_op_of(sn) != Ntype_op::Sub) {
        continue;
      }
      auto sio = sn.get_subnode_io();
      if (sio != nullptr && collapse_defs->count(std::string(sio->get_name())) > 0) {
        opaque_subs.insert(sn.get_subnode_gid());
      }
    }
  }
  const ankerl::unordered_dense::set<hhds::Gid>* opaque = opaque_subs.empty() ? nullptr : &opaque_subs;
  lc::Design_clocks                              clocks(g, /*hier=*/true, opaque);

  absl::flat_hash_set<std::string> roots;       // distinct root nets any endpoint commits on
  absl::flat_hash_set<std::string> guard_keys;  // distinct sampled guards
  bool                             implicit_root     = false;
  std::string                      implicit_root_key = "\x01implicit";
  std::string                      derived_root;  // an endpoint whose clock is not a clock INPUT

  auto refuse = [&](const std::string& msg) {
    if (plan.ok) {
      plan.ok    = false;
      plan.error = msg;
    }
  };

  for (auto node : g->occurrences(opaque).nodes(hhds::Node_order::forward)) {
    const auto op = gu::type_op_of(node);
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch && op != Ntype_op::Memory) {
      continue;
    }
    if (!node.has_out_edges()) {
      continue;  // unobservable: the encoder skips it too
    }
    Phase_endpoint e;
    const bool     is_latch = op == Ntype_op::Latch;
    // A latch's window is controlled by its ENABLE (its gate IS its enable);
    // everything else by `clock_pin`.
    const auto     ctrl     = lc::sink_driver_hier(node, is_latch ? "enable" : "clock_pin");
    // An ALWAYS-OPEN latch (no enable pin -- tolg wires none when every path
    // writes the reg -- or a constant-true one) stores nothing. Classify it
    // BEFORE any clock question is asked: it has no window, so it has no closing
    // edge and no phase, and resolving its constant control cone would land on a
    // "derived clock" and refuse what is really a combinational buffer.
    if (is_latch && (ctrl.is_invalid() || (ctrl.is_const() && !ctrl.is_known_false()))) {
      e.transparent = true;
      e.phase       = Phase::Rise;
      ++plan.n_latch_transparent;
      plan.ep[box_node_key(node)] = std::move(e);
      continue;
    }
    Clock_chain ch = resolve_chain(ctrl, clocks);
    if (ch.refused) {
      refuse("state element `" + gu::debug_name(node) + "` " + ch.why);
      continue;
    }
    const bool implicit = ctrl.is_invalid() || ch.root.is_invalid();
    if (implicit && !is_latch) {
      // `reg x = 0`: the module's own clock -- a real commit class, not an
      // unresolvable cone. The forest names it (top-down, from whichever root
      // reaches this module); without one it stays the synthetic "\x01implicit".
      implicit_root_key = root_key({}, &node, clock_forest);
      implicit_root     = true;
    }

    bool clock_role = true;
    if (is_latch) {
      // Clock-role vs data-role is the whole classification. Only a latch whose
      // window is controlled by the root CLOCK gets close-before-edge
      // scheduling; a data-gated latch keeps Pyrope's ordinary tick semantics
      // and joins the rise batch (this is the live latch_mixed_flop behavior).
      clock_role = !implicit && (clocks.is_clock(ch.root) || ch.gated);
    }

    // `rising` = the edge of the ROOT net this endpoint commits on.
    //   flop  : posclk (negedge => fall), flipped by every cone inversion
    //   latch : transparent while the gate is asserted, so it COMMITS when the
    //           gate deasserts -- an active-HIGH window closes on the FALL.
    bool rising = is_latch ? ch.inverted : (!posclk_is_false(node) != ch.inverted);
    if (is_latch && posclk_is_false(node)) {
      rising = !rising;  // active-LOW enable polarity flips the closing edge
    }

    if (op == Ntype_op::Memory) {
      e.phase                                       = rising ? Phase::Rise : Phase::Fall;
      (rising ? plan.n_mem_rise : plan.n_mem_fall) += 1;
    } else if (is_latch && clock_role) {
      // Transparent-LOW closes at the RISE, so it must commit in the microstep
      // IMMEDIATELY BEFORE the rise batch; transparent-HIGH closes at the FALL.
      e.phase                                          = rising ? Phase::Close_low : Phase::Close_high;
      e.clock_role_latch                               = true;
      (rising ? plan.n_latch_low : plan.n_latch_high) += 1;
    } else if (is_latch) {
      e.phase = Phase::Rise;
      ++plan.n_latch_data;
    } else {
      e.phase                                         = rising ? Phase::Rise : Phase::Fall;
      (rising ? plan.n_flop_rise : plan.n_flop_fall) += 1;
    }

    // A recognized gate contributes a guard sampled on the INACTIVE PHASE
    // IMMEDIATELY BEFORE THE CELL'S OWN ACTIVE EDGE -- parity of the CELL, not
    // of the consumer. Pinning it to the rise unconditionally reads an inverted
    // cell's guard half a period early, which is a wrong verdict rather than an
    // UNKNOWN (`phase_clock_gate_invert` is exactly that fixture).
    if (ch.gated && !ch.guards.empty()) {
      e.guard_sample     = ch.guard_before_fall ? Phase::Close_high : Phase::Close_low;
      e.live_guard       = is_latch && clock_role && !ch.live_guards.empty();
      const auto& guards = e.live_guard ? ch.live_guards : ch.guards;
      std::string digest = root_key(ch.root, &node, clock_forest) + (ch.guard_before_fall ? "-" : "+");
      for (const auto& gp : guards) {
        digest += "&" + cone_digest(gp);
      }
      e.guard_key = "\x01" + std::string(e.live_guard ? "latchguard:" : "ckguard:") + digest;
      if (!e.live_guard && guard_keys.insert(e.guard_key).second) {
        (ch.guard_before_fall ? plan.n_guard_high : plan.n_guard_low) += 1;
      }
      plan.guard_cones[e.guard_key] = guards;
    }

    // Only a CLOCK-role endpoint contributes a clock root. A DATA-gated latch
    // commits on its ENABLE, which is ordinary logic -- counting that net as a
    // "clock root" made every def holding one look multi-clock and refused it by
    // name (measured on minion's prim_write_commit_rst_en x6, which holds one
    // clock-role and one data-role latch and PROVES standalone in 20ms).
    const bool clock_root_endpoint = !is_latch || clock_role;
    if (!implicit && clock_root_endpoint) {
      roots.insert(root_key(ch.root, &node, clock_forest));
      // v1 orders ONE ROOT CLOCK, and a root is a clock INPUT. Anything else --
      // a mux-selected clock, a divider built from logic, an unrecognized gate
      // whose cell materialization did not fire -- has no defined edge sequence
      // to schedule against, and scheduling it anyway would silently declare a
      // gated clock equal to an ungated one. Named refusal (only when the
      // schedule is actually required; a plain posedge design keeps the
      // encoder's existing detected-edge path either way).
      if (!gu::is_graph_input_pin(ch.root)) {
        derived_root = gu::debug_name(node);
      }
    }
    if (op == Ntype_op::Fflop && e.phase != Phase::Rise) {
      refuse("fluid flop `" + gu::debug_name(node) + "` is not on the rise batch; fluid timing is not scheduled");
    }
    plan.ep[box_node_key(node)] = std::move(e);
  }

  plan.n_guards = static_cast<int>(guard_keys.size());
  if (implicit_root) {
    roots.insert(implicit_root_key);
  }
  plan.n_roots = static_cast<int>(roots.size());
  // WHEN FOUR MICROSTEPS ARE REQUIRED: some endpoint does not commit at the root
  // RISE. That is a CLOCK-ROLE latch (it closes half a period away from the edge
  // batch that reads it) or a negedge flop / memory port.
  //
  // Deliberately NOT triggered by: a DATA-gated latch (ordinary tick semantics,
  // it joins the rise batch and is exactly a flop-with-enable at P=1), an
  // always-transparent latch (a wire), or a recognized clock GATE (M9 already
  // encodes a Clock_cell exactly at P=1 -- "commit iff the enable held at this
  // edge"). Those need the PLAN, not the microsteps; see needs_plan(). Promoting
  // them to four encodes per period would quadruple the cost of the whole minion
  // corpus for no verdict it does not already get -- and it measurably COSTS
  // verdicts, because the single-step inductive engine declines a multi design
  // and the bounded-BMC fallback is suppressed under a speculative state pair.
  plan.multi   = plan.n_flop_fall > 0 || plan.n_latch_low > 0 || plan.n_latch_high > 0 || plan.n_mem_fall > 0;
  if (!roots.empty()) {
    std::vector<std::string> sorted(roots.begin(), roots.end());
    std::sort(sorted.begin(), sorted.end());
    plan.root_clock = sorted.front() == "\x01implicit" ? "" : sorted.front();
    if (sorted.size() > 1 && plan.multi) {
      // v1 imposes a TOTAL ORDER for ONE root clock. Two unrelated roots have no
      // order to impose, and inventing one is a wrong verdict, so fail closed
      // naming them. (A flop-only design with unrelated roots never gets here:
      // `multi` stays false and the legacy detected-edge encoding still runs.)
      std::string names;
      for (const auto& r : sorted) {
        names += (names.empty() ? "" : ", ") + (r == "\x01implicit" ? std::string("<implicit>") : r);
      }
      refuse("the phase schedule needs a total order between " + std::to_string(sorted.size()) + " unrelated clock roots {" + names
             + "}; v1 orders ONE root clock");
    }
  }
  if (plan.multi && !derived_root.empty()) {
    refuse("state element `" + derived_root
           + "` has a derived clock the phase schedule cannot model (not a clock input, and not a recognized "
             "Clock_cell / `<clock> & <enables>` gate)");
  }
  // LEC_PHASE_LOG-style diagnostic, emitted LAST: `multi`, `n_roots` and the
  // refusals are all decided above, and printing before they are set reports a
  // plan that does not exist (it cost an hour of chasing a phantom `multi=0`).
  if (std::getenv("LEC_PHASE_PLAN") != nullptr) {
    std::fprintf(stderr,
                 "[LEC_PLAN] %-46s %s roots=%d multi=%d needs_plan=%d multi_root=%d ok=%d %s\n",
                 std::string{g->get_name()}.c_str(),
                 plan.describe().c_str(),
                 plan.n_roots,
                 plan.multi ? 1 : 0,
                 plan.needs_plan() ? 1 : 0,
                 plan.multi_root() ? 1 : 0,
                 plan.ok ? 1 : 0,
                 plan.error.c_str());
  }
  return plan;
}

}  // namespace livehd::lec
