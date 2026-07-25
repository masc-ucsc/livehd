// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "latch_contract.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "inline_sub.hpp"

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

    if (op == Ntype_op::And) {
      // `x & 1` — the 1-bit WIDTH MASK the slang reader puts on a boolean
      // control (`always @(posedge ~clk)` arrives as
      // `And(Not(Get_mask(clk)), 1)`). Identity for phase purposes, so follow
      // the value and leave the parity alone. Deliberately narrow: an `And`
      // whose operands are BOTH real signals is an ICG cone (`clk & en`), and
      // treating that as an identity would silently drop the enable.
      hhds::Pin_class val;
      bool            masked = false;
      int             cnt    = 0;
      for (const auto& e : n.inp_edges()) {
        ++cnt;
        if (const_is(e.driver, 1) || const_is(e.driver, -1)) {
          masked = true;
        } else {
          val = e.driver;
        }
      }
      if (cnt != 2 || !masked || val.is_invalid()) {
        break;  // an ICG cone or something else: this node IS the root
      }
      ph.net = val;
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

// `posclk` known-false on this node (negedge flop / active-LOW latch enable).
bool posclk_is_false(const hhds::Node_class& n) {
  auto pc = gu::get_driver_of_sink_name(n, "posclk");
  return !pc.is_invalid() && gu::is_const_pin(pc) && gu::hydrate_const(pc).is_known_false();
}

}  // namespace

// ---------------------------------------------------------------------------
// Design_clocks

bool Design_clocks::name_looks_like_clock(std::string_view name) {
  // Token-wise on '_' boundaries, case-insensitively. `clk`, `clock`,
  // `core_clk`, `clk2`, `CLK` match; `clock_en`, `gclk_gate`, `clocked` do not.
  //
  // Deliberately narrow in BOTH directions, because both mistakes are silent
  // full-cycle errors under M8's slot table: calling a data enable a clock
  // re-slots real state, and calling a gate clock "data" drops a
  // transparent-LOW latch (commits on the RISE, slot 0) into the LAST slot.
  // Hence the blocklist: a name that also carries an enable/gate word is a
  // gated or qualified signal, not the clock itself.
  static constexpr std::string_view kNotClock[]
      = {"en", "enable", "gate", "gated", "sel", "valid", "vld", "req", "ack", "data", "d", "q"};

  std::string lower;
  lower.reserve(name.size());
  for (char c : name) {
    lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }

  auto tok_is_clock = [](std::string_view t) {
    for (std::string_view base : {std::string_view{"clk"}, std::string_view{"clock"}}) {
      if (t.size() < base.size() || t.substr(0, base.size()) != base) {
        continue;
      }
      const auto rest = t.substr(base.size());  // `clk2`, `clock1` — a numbered domain
      if (rest.empty()) {
        return true;
      }
      if (std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return true;
      }
    }
    return false;
  };

  bool   saw_clock = false;
  size_t pos       = 0;
  while (pos <= lower.size()) {
    const size_t end = std::min(lower.find('_', pos), lower.size());
    const auto   tok = std::string_view{lower}.substr(pos, end - pos);
    for (auto bad : kNotClock) {
      if (tok == bad) {
        return false;
      }
    }
    saw_clock = saw_clock || tok_is_clock(tok);
    pos       = end + 1;
  }
  return saw_clock;
}

Design_clocks::Design_clocks(hhds::Graph* g) {
  if (g == nullptr) {
    return;
  }
  for (auto n : g->fast_class()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop) {
      continue;
    }
    auto clk = gu::get_driver_of_sink_name(n, "clock_pin");
    if (clk.is_invalid()) {
      implicit_clock_ = true;  // `reg x = 0`: the module's own clock
      continue;
    }
    const auto root = resolve_phase(clk).net;
    if (root.is_invalid()) {
      continue;
    }
    roots_.insert(root.get_class_index());
    if (gu::is_graph_input_pin(root)) {
      input_names_.insert(std::string(gu::pin_name_of(root)));
    }
  }
}

bool Design_clocks::is_clock(const hhds::Pin_class& root) const {
  if (root.is_invalid()) {
    return false;
  }
  if (roots_.contains(root.get_class_index())) {
    return true;
  }
  // Name fallback, ONLY for a graph input. An internal node's name is a
  // compiler artifact, so matching it would be pattern-matching on tolg's
  // temporaries. See the ICG trap in encode.cpp: this same "is it a clock?"
  // question, answered by "is it an input?", classified the ENABLE of `clk & en`
  // as a clock and silently produced no gating at all.
  if (!gu::is_graph_input_pin(root)) {
    return false;
  }
  return name_looks_like_clock(gu::pin_name_of(root));
}

