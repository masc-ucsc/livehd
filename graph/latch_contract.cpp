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
template <typename Pin>
struct Phase_t {
  Pin  net;
  bool inverted = false;
};
using Phase = Phase_t<hhds::Pin_class>;

template <typename Pin>
bool const_is(const Pin& p, int64_t want) {
  if (p.is_invalid() || !gu::is_const_pin(p)) {
    return false;
  }
  auto c = gu::hydrate_const(p);
  return c.is_just_i64() && c.to_just_i64() == want;
}

// `stop_at_clock_cell` keeps the walk on the OUTSIDE of a gate. The default
// (false) canonicalizes a gated net to its reference clock, which is what every
// commit-class consumer wants. The formal phase scheduler needs the opposite:
// it must step a CHAIN of gates one cell at a time so that every cell's enable
// lands in the combined guard (`gate(gate(clk,en0),en1)` -> `en0 & en1`);
// hopping straight to the root would silently drop the inner enable.
template <typename Pin>
Phase_t<Pin> resolve_phase(Pin p, bool stop_at_clock_cell = false) {
  Phase_t<Pin> ph;
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
      Pin sel, arm0, arm1;
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
      Pin a, b;
      int cnt = 0;
      for (const auto& e : n.inp_edges()) {
        (cnt++ == 0 ? a : b) = e.driver;
      }
      if (cnt != 2) {
        break;
      }
      Pin val;
      Pin cst;
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
      Pin a;
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
      Pin  val;
      bool masked = false;
      int  cnt    = 0;
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
      auto a = gu::first_value_driver(n);
      if (a.is_invalid()) {
        break;
      }
      ph.net = a;
      continue;
    }

    if (op == Ntype_op::Clock_cell) {
      if (stop_at_clock_cell) {
        break;  // the cell IS the root for this caller (see the parameter note)
      }
      // 2f-latch M9. A gated clock's ROOT is its REFERENCE clock: the cell is an
      // enable, not a new clock domain, so a gated flop and an ungated one on
      // the same reference share ONE commit class and therefore one slot.
      //
      // Stopping here instead would make the cell's own output a root, and
      // Design_clocks' ctor would then insert it into roots_ -- so `is_clock`
      // would start answering true for gated nets, a second "clock domain"
      // would appear out of nowhere, and two flops that commit on the very same
      // edge would land in different slots. That is the pollution the header
      // warns about for the And case, in a new costume.
      //
      // `invert` is the ACTIVE-LOW GATE FLAVOUR (`clk | ~en_latch`, minion's
      // prim_clk_gate_n), NOT an inverted output. Both flavours transition on
      // exactly the same reference edges -- `clk & en` idles LOW and `clk | ~en`
      // idles HIGH, but neither moves while the enable is deasserted -- so the
      // consumer's edge is UNCHANGED and the parity bit must not flip here.
      // What the flavour does change is WHEN THE ENABLE IS SAMPLED: an ordinary
      // gate latches it while the clock is LOW (pre-rise), the active-low one
      // while it is HIGH (pre-fall). That is a scheduling fact, carried by the
      // phase schedule's guard sample point, not a parity.
      auto ref = gu::get_driver_of_sink_name(ph.net.get_master_node(), "clk_ref");
      if (ref.is_invalid()) {
        break;
      }
      ph.net = ref;
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
  const auto                             owner_q   = has_owner ? hold_owner.get_driver_pin(0) : hhds::Pin_class{};

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

Control_root control_root(hhds::Pin_class p, bool stop_at_clock_cell) {
  const Phase ph = resolve_phase(p, stop_at_clock_cell);
  return Control_root{ph.net, ph.inverted};
}

Occurrence_control_root control_root(hhds::Occurrence_pin p, bool stop_at_clock_cell) {
  const auto ph = resolve_phase(p, stop_at_clock_cell);
  return Occurrence_control_root{ph.net, ph.inverted};
}

hhds::Pin_class sink_driver_hier(const hhds::Node_class& n, std::string_view sink_name) {
  const auto pid = Ntype::get_sink_pid(gu::type_op_of(n), sink_name);
  for (const auto& e : n.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      return e.driver;
    }
  }
  return {};
}

hhds::Occurrence_pin sink_driver_hier(const hhds::Occurrence_node& n, std::string_view sink_name) {
  const auto pid = Ntype::get_sink_pid(gu::type_op_of(n), sink_name);
  for (const auto& e : n.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      return e.driver;
    }
  }
  return {};
}

