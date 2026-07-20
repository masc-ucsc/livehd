// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "latch_contract.hpp"

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"

namespace livehd::latch_contract {

namespace gu = livehd::graph_util;

namespace {

// ---------------------------------------------------------------------------
// Phase resolution: walk a control cone (a clock_pin, or a latch's enable) back
// to the ROOT net that drives it, counting inversions on the way.
//
// This is what lets `if clk { … }` and `if !clk { … }` be recognized as the
// SAME net at OPPOSITE parity — i.e. as a master/slave pair rather than two
// unrelated latches. Without it, tolg's per-latch mux/eq nodes make every
// enable look like a distinct signal and rule C below could never fire (or,
// worse, would fire on master/slave).
//
// The shapes traversed are exactly the ones tolg and the readers emit for a
// boolean control:
//   Mux(s, 0, 1)   the `cond ? 1 : 0` enable shape  -> follow the SELECTOR
//   EQ(x, 0)       boolean negation                 -> follow x, flip parity
//   EQ(x, 1)       boolean identity                 -> follow x
//   Not(x)                                          -> follow x, flip parity
//   Get_mask/Sext  width/sign adjust (identity)     -> follow the value
// Anything else is treated as the root. A cone we cannot decode simply resolves
// to itself, which makes two such cones compare UNEQUAL — the conservative
// direction for rule C (it can only fail to fire, never fire spuriously).
struct Phase {
  hhds::Pin_class net;
  bool            inverted = false;
};

bool const_is(const hhds::Pin_class& p, int64_t want) {
  if (p.is_invalid() || !gu::is_const_pin(p)) {
    return false;
  }
  auto c = gu::hydrate_const(p);
  return c.is_just_i64() && c.to_just_i64() == want;
}

Phase resolve_phase(hhds::Pin_class p) {
  Phase ph;
  ph.net = p;
  for (int hops = 0; hops < 64 && !ph.net.is_invalid(); ++hops) {
    if (gu::is_graph_input_pin(ph.net) || gu::is_const_pin(ph.net)) {
      break;
    }
    auto n  = ph.net.get_master_node();
    auto op = gu::type_op_of(n);

    if (op == Ntype_op::Mux) {
      // `cond ? 1 : 0` (or its negation `cond ? 0 : 1`): the phase is the
      // selector's, inverted when the arms are swapped. Any other mux is data,
      // not a control shape -> stop.
      hhds::Pin_class sel, arm0, arm1;
      for (const auto& e : n.inp_edges()) {
        const auto pid = e.sink.get_port_id();
        if (pid == 0) {
          sel = e.driver;
        } else if (pid == 1) {
          arm0 = e.driver;
        } else if (pid == 2) {
          arm1 = e.driver;
        }
      }
      if (sel.is_invalid() || arm0.is_invalid() || arm1.is_invalid()) {
        break;
      }
      if (const_is(arm0, 0) && !const_is(arm1, 0)) {
        ph.net = sel;  // cond ? nonzero : 0  -> same parity as cond
        continue;
      }
      if (!const_is(arm0, 0) && const_is(arm1, 0)) {
        ph.net      = sel;  // cond ? 0 : nonzero -> inverted
        ph.inverted = !ph.inverted;
        continue;
      }
      break;  // a data mux: this is the root as far as phase goes
    }

    if (op == Ntype_op::EQ) {
      // Boolean compare against a constant: `x == 0` negates, `x == 1` is the
      // identity. tolg emits `(x == 0) == 0` for a plain `if x`, so this arm is
      // walked twice and the parity comes out even — which is why an explicit
      // `if !clk` (one extra negation) reliably lands on the opposite parity.
      hhds::Pin_class a, b;
      int             cnt = 0;
      for (const auto& e : n.inp_edges()) {
        (cnt++ == 0 ? a : b) = e.driver;
      }
      if (cnt != 2) {
        break;
      }
      hhds::Pin_class val;
      hhds::Pin_class cst;
      if (gu::is_const_pin(b)) {
        val = a;
        cst = b;
      } else if (gu::is_const_pin(a)) {
        val = b;
        cst = a;
      } else {
        break;  // comparing two real signals: a data compare, not a phase shape
      }
      if (const_is(cst, 0)) {
        ph.inverted = !ph.inverted;
        ph.net      = val;
        continue;
      }
      if (const_is(cst, 1)) {
        ph.net = val;
        continue;
      }
      break;
    }

    if (op == Ntype_op::Not) {
      hhds::Pin_class a;
      for (const auto& e : n.inp_edges()) {
        a = e.driver;
        break;
      }
      if (a.is_invalid()) {
        break;
      }
      ph.inverted = !ph.inverted;
      ph.net      = a;
      continue;
    }

    if (op == Ntype_op::Get_mask || op == Ntype_op::Sext) {
      // Width / sign adjust wrappers tolg puts on a typed port read: identity
      // for phase purposes. Follow the VALUE operand (port 'a'/first edge).
      hhds::Pin_class a;
      for (const auto& e : n.inp_edges()) {
        if (a.is_invalid() || e.sink.get_port_id() < a.get_port_id()) {
          a = e.driver;
        }
      }
      if (a.is_invalid()) {
        break;
      }
      ph.net = a;
      continue;
    }

    break;  // anything else: this node IS the root
  }
  return ph;
}

// The enable's EFFECTIVE parity, folding in the polarity pin. `posclk` on a
// Latch is the ENABLE POLARITY (user ruling): known-false = active LOW, which
// flips the transparent level and therefore the phase.
Phase latch_phase(const hhds::Node_class& n) {
  Phase ph = resolve_phase(gu::get_driver_of_sink_name(n, "enable"));
  auto  pc = gu::get_driver_of_sink_name(n, "posclk");
  if (!pc.is_invalid() && gu::is_const_pin(pc) && gu::hydrate_const(pc).is_known_false()) {
    ph.inverted = !ph.inverted;
  }
  return ph;
}

// ---------------------------------------------------------------------------
// Backward combinational reach: which STATE elements' outputs feed `start`,
// walking only through combinational nodes.
//
// `hold_owner` (when valid) is the latch whose own hold mux must be exempted:
// tolg bakes `din = cond ? d : q` into EVERY Pyrope/slang latch, so every such
// latch has a STRUCTURAL q-to-own-D path that is not a real one. The exemption
// is narrow on purpose — only an operand that is DIRECTLY that latch's q is
// skipped, so `q + 1` (where q reaches din through a Sum) is still caught.
void comb_reach(const hhds::Pin_class& start, const hhds::Node_class& hold_owner,
                absl::flat_hash_set<hhds::Class_index>& hit_state) {
  if (start.is_invalid()) {
    return;
  }
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  const bool                             has_owner = !hold_owner.is_invalid();
  const auto owner_q = has_owner ? hold_owner.get_driver_pin(0) : hhds::Pin_class{};

  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || gu::is_const_pin(p) || gu::is_graph_input_pin(p)) {
      continue;
    }
    if (!seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n  = p.get_master_node();
    auto op = gu::type_op_of(n);
    if (gu::is_type_register(n)) {
      hit_state.insert(n.get_class_index());
      continue;  // STOP at state: a path THROUGH a register is not combinational
    }
    if (op == Ntype_op::Sub) {
      continue;  // opaque instance: not traversed (conservative, may under-report)
    }
    for (const auto& e : n.inp_edges()) {
      // The hold-mux exemption, applied narrowly (see above).
      if (has_owner && op == Ntype_op::Mux && !owner_q.is_invalid() && !e.driver.is_invalid()
          && e.driver.get_class_index() == owner_q.get_class_index()) {
        continue;
      }
      work.push_back(e.driver);
    }
  }
}

std::string latch_label(const hhds::Node_class& n) { return gu::debug_name(n); }

}  // namespace