// ---------------------------------------------------------------------------

std::string Commit_class::net_key() const {
  if (implicit_clock) {
    return "\x01implicit";  // one canonical name for the module clock, on BOTH miter sides
  }
  if (net.is_invalid()) {
    return "\x01unresolved";
  }
  if (gu::is_graph_input_pin(net)) {
    return std::string(gu::pin_name_of(net));
  }
  // An internal root: identify it by the node it drives plus the port. Stable
  // within a graph, which is all a slot assignment needs (the miter-wide rule
  // is that BOTH sides normalize, not that internal keys match across sides).
  return "\x02" + std::to_string(static_cast<uint64_t>(net.get_class_index().value));
}

hhds::Pin_class latch_transparent_arm(const hhds::Node_class& n) {
  if (gu::type_op_of(n) != Ntype_op::Latch) {
    return {};
  }
  auto q   = n.get_driver_pin(0);
  auto din = gu::get_driver_of_sink_name(n, "din");
  if (q.is_invalid() || din.is_invalid() || gu::is_const_pin(din) || gu::is_graph_input_pin(din)) {
    return {};
  }
  auto mux = din.get_master_node();
  if (gu::type_op_of(mux) != Ntype_op::Mux) {
    return {};
  }
  for (const auto& e : mux.inp_edges()) {
    if (e.sink.get_port_id() == 0) {
      continue;  // the selector is the gate, not an arm
    }
    if (!e.driver.is_invalid() && e.driver.get_class_index() != q.get_class_index()) {
      return e.driver;
    }
  }
  return {};
}

int inline_clock_gate_cells(hhds::Graph* g, std::string_view from_pass) {
  if (g == nullptr) {
    return 0;
  }
  // 1. Collect the pins that drive some state element's clock_pin.
  absl::flat_hash_set<hhds::Class_index> clock_drivers;
  for (auto n : g->fast_class()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Memory && op != Ntype_op::Latch) {
      continue;
    }
    auto d = gu::get_driver_of_sink_name(n, "clock_pin");
    // Keyed on the driving NODE, not the pin: the same pin reached through an
    // edge and through Node::out_pins() does not compare equal, so a pin-keyed
    // set silently matched nothing. Hop the identity wrappers a typed port read
    // picks up, so a clock that reaches the flop through a width mask still
    // points back at the cell that produced it.
    for (int hops = 0; hops < 8 && !d.is_invalid(); ++hops) {
      if (gu::is_graph_input_pin(d) || gu::is_const_pin(d)) {
        break;
      }
      auto dn = d.get_master_node();
      clock_drivers.insert(dn.get_class_index());
      if (gu::type_op_of(dn) != Ntype_op::Get_mask && gu::type_op_of(dn) != Ntype_op::Sext) {
        break;
      }
      hhds::Pin_class a;
      for (const auto& e : dn.inp_edges()) {
        if (a.is_invalid() || e.sink.get_port_id() < a.get_port_id()) {
          a = e.driver;
        }
      }
      d = a;
    }
  }
  if (clock_drivers.empty()) {
    return 0;
  }
  // 2. An instance is a clock-gate cell when one of its outputs is in that set
  //    AND its def holds a Latch. Both halves matter: "drives a clock" alone
  //    would inline an ordinary clock buffer or PLL wrapper for no reason, and
  //    "contains a latch" alone would inline half the design.
  std::vector<hhds::Node_class> cells;
  for (auto n : g->fast_class()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto def = n.get_subnode_graph();
    if (!def) {
      continue;  // body-less blackbox: nothing to inline
    }
    if (!clock_drivers.contains(n.get_class_index())) {
      continue;
    }
    bool has_latch = false;
    for (auto dn : def->fast_class()) {
      if (gu::type_op_of(dn) == Ntype_op::Latch) {
        has_latch = true;
        break;
      }
    }
    if (has_latch) {
      cells.push_back(n);
    }
  }
  int done = 0;
  for (const auto& c : cells) {  // never mutate while iterating fast_class
    if (gu::inline_sub_instance(g, c, from_pass)) {
      ++done;
    }
  }
  return done;
}