Design_clocks::Design_clocks(hhds::Graph* g, bool hier, const ankerl::unordered_dense::set<hhds::Gid>* opaque) {
  if (g == nullptr) {
    return;
  }
  auto record = [&](const auto& n, const auto& clk) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop) {
      return;
    }
    if (clk.is_invalid()) {
      implicit_clock_ = true;  // `reg x = 0`: the module's own clock
      return;
    }
    const auto root = resolve_phase(clk).net;
    if (root.is_invalid()) {
      return;
    }
    roots_.insert(root_key(root));
    if (gu::is_graph_input_pin(root)) {
      input_names_.insert(std::string(gu::pin_name_of(root)));
    }
  };
  if (hier) {
    if (opaque != nullptr) {
      auto policy = [&](const hhds::Instance_site& site) {
        return opaque->contains(site.target_gid()) ? hhds::Instance_action::opaque : hhds::Instance_action::descend;
      };
      for (auto n : g->grouped_hierarchy(policy).nodes()) {
        const auto op = gu::type_op_of(n);
        if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
          record(n, sink_driver_hier(n, "clock_pin"));
        }
      }
    } else {
      for (auto n : g->grouped_hierarchy().nodes()) {
        const auto op = gu::type_op_of(n);
        if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
          record(n, sink_driver_hier(n, "clock_pin"));
        }
      }
    }
  } else {
    for (auto n : g->body().nodes()) {
      const auto op = gu::type_op_of(n);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
        record(n, gu::get_driver_of_sink_name(n, "clock_pin"));
      }
    }
  }
}