std::optional<Commit_class> commit_class_of(const hhds::Node_class& n) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
    Commit_class cc;
    cc.net    = resolve_phase(gu::get_driver_of_sink_name(n, "clock_pin")).net;
    cc.rising = true;
    if (auto pc = gu::get_driver_of_sink_name(n, "posclk");
        !pc.is_invalid() && gu::is_const_pin(pc) && gu::hydrate_const(pc).is_known_false()) {
      cc.rising = false;  // negedge flop
    }
    if (cc.net.is_invalid()) {
      return std::nullopt;
    }
    return cc;
  }
  if (op == Ntype_op::Latch) {
    const Phase ph = latch_phase(n);
    if (ph.net.is_invalid()) {
      return std::nullopt;
    }
    Commit_class cc;
    cc.net = ph.net;
    // Transparent while the enable is asserted, so it COMMITS when the enable
    // deasserts: an active-HIGH enable commits on the net's FALL, an active-LOW
    // (inverted) one on its RISE.
    cc.rising = ph.inverted;
    return cc;
  }
  return std::nullopt;
}

bool check(hhds::Graph* g) {
  if (g == nullptr) {
    return true;
  }
  std::vector<hhds::Node_class> latches;
  for (auto n : g->fast_class()) {
    if (gu::type_op_of(n) == Ntype_op::Latch) {
      latches.push_back(n);
    }
  }
  if (latches.empty()) {
    return true;  // the overwhelmingly common case: pay nothing
  }

  // Rule C compares COMMIT CLASSES through the public API — the same call M4
  // (LEC) and M5 (sim) use — so there is exactly one notion of "when does this
  // element commit" in the tree rather than a checker-private copy that can
  // drift from what the back ends believe.
  absl::flat_hash_map<hhds::Class_index, Commit_class> cc_of;
  for (const auto& l : latches) {
    if (auto cc = commit_class_of(l)) {
      cc_of.emplace(l.get_class_index(), *cc);
    }
  }

  const std::string gname{g->get_name()};
  bool              ok = true;

  auto reject = [&](std::string_view rule, const std::string& msg, std::string_view hint) {
    ok = false;
    livehd::diag::err("pass.latch", "latch-contract", "unsupported")
        .msg("module `{}` violates the latch contract ({}): {}", gname, rule, msg)
        .hint(hint)
        .emit();
  };

  for (const auto& l : latches) {
    const auto self_ix = l.get_class_index();

    // ---- rule A: the ENABLE may not depend on this latch's own Q ------------
    // A self-timed gate: the latch closes itself. There is no "closing edge" to
    // commit at that is independent of the value being committed, so the
    // commit-class model has nothing to key on.
    {
      absl::flat_hash_set<hhds::Class_index> hit;
      comb_reach(gu::get_driver_of_sink_name(l, "enable"), hhds::Node_class{}, hit);
      if (hit.contains(self_ix)) {
        reject("rule A, self-timed gate",
               "latch `" + latch_label(l) + "` derives its own enable from its own Q",
               "a latch whose gate closes itself has no commit edge independent of the value it commits, so the "
               "flop-with-enable model cannot represent it; drive the enable from a signal that does not depend on "
               "this latch");
        continue;  // its D-cone findings would be noise on top of this
      }
    }

    // ---- rule B: D may not depend on this latch's own Q --------------------
    // Transparent self-update — a real latch would oscillate/settle inside the
    // window, which is exactly the timing the abstraction discards. The hold
    // mux tolg synthesizes is exempted (see comb_reach).
    {
      absl::flat_hash_set<hhds::Class_index> hit;
      comb_reach(gu::get_driver_of_sink_name(l, "din"), l, hit);
      if (hit.contains(self_ix)) {
        reject("rule B, transparent self-update",
               "latch `" + latch_label(l) + "` has a combinational path from its Q back to its own D",
               "while the window is open a real latch would re-evaluate this path continuously; the "
               "commit-at-closing-edge model evaluates it once, so the two disagree. Break the loop with a second "
               "state element (a master/slave pair on OPPOSITE phases is fine)");
        continue;
      }

      // ---- rule C: no comb path between SIMULTANEOUSLY transparent latches --
      // This is where time borrowing lives: if two latches are open at the same
      // time, logic can straddle the boundary between them and the "cycle" a
      // value lands in stops being well defined. A master/slave pair sits on
      // OPPOSITE parities of the same net and is ACCEPTED — that is the whole
      // reason phases are resolved to a root net instead of compared per-node.
      for (const auto& other : latches) {
        const auto other_ix = other.get_class_index();
        if (other_ix == self_ix || !hit.contains(other_ix)) {
          continue;
        }
        // Same net AND same committing edge => their windows are open together.
        // An UNRESOLVED cone (absent from the map) is never claimed to be
        // simultaneous: the check can fail to fire, never fire spuriously.
        auto a_it = cc_of.find(self_ix);
        auto b_it = cc_of.find(other_ix);
        if (a_it == cc_of.end() || b_it == cc_of.end()) {
          continue;
        }
        if (a_it->second.net.get_class_index() != b_it->second.net.get_class_index()
            || a_it->second.rising != b_it->second.rising) {
          continue;  // opposite phase (master/slave): the point of the whole walk
        }
        reject("rule C, simultaneously-transparent pair",
               "latches `" + latch_label(other) + "` and `" + latch_label(l)
                   + "` are transparent at the SAME time and there is a combinational path between them",
               "both windows are open together, so logic straddles the boundary and the cycle a value lands in is "
               "not well defined (this is time borrowing, which is out of scope). Put the two latches on OPPOSITE "
               "phases of the gate, as a master/slave pair does");
        break;  // one report per latch is enough to act on
      }
    }
  }

  return ok;
}

}  // namespace livehd::latch_contract