std::optional<Icg_cone> resolve_icg(const hhds::Pin_class& clock_pin, const Design_clocks& clocks) {
  if (clock_pin.is_invalid() || gu::is_const_pin(clock_pin) || gu::is_graph_input_pin(clock_pin)) {
    return std::nullopt;
  }
  // Descend the boolean-SHAPING wrappers first. A flop's clock_pin is usually
  // the `And` itself, but a LATCH's enable arrives wrapped: tolg lowers
  // `if !clk and en { … }` to `enable = Mux(cone, 0, 1)`, so requiring the
  // driver to BE an And silently missed every Pyrope/slang gated latch. Any
  // inversion picked up on the way folds into the gate's edge below.
  const Phase outer = resolve_phase(clock_pin);
  if (outer.net.is_invalid() || gu::is_const_pin(outer.net) || gu::is_graph_input_pin(outer.net)) {
    return std::nullopt;
  }
  auto n = outer.net.get_master_node();
  if (gu::type_op_of(n) != Ntype_op::And) {
    return std::nullopt;
  }
  Icg_cone icg;
  int      n_clock = 0;
  for (const auto& e : n.inp_edges()) {
    if (e.driver.is_invalid()) {
      return std::nullopt;
    }
    // A CONSTANT operand is a WIDTH MASK, not an enable. `x & 1` is how the
    // slang reader spells a 1-bit boolean control, so `posedge ~clk` arrives as
    // `And(Not(Get_mask(clk)), 1)`. Reading that constant as an enable made an
    // INVERTED CLOCK look like a gated one, which silently threw the inversion
    // away and un-fixed the negedge-vs-posedge case.
    if (gu::is_const_pin(e.driver)) {
      continue;
    }
    const auto ph = resolve_phase(e.driver);
    if (!ph.net.is_invalid() && clocks.is_clock(ph.net)) {
      ++n_clock;
      icg.clock = ph.net;
      // `~clk & en` gates the FALLING edge; an inversion on the way down to the
      // And (a latch's `Mux(cone,0,1)` shaping, or an explicit `!`) flips it too.
      icg.clock_inverted = ph.inverted != outer.inverted;
    } else {
      icg.enables.push_back(e.driver);
    }
  }
  // EXACTLY ONE clock operand, and at least one non-constant enable. Zero clock
  // operands means we could not tell which is the clock; more than one means the
  // cone is an AND of clocks, which is not a gate. Both are ambiguous, and
  // guessing here is how the enable gets classified as the clock -- fail closed
  // instead. No enables at all means this was a mask, not a gate: leave it to
  // resolve_phase, which already treats `x & 1` as an identity.
  if (n_clock != 1 || icg.enables.empty()) {
    return std::nullopt;
  }
  return icg;
}