bool Design_clocks::is_clock(const hhds::Pin_class& root) const {
  if (root.is_invalid()) {
    return false;
  }
  if (roots_.contains(root_key(root))) {
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

bool Design_clocks::is_clock(const hhds::Occurrence_pin& root) const {
  if (root.is_invalid()) {
    return false;
  }
  // Keyed by (body gid, class index): the occurrence PATH is deliberately not
  // part of the key — a clock root of body B is a clock root at every occurrence
  // of B — but the body identity is, or bodies alias (see Root_key).
  if (roots_.contains(root_key(root))) {
    return true;
  }
  return gu::is_graph_input_pin(root) && name_looks_like_clock(gu::pin_name_of(root));
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
  if (q.is_invalid() || din.is_invalid()) {
    return {};
  }
  if (!gu::is_const_pin(din) && !gu::is_graph_input_pin(din)) {
    auto mux = din.get_master_node();
    if (gu::type_op_of(mux) == Ntype_op::Mux) {
      // tolg's hold-mux shape (`gate ? d : q`): the mux must be peeled, because
      // its condition reads the clock and, evaluated AT the sampling edge,
      // selects the stale Q. A mux with no Q arm is a genuine VALUE mux and
      // falls through: it is the through-value itself.
      bool            has_q_arm = false;
      hhds::Pin_class other;
      for (const auto& e : mux.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          continue;  // the selector is the gate, not an arm
        }
        if (!e.driver.is_invalid() && e.driver.get_class_index() == q.get_class_index()) {
          has_q_arm = true;
        } else if (other.is_invalid()) {
          other = e.driver;
        }
      }
      if (has_q_arm) {
        return other;  // invalid `other` fails closed, as before
      }
    }
  }
  // After cprop::canonicalize_latch_holds the hold mux is stripped and the open
  // condition lives on `enable`, so din -- including a graph input, a const, a
  // Get_mask, or a residual value mux -- IS the value the latch passes through.
  return din;
}

hhds::Occurrence_pin latch_transparent_arm(const hhds::Occurrence_node& n) {
  if (gu::type_op_of(n) != Ntype_op::Latch) {
    return {};
  }
  auto q   = n.get_driver_pin(0);
  auto din = gu::get_driver_of_sink_name(n, "din");
  if (q.is_invalid() || din.is_invalid()) {
    return {};
  }
  if (!gu::is_const_pin(din) && !gu::is_graph_input_pin(din)) {
    auto mux = din.get_master_node();
    if (gu::type_op_of(mux) == Ntype_op::Mux) {
      bool                 has_q_arm = false;
      hhds::Occurrence_pin other;
      for (const auto& e : mux.inp_edges()) {
        if (e.sink.get_port_id() == 0) {
          continue;
        }
        if (!e.driver.is_invalid() && e.driver == q) {
          has_q_arm = true;
        } else if (other.is_invalid()) {
          other = e.driver;
        }
      }
      if (has_q_arm) {
        return other;
      }
    }
  }
  return din;
}

int inline_clock_gate_cells(hhds::Graph* g, std::string_view from_pass, const std::function<bool(const hhds::Graph*)>& is_boxed) {
  if (g == nullptr) {
    return 0;
  }
  // 1. Collect the pins that drive some state element's clock_pin.
  absl::flat_hash_set<hhds::Class_index> clock_drivers;
  for (auto n : g->body().nodes()) {
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
      d = gu::first_value_driver(dn);
    }
  }
  // ...AND the pins that drive a CHILD's clock PORT. A gate whose output crosses
  // into a sub-instance is just as much a clock driver as one wired to a local
  // flop — it is simply one definition away — and collecting only the local case
  // left minion's whole vpu/txfma cone with an unfoldable gate.
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto cdef = n.get_subnode_graph();
    if (!cdef) {
      continue;
    }
    auto cio = cdef->get_io();
    if (!cio) {
      continue;
    }
    // The child's input ports some state element inside it clocks on.
    absl::flat_hash_set<uint32_t> clk_pids;
    for (const auto& cd : cio->get_input_pin_decls()) {
      auto cport = cdef->get_input_pin(cd.name);
      if (cport.is_invalid()) {
        continue;
      }
      for (auto cn : cdef->body().nodes()) {
        const auto cop = gu::type_op_of(cn);
        if (cop != Ntype_op::Flop && cop != Ntype_op::Fflop && cop != Ntype_op::Memory && cop != Ntype_op::Latch) {
          continue;
        }
        auto ccd = gu::get_driver_of_sink_name(cn, "clock_pin");
        if (!ccd.is_invalid() && ccd.get_class_index() == cport.get_class_index()) {
          clk_pids.insert(static_cast<uint32_t>(cd.port_id));
          break;
        }
      }
    }
    if (clk_pids.empty()) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      if (!clk_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
        continue;
      }
      auto d = e.driver;
      for (int hops = 0; hops < 8 && !d.is_invalid(); ++hops) {
        if (gu::is_graph_input_pin(d) || gu::is_const_pin(d)) {
          break;
        }
        auto dn = d.get_master_node();
        clock_drivers.insert(dn.get_class_index());
        if (gu::type_op_of(dn) != Ntype_op::Get_mask && gu::type_op_of(dn) != Ntype_op::Sext) {
          break;
        }
        d = gu::first_value_driver(dn);
      }
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
  for (auto n : g->body().nodes()) {
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
    // A def the caller BLACKBOXED stays a blackbox. Inlining it would compare
    // internals the caller declared out of scope -- see the header: minion's
    // trusted `prim_clk_gate` holds an unreset enable latch the two front-ends
    // default differently, so comparing it manufactures a refutation.
    if (is_boxed && is_boxed(def.get())) {
      continue;
    }
    // A gate cell is recognized either by holding the enable LATCH (the real
    // ICG, prim_clk_gate) or by being entirely STATE-FREE. The second arm
    // matters because a plain `assign clk_o = clk_i & en;` wrapper is a clock
    // gate too, and refusing it left the flop it clocks with an unfoldable
    // "derived clock" for want of a latch nobody wrote. Inlining a state-free
    // callee cannot change state identity — the whole reason the latch test was
    // conservative — so the added arm carries none of that risk.
    bool has_latch  = false;
    bool state_free = true;
    for (auto dn : def->body().nodes()) {
      const auto dop = gu::type_op_of(dn);
      if (dop == Ntype_op::Latch) {
        has_latch = true;
      }
      if (dop == Ntype_op::Latch || dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Memory
          || dop == Ntype_op::Sub) {
        state_free = false;  // a nested Sub is opaque here: treat it as state
      }
    }
    if (has_latch || state_free) {
      cells.push_back(n);
    }
  }
  int done = 0;
  for (const auto& c : cells) {  // never mutate while iterating fast_class
    const bool ok = gu::inline_sub_instance(g, c, from_pass);
    if (::getenv("LIVEHD_SIM_CLK_DEBUG") != nullptr) {
      std::fprintf(stderr,
                   "[icgdbg] %s: inline cell %s -> %s\n",
                   std::string{g->get_name()}.c_str(),
                   gu::debug_name(c).c_str(),
                   ok ? "ok" : "FAILED");
    }
    if (ok) {
      ++done;
    }
  }
  return done;
}

namespace {
// A GATE CHAIN is walked one cell at a time, each contributing its own enable.
// The bound is a runaway guard, not a design limit: a clock cone is
// structurally allowed to be cyclic (nothing type-checks it), and minion's
// deepest real chain — `cgate_txfma` -> `txfma_top` -> a per-stage re-gate — is
// three cells.
constexpr int kMaxGateChain = 16;

[[nodiscard]] std::optional<Icg_cone> clock_op_depth(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth);
[[nodiscard]] std::optional<Icg_cone> resolve_icg_depth(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth);

// Absorb an inner cell's cone into `outer_cone`: the chain's clock is the INNER
// cell's clock (recursively, the chain's root) and the guards ACCUMULATE — a
// flop behind `gate(gate(clk, e0), e1)` commits only when e0 AND e1 are true.
// `outer_inverted` is the inversion picked up between the two cells.
void absorb_chain(Icg_cone& outer_cone, const Icg_cone& inner, bool outer_inverted) {
  outer_cone.clock          = inner.clock;
  outer_cone.clock_inverted = inner.clock_inverted != outer_inverted;
  outer_cone.div            = inner.div;  // v1 refuses div != 1 at every lowering
  outer_cone.enables.insert(outer_cone.enables.end(), inner.enables.begin(), inner.enables.end());
}
}  // namespace

std::optional<Icg_cone> resolve_icg(const hhds::Pin_class& clock_pin, const Design_clocks& clocks) {
  return resolve_icg_depth(clock_pin, clocks, 0);
}

namespace {
std::optional<Icg_cone> resolve_icg_depth(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth) {
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
      icg.clock          = ph.net;
      // `~clk & en` gates the FALLING edge; an inversion on the way down to the
      // And (a latch's `Mux(cone,0,1)` shaping, or an explicit `!`) flips it too.
      icg.clock_inverted = ph.inverted != outer.inverted;
      continue;
    }
    // A GATE ON AN ALREADY GATED CLOCK. `is_clock` cannot see this operand as a
    // clock — only nets a flop actually clocks on (plus a conventionally named
    // input) are roots, and an intermediate gate output is neither — so before
    // 2026-08-05 the inner gate was filed as an ENABLE, the cone came out with
    // ZERO clock operands, and the whole chain was refused as "some other
    // derived clock". minion's VPU lane cascades three of these
    // (`cgate_txfma` -> `txfma_top` -> a per-stage re-gate), which is why the
    // recursion lives HERE, in the shared recognizer, rather than in any one
    // consumer's private matcher.
    if (depth < kMaxGateChain) {
      if (auto inner = clock_op_depth(e.driver, clocks, depth + 1)) {
        ++n_clock;
        absorb_chain(icg, *inner, outer.inverted);
        continue;
      }
    }
    icg.enables.push_back(e.driver);
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
}  // namespace

// ---------------------------------------------------------------------------
// 2f-latch M9 — Clock_cell: recognition and materialization.

std::optional<Icg_cone> clock_cell_cone(const hhds::Node_class& n, const Design_clocks& clocks) {
  if (gu::type_op_of(n) != Ntype_op::Clock_cell) {
    return std::nullopt;
  }
  Icg_cone   c;
  const auto ref = gu::get_driver_of_sink_name(n, "clk_ref");
  if (ref.is_invalid()) {
    return std::nullopt;
  }
  // Fold any inversion picked up on the way to the reference into the cone's
  // edge, exactly as the inline path does -- so a Clock_cell fed `~clk` and one
  // carrying invert=true are the SAME operation and canonicalize together.
  const Phase ph   = resolve_phase(ref);
  c.clock          = ph.net.is_invalid() ? ref : ph.net;
  // `invert` (the active-low gate flavour) does NOT change the reference edge a
  // consumer commits on -- see the note in resolve_phase. It only moves the
  // enable's sample point, which this cone type does not carry.
  c.clock_inverted = ph.inverted;
  if (const auto d = gu::get_driver_of_sink_name(n, "div"); !d.is_invalid() && gu::is_const_pin(d)) {
    const auto dv = gu::hydrate_const(d);
    c.div         = (dv.is_just_i64() && dv.to_just_i64() > 0) ? static_cast<int>(dv.to_just_i64()) : 0;
  }
  if (const auto en = gu::get_driver_of_sink_name(n, "en"); !en.is_invalid() && !const_is(en, 1)) {
    c.enables.push_back(en);
  }
  (void)clocks;  // clock identity is structural here: clk_ref IS the reference by construction
  return c;
}

std::optional<Icg_cone> clock_op_of(const hhds::Pin_class& clock_pin, const Design_clocks& clocks) {
  return clock_op_depth(clock_pin, clocks, 0);
}