std::optional<Commit_class> commit_class_of(const hhds::Node_class& n, const Design_clocks* clocks) {
  const auto op = gu::type_op_of(n);
  if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch) {
    return std::nullopt;
  }
  std::optional<Design_clocks> owned;
  if (clocks == nullptr) {
    owned.emplace(n.get_graph());
    clocks = &*owned;
  }

  if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
    Commit_class cc;
    auto         clk = gu::get_driver_of_sink_name(n, "clock_pin");
    bool         clk_inverted = false;
    if (clk.is_invalid()) {
      // Implicitly clocked: a real class on the module clock, NOT "unresolvable".
      cc.implicit_clock = true;
      cc.role           = Net_role::Clock;
    } else {
      // An ICG cone (`clk & en`) commits on its CLOCK operand's edge, gated by
      // the enables -- so its commit CLASS is the clock's, and the enables are
      // data that the M8 lowering folds into the flop's `enable`. Without this
      // the And node itself becomes the "net" and the flop looks like a second
      // clock domain, which is what made the canonical ICG refuse.
      if (auto icg = resolve_icg(clk, *clocks)) {
        cc.net    = icg->clock;
        cc.role   = Net_role::Clock;
        cc.rising = !posclk_is_false(n);
        if (icg->clock_inverted) {
          cc.rising = !cc.rising;  // `~clk & en`: gated, and on the FALLING edge
        }
        return cc;
      }
      const Phase ph = resolve_phase(clk);
      cc.net         = ph.net;
      if (cc.net.is_invalid()) {
        return std::nullopt;
      }
      // AN INVERSION IN THE CLOCK CONE IS A NEGEDGE. `always @(posedge ~clk)`
      // and `wire nclk = ~clk; always @(posedge nclk)` are the SAME machine as
      // `always @(negedge clk)`, and resolve_phase already walks the Not — but
      // this branch used to keep only `.net` and take the edge from the
      // `posclk` pin alone, so the inversion was invisible. That made the whole
      // negedge story SPELLING-DEPENDENT: the pin form was slotted correctly
      // while the cone form was reported as "no negedge state, one clock net",
      // skipped, and then (a) falsely PROVEN against a real posedge design and
      // (b) falsely REFUTED against the identical negedge design, because
      // pass.single_edge lowered only one side into a 2-slot time base.
      // The yosys reader hides this (its `proc` folds `~clk` into
      // CLK_POLARITY=0 before we ever see a cone); the slang reader does not.
      clk_inverted = ph.inverted;
      // A flop's clock_pin cone IS a clock by definition of the pin — even a
      // derived one (an ICG output). The role says "this is timing, not data".
      cc.role = Net_role::Clock;
    }
    cc.rising = !posclk_is_false(n);  // known-false => negedge flop
    if (clk_inverted) {
      cc.rising = !cc.rising;  // ...and an inverted clock flips it again
    }
    return cc;
  }

  // A latch gated by `<clock> & <data>` -- minion's prim_phase_pair shape,
  // `always_latch if (!clk && lo_en) q <= d`. Its window is the clock's, so its
  // COMMIT CLASS is the clock's edge; the data operands are an ordinary enable.
  // Without this the whole cone resolves to the And node, the latch classifies
  // as DATA-gated, and the lowering leaves the CLOCK being read as data in the
  // enable (evaluated once per sub-step against a free input) -- and, with no
  // Clock-role element left in the design, the retyped flop comes out with no
  // clock at all (`always @(posedge 'hx)`).
  if (auto icg = resolve_icg(gu::get_driver_of_sink_name(n, "enable"), *clocks)) {
    Commit_class cc;
    cc.net  = icg->clock;
    cc.role = Net_role::Clock;
    // Transparent while the gate is asserted, so it COMMITS when the gate
    // deasserts: gated on `!clk` (inverted) => commits on the clock's RISE.
    cc.rising = icg->clock_inverted;
    if (posclk_is_false(n)) {
      cc.rising = !cc.rising;  // active-LOW enable polarity flips it again
    }
    return cc;
  }

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
  cc.role   = clocks->is_clock(ph.net) ? Net_role::Clock : Net_role::Data;
  return cc;
}

Single_edge_need needs_single_edge(hhds::Graph* g, const Design_clocks* clocks) {
  Single_edge_need need;
  if (g == nullptr) {
    return need;
  }
  std::optional<Design_clocks> owned;
  if (clocks == nullptr) {
    owned.emplace(g);
    clocks = &*owned;
  }

  absl::flat_hash_set<std::string> clock_nets;
  for (auto n : g->fast_class()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Latch) {
      ++need.n_latches;
      continue;
    }
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop) {
      continue;
    }
    // Count a negedge from the COMMIT CLASS, not from the `posclk` pin. The two
    // differ whenever the inversion lives in the clock CONE rather than on the
    // pin (`always @(posedge ~clk)`, which the slang reader keeps as a cone
    // while yosys folds it into CLK_POLARITY=0). Reading the pin alone made the
    // whole trigger spelling-dependent: the same design was normalized or
    // skipped depending on which reader produced it.
    if (resolve_icg(gu::get_driver_of_sink_name(n, "clock_pin"), *clocks)) {
      ++need.n_icg_flops;
    }
    if (auto cc = commit_class_of(n, clocks)) {
      clock_nets.insert(cc->net_key());
      if (!cc->rising) {
        ++need.n_negedge_flops;
      }
    } else if (posclk_is_false(n)) {
      ++need.n_negedge_flops;  // unresolvable cone, but the pin still says negedge
    }
  }
  need.n_clock_nets = static_cast<int>(clock_nets.size());

  std::string why;
  if (need.n_latches > 0) {
    why = std::to_string(need.n_latches) + " latch cell(s)";
  }
  if (need.n_negedge_flops > 0) {
    why += (why.empty() ? "" : ", ") + std::to_string(need.n_negedge_flops) + " negedge flop(s)";
  }
  if (need.n_icg_flops > 0) {
    why += (why.empty() ? "" : ", ") + std::to_string(need.n_icg_flops) + " gated-clock flop(s)";
  }
  if (need.n_clock_nets >= 2) {
    why += (why.empty() ? "" : ", ") + std::to_string(need.n_clock_nets) + " clock nets";
  }
  need.needed = !why.empty();
  need.why    = std::move(why);
  return need;
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