namespace {
std::optional<Icg_cone> clock_op_depth(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth) {
  if (clock_pin.is_invalid()) {
    return std::nullopt;
  }
  // A materialized cell wins: once the gate is a Clock_cell the And cone that
  // produced it is gone, and reading the cell is exact rather than a re-match.
  // `stop_at_clock_cell` is what keeps a CHAIN visible: the default walk
  // canonicalizes straight through a cell to its reference clock, which is the
  // right answer for a commit CLASS but silently discards the inner cell's
  // enable — and an enable dropped from a guard is a commit that should not
  // have happened.
  const Phase outer = resolve_phase(clock_pin, /*stop_at_clock_cell=*/true);
  if (!outer.net.is_invalid() && !gu::is_graph_input_pin(outer.net) && !gu::is_const_pin(outer.net)) {
    if (auto c = clock_cell_cone(outer.net.get_master_node(), clocks)) {
      c->clock_inverted = c->clock_inverted != outer.inverted;
      // A cell whose clk_ref is ITSELF a clock operation: absorb it, so a
      // chain contributes one guard per cell.
      if (depth < kMaxGateChain) {
        if (const auto ref = gu::get_driver_of_sink_name(outer.net.get_master_node(), "clk_ref"); !ref.is_invalid()) {
          if (auto inner = clock_op_depth(ref, clocks, depth + 1)) {
            Icg_cone chained = *c;  // keeps this cell's own enable
            absorb_chain(chained, *inner, outer.inverted);
            return chained;
          }
        }
      }
      return c;
    }
  }
  return resolve_icg_depth(clock_pin, clocks, depth);
}
}  // namespace

std::optional<Icg_def_match> match_icg_def(hhds::Graph* def) {
  if (def == nullptr) {
    return std::nullopt;
  }
  auto gio = def->get_io();
  if (!gio) {
    return std::nullopt;
  }
  // A gate cell has exactly one clock output. Try each declared output; the
  // shape test below is strict enough that at most one can match.
  for (const auto& od : gio->get_output_pin_decls()) {
    auto opin = def->get_output_pin(od.name);
    if (opin.is_invalid()) {
      continue;
    }
    hhds::Pin_class inner;
    for (const auto& e : opin.inp_edges()) {
      inner = e.driver;
      break;
    }
    if (inner.is_invalid() || gu::is_const_pin(inner) || gu::is_graph_input_pin(inner)) {
      continue;
    }
    const Phase op_ph = resolve_phase(inner);
    if (op_ph.net.is_invalid() || gu::is_graph_input_pin(op_ph.net) || gu::is_const_pin(op_ph.net)) {
      continue;
    }
    auto       gate    = op_ph.net.get_master_node();
    const auto gate_op = gu::type_op_of(gate);
    // TWO flavours, and the design corpus has both. `clk & en_latch` (enable
    // latched while the clock is LOW, output idles low) is the ordinary ICG;
    // `clk | ~en_latch` (enable latched while the clock is HIGH, output idles
    // high) is minion's prim_clk_gate_n. They gate the SAME reference edges --
    // neither output moves while the enable is deasserted -- so the only
    // difference is the enable's sample point, recorded as `invert`.
    if (gate_op != Ntype_op::And && gate_op != Ntype_op::Or) {
      continue;
    }
    if (op_ph.inverted) {
      // An inversion between the gate and the output (`~(clk & en)`) is a
      // genuinely inverted CLOCK, not a flavour: its consumers commit on the
      // opposite reference edge. Not modelled -- leave it to the derived-clock
      // refusal rather than fold it into `invert`, whose meaning is the flavour.
      continue;
    }
    // Split the And's operands into "the one holding a latch below it" (the
    // enable) and "the bare input port" (the clock). This is the STRUCTURAL
    // anchor that replaces the name heuristic: a gate cell's own body holds no
    // flop, so Design_clocks has no root to offer inside the def.
    hhds::Pin_class clk_port, latched;
    int             n_ports = 0, n_latched = 0;
    for (const auto& e : gate.inp_edges()) {
      if (e.driver.is_invalid() || gu::is_const_pin(e.driver)) {
        continue;  // a width mask, not an operand
      }
      const Phase ph = resolve_phase(e.driver);
      if (!ph.net.is_invalid() && gu::is_graph_input_pin(ph.net)) {
        ++n_ports;
        clk_port = ph.net;
        continue;
      }
      if (!ph.net.is_invalid() && !gu::is_const_pin(ph.net) && gu::type_op_of(ph.net.get_master_node()) == Ntype_op::Latch) {
        ++n_latched;
        latched = ph.net;
        continue;
      }
      return std::nullopt;  // an operand that is neither: not a gate cell
    }
    if (n_ports != 1 || n_latched != 1) {
      continue;
    }
    // CONFIRM the latch is the enable sampler for THIS clock: it must be
    // transparent on the opposite phase of the same port. Without this check a
    // module that ANDs a clock with an unrelated latched data bit would match,
    // and its "enable" would be sampled at the wrong time.
    auto        latch_node = latched.get_master_node();
    const Phase gate_ph    = resolve_phase(gu::get_driver_of_sink_name(latch_node, "enable"));
    if (gate_ph.net.is_invalid() || gate_ph.net.get_class_index() != clk_port.get_class_index()) {
      continue;
    }
    auto arm = latch_transparent_arm(latch_node);
    if (arm.is_invalid()) {
      arm = gu::get_driver_of_sink_name(latch_node, "din");  // raw yosys D/EN shape
    }
    if (arm.is_invalid()) {
      continue;
    }
    Icg_def_match m;
    m.clk_in      = clk_port;
    m.out         = opin;
    m.enable_cone = arm;
    m.invert      = gate_op == Ntype_op::Or;
    return m;
  }
  return std::nullopt;
}

namespace {

// Clone the COMBINATIONAL cone rooted at `d` (a pin inside `def`) into
// `parent`, re-rooting the def's graph inputs onto the instance's drivers.
//
// Fails closed (invalid pin) on anything that is not pure comb -- a state
// element, a Memory, a nested Sub. That is the boundary that keeps this from
// becoming an inline: only a function of nets the parent ALREADY drives may
// cross, so no state and no new compare point comes with it.
class Cone_cloner {
public:
  Cone_cloner(hhds::Graph* parent, hhds::Graph* def, const hhds::Node_class& inst,
              const absl::flat_hash_map<std::string, uint32_t>& in_name2pid)
      : parent_(parent), def_(def), inst_(inst), in_name2pid_(in_name2pid) {}

  [[nodiscard]] bool failed() const { return failed_; }

  hhds::Pin_class clone(const hhds::Pin_class& d, int depth = 0) {
    if (d.is_invalid() || depth > 64) {
      failed_ = true;
      return {};
    }
    if (gu::is_const_pin(d)) {
      return gu::create_const(*parent_, gu::hydrate_const(d));
    }
    if (auto it = cache_.find(d); it != cache_.end()) {
      return it->second;
    }
    if (gu::is_graph_input_pin(d)) {
      auto pit = in_name2pid_.find(std::string{gu::pin_name_of(d)});
      if (pit == in_name2pid_.end()) {
        failed_ = true;
        return {};
      }
      auto res = driver_feeding(pit->second);
      if (res.is_invalid()) {
        failed_ = true;  // the parent left the port dangling: refuse rather than invent a value
        return {};
      }
      cache_[d] = res;
      return res;
    }
    auto       n  = d.get_master_node();
    const auto op = gu::type_op_of(n);
    if (Ntype::is_loop_last(op) || op == Ntype_op::Sub || op == Ntype_op::Memory) {
      failed_ = true;  // state or hierarchy in the enable cone: not a plain function of the ports
      return {};
    }
    auto neo = gu::create_typed_node(*parent_, op);
    // Carry the source span only -- NOT the name. A cloned cone node is a new
    // node in the parent's namespace, and copying the def-local name would
    // collide across two instances of the same cell.
    if (auto a = n.attr(hhds::attrs::srcid); a.has() && a.get() != 0 && def_ != nullptr) {
      neo.attr(hhds::attrs::srcid).set(parent_->source_locator().import_from(def_->source_locator(), a.get()));
    }
    auto res  = neo.create_driver_pin(d.get_port_id());
    cache_[d] = res;  // before recursing: a diamond re-uses the clone, a cycle would recurse forever
    for (const auto& e : n.inp_edges()) {
      auto sp = neo.create_sink_pin(e.sink.get_port_id());
      auto dp = clone(e.driver, depth + 1);
      if (dp.is_invalid()) {
        failed_ = true;
        return {};
      }
      dp.connect_sink(sp);
    }
    gu::set_bits(res, std::max(1, static_cast<int>(gu::bits_of(d))));
    return res;
  }

private:
  hhds::Pin_class driver_feeding(uint32_t pid) {
    for (const auto& e : inst_.inp_edges()) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) != pid) {
        continue;
      }
      if (gu::is_const_pin(e.driver)) {
        return gu::create_const(*parent_, gu::hydrate_const(e.driver));
      }
      return e.driver;
    }
    return {};
  }

  hhds::Graph*                                          parent_;
  hhds::Graph*                                          def_;
  hhds::Node_class                                      inst_;
  const absl::flat_hash_map<std::string, uint32_t>&     in_name2pid_;
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> cache_;
  bool                                                  failed_ = false;
};

}  // namespace

namespace {

// Walk a control pin back through the IDENTITY wrappers a typed port read picks
// up, to the graph INPUT it comes from. Invalid when it does not come from one.
hhds::Pin_class walk_to_graph_input(hhds::Pin_class p) {
  for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
    if (gu::is_graph_input_pin(p)) {
      return p;
    }
    if (gu::is_const_pin(p)) {
      return {};
    }
    auto       n  = p.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Get_mask && op != Ntype_op::Sext && op != Ntype_op::Set_mask) {
      return {};
    }
    p = gu::first_value_driver(n);
  }
  return {};
}

// The names of `def`'s INPUT ports that are used as a CLOCK inside it — either
// directly by a state element's `clock_pin`, or (recursively) by a nested
// instance's own clock port.
//
// This is what makes a gate recognizable when its output does not clock
// anything in the module that BUILDS it. That is not an exotic case: minion
// gates `ctrl_frf_clk` in `txfmafrac_top` and passes it DOWN into `txfma_f0`,
// so a scan that only looks at local `clock_pin`s sees no clock driver at all
// and the gate stays an opaque `Sub`.
absl::flat_hash_set<std::string> clock_port_names(hhds::Graph* def, int depth) {
  absl::flat_hash_set<std::string> out;
  if (def == nullptr || depth > 8) {
    return out;
  }
  // A GATE CELL's own clock port. Its only state element is the enable latch,
  // whose gate is its ENABLE (a latch has no clock_pin -- user ruling
  // 2026-07-20), so the scan below finds nothing and the cell looks like it has
  // no clock port at all. That breaks a GATE CHAIN: the outer gate's clk port is
  // fed by the inner gate's output, and without this the inner one is never seen
  // as driving a clock, never materializes, and stays an opaque Sub -- which
  // then makes the outer cell's reference clock a derived net.
  if (auto m = match_icg_def(def); m.has_value() && !m->clk_in.is_invalid()) {
    out.insert(std::string(gu::pin_name_of(m->clk_in)));
  }
  for (auto n : def->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Memory || op == Ntype_op::Latch) {
      if (auto in = walk_to_graph_input(gu::get_driver_of_sink_name(n, "clock_pin")); !in.is_invalid()) {
        out.insert(std::string(gu::pin_name_of(in)));
      }
      continue;
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto child = n.get_subnode_graph();
    if (!child) {
      continue;
    }
    const auto inner = clock_port_names(child.get(), depth + 1);
    if (inner.empty()) {
      continue;
    }
    auto cio = child->get_io();
    if (!cio) {
      continue;
    }
    absl::flat_hash_set<uint32_t> clk_pids;
    for (const auto& d : cio->get_input_pin_decls()) {
      if (inner.contains(d.name)) {
        clk_pids.insert(static_cast<uint32_t>(d.port_id));
      }
    }
    for (const auto& e : n.inp_edges()) {
      if (!clk_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
        continue;
      }
      if (auto in = walk_to_graph_input(e.driver); !in.is_invalid()) {
        out.insert(std::string(gu::pin_name_of(in)));  // a clock threaded straight through
      }
    }
  }
  return out;
}

}  // namespace

int materialize_clock_cells(hhds::Graph* g, std::string_view from_pass, const std::function<bool(const hhds::Graph*)>& is_boxed) {
  if (g == nullptr) {
    return 0;
  }
  // 1. Which nodes drive a state element's clock_pin? Same scan (and the same
  //    node-keyed trap) as inline_clock_gate_cells: the SAME pin reached through
  //    an edge and through out_pins() does not compare equal.
  absl::flat_hash_set<hhds::Class_index> clock_drivers;
  auto                                   note_clock_driver = [&clock_drivers](hhds::Pin_class d) {
    for (int hops = 0; hops < 8 && !d.is_invalid(); ++hops) {
      if (gu::is_graph_input_pin(d) || gu::is_const_pin(d)) {
        break;
      }
      auto dn = d.get_master_node();
      clock_drivers.insert(dn.get_class_index());
      const auto dop = gu::type_op_of(dn);
      if (dop != Ntype_op::Get_mask && dop != Ntype_op::Sext && dop != Ntype_op::Set_mask) {
        break;
      }
      d = gu::first_value_driver(dn);
    }
  };
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Memory || op == Ntype_op::Latch) {
      note_clock_driver(gu::get_driver_of_sink_name(n, "clock_pin"));
      continue;
    }
    // A gate whose output clocks nothing HERE but is passed DOWN into a child's
    // clock port. Legal per M9's binding rule (a Sub input port is a clock
    // sink), and the common shape in real hierarchy -- without this the gate
    // stays an opaque Sub and every flop below it refuses.
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto def = n.get_subnode_graph();
    if (!def) {
      continue;
    }
    const auto cnames = clock_port_names(def.get(), 0);
    if (cnames.empty()) {
      continue;
    }
    auto gio = def->get_io();
    if (!gio) {
      continue;
    }
    absl::flat_hash_set<uint32_t> clk_pids;
    for (const auto& d : gio->get_input_pin_decls()) {
      if (cnames.contains(d.name)) {
        clk_pids.insert(static_cast<uint32_t>(d.port_id));
      }
    }
    for (const auto& e : n.inp_edges()) {
      if (clk_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
        note_clock_driver(e.driver);
      }
    }
  }
  if (clock_drivers.empty()) {
    return 0;
  }
  // 2. Collect the instantiated gate cells (never mutate while walking).
  std::vector<std::pair<hhds::Node_class, Icg_def_match>> cells;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub || !clock_drivers.contains(n.get_class_index())) {
      continue;
    }
    auto def = n.get_subnode_graph();
    if (!def) {
      continue;  // a body-less blackbox needs the DECLARED entry point (not implemented yet)
    }
    if (is_boxed && is_boxed(def.get())) {
      continue;
    }
    if (auto m = match_icg_def(def.get())) {
      cells.emplace_back(n, *m);
    }
  }
  int done = 0;
  for (const auto& [inst, m] : cells) {
    auto def = inst.get_subnode_graph();
    auto gio = def ? def->get_io() : nullptr;
    if (!gio) {
      continue;
    }
    absl::flat_hash_map<std::string, uint32_t> in_name2pid;
    for (const auto& d : gio->get_input_pin_decls()) {
      in_name2pid[d.name] = static_cast<uint32_t>(d.port_id);
    }
    uint32_t out_pid = 0;
    bool     found   = false;
    for (const auto& d : gio->get_output_pin_decls()) {
      if (d.name == gu::pin_name_of(m.out)) {
        out_pid = static_cast<uint32_t>(d.port_id);
        found   = true;
        break;
      }
    }
    if (!found) {
      continue;
    }
    // The parent net wired to the def's clock port.
    hhds::Pin_class clk_src;
    if (auto pit = in_name2pid.find(std::string{gu::pin_name_of(m.clk_in)}); pit != in_name2pid.end()) {
      for (const auto& e : inst.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) == pit->second) {
          clk_src = e.driver;
          break;
        }
      }
    }
    if (clk_src.is_invalid()) {
      continue;
    }
    Cone_cloner cc(g, def.get(), inst, in_name2pid);
    auto        en_src = cc.clone(m.enable_cone);
    if (cc.failed() || en_src.is_invalid()) {
      continue;  // fail closed: leave the Sub alone and let the consumer refuse by name
    }
    auto cell = gu::create_typed_node(*g, Ntype_op::Clock_cell);
    clk_src.connect_sink(gu::setup_sink_by_name(cell, "clk_ref"));
    en_src.connect_sink(gu::setup_sink_by_name(cell, "en"));
    if (m.invert) {
      gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(cell, "invert"));
    }
    auto cq = cell.get_driver_pin(0);
    gu::set_bits(cq, 1);

    // Everything the instance's clock output drove now reads the cell.
    std::vector<hhds::Pin_class> readers;
    for (const auto& e : inst.out_edges()) {
      if (static_cast<uint32_t>(e.driver.get_port_id()) == out_pid) {
        readers.push_back(e.sink);
      }
    }
    for (const auto& s : readers) {
      cq.connect_sink(s);
    }
    // Drop the instance. This is the step that makes the ICG's LATCH disappear
    // from every latch-counting consumer -- the cell carries the sampling
    // contract instead -- so a design whose only latches were its clock gates
    // stops tripping the hierarchy refusal entirely.
    inst.del_node();
    ++done;
  }
  if (done > 0) {
    livehd::diag::info(from_pass, "clock-cell-materialized", "progress")
        .msg("{}: materialized {} clock-gate cell(s) as Clock_cell in `{}`", from_pass, done, g->get_name())
        .emit();
  }
  return done;
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
    auto         clk          = gu::get_driver_of_sink_name(n, "clock_pin");
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
      cc.role      = Net_role::Clock;
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
    cc.net    = icg->clock;
    cc.role   = Net_role::Clock;
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
  cc.net    = ph.net;
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
  for (auto n : g->body().nodes()) {
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
  for (auto n : g->body().nodes()) {
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
