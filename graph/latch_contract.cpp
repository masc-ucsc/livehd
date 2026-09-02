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
//   Xor(x, 1)      canonical u1 boolean negation    -> follow x, flip parity
//   Xor(x, 0)      boolean identity                 -> follow x
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
  if (p.is_invalid() || !p.is_const()) {
    return false;
  }
  const auto& c = gu::const_of(p);
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
    if (gu::is_graph_input_pin(ph.net) || ph.net.is_const()) {
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
      if (b.is_const()) {
        val = a;
        cst = b;
      } else if (a.is_const()) {
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

    if (op == Ntype_op::Xor) {
      // lesssign's canonical logical negation is `u1 ^ 1`. Treat only the
      // exact two-input boolean form as phase shaping; an n-ary/data XOR is a
      // genuine computed control root and must remain opaque.
      Pin val;
      Pin cst;
      int cnt = 0;
      for (const auto& e : n.inp_edges()) {
        ++cnt;
        if (e.driver.is_const()) {
          if (!cst.is_invalid()) {
            break;
          }
          cst = e.driver;
        } else {
          if (!val.is_invalid()) {
            break;
          }
          val = e.driver;
        }
      }
      if (cnt != 2 || val.is_invalid() || cst.is_invalid()) {
        break;
      }
      // `x ^ 1` is a logical negation ONLY for a u1 `x`; on anything wider it
      // flips bit 0 alone and the result is not the inverted phase of `x`.
      // Refusing here just leaves the XOR as the (opaque) control root, which
      // is the conservative direction.
      if (gu::bits_of(val) != 1 || !gu::is_unsign(val)) {
        break;
      }
      if (const_is(cst, 1)) {
        ph.inverted = !ph.inverted;
        ph.net      = val;
        continue;
      }
      if (const_is(cst, 0)) {
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
// Latch is the ENABLE POLARITY: known-false = active LOW, which
// flips the transparent level and therefore the phase.
Phase latch_phase(const hhds::Node_class& n) {
  Phase ph = resolve_phase(gu::get_driver_of_sink_name(n, "enable"));
  auto  pc = gu::get_driver_of_sink_name(n, "posclk");
  if (pc.is_known_false()) {
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
    if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
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
  return pc.is_known_false();
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
  if (!din.is_const() && !gu::is_graph_input_pin(din)) {
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
  if (!din.is_const() && !gu::is_graph_input_pin(din)) {
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

namespace {

[[nodiscard]] bool const_true(const hhds::Pin_class& p) { return p.is_known_true(); }

[[nodiscard]] std::optional<Reset_input_port> reset_root_port(const hhds::Pin_class& driver, bool active_low) {
  if (driver.is_invalid()) {
    return std::nullopt;
  }
  const auto root = control_root(driver);
  if (root.net.is_invalid() || !gu::is_graph_input_pin(root.net)) {
    return std::nullopt;
  }
  return Reset_input_port{static_cast<uint32_t>(root.net.get_port_id()), active_low != root.inverted};
}

}  // namespace

const Clock_input_ports& clock_input_interface(const std::shared_ptr<hhds::Graph>& def, Clock_port_cache& cache) {
  // A subtree the walk cannot enter -- a body-less black box, or one whose walk
  // is already in flight (mutual instantiation) -- is NOT an analyzed-empty
  // one. Reporting it complete lets a caller gate nothing and call it success.
  static const Clock_input_ports unknown{{}, /*complete=*/false};
  if (!def) {
    return unknown;
  }
  if (auto it = cache.clock_memo.find(def.get()); it != cache.clock_memo.end()) {
    return it->second;
  }
  if (!cache.clock_busy.insert(def.get()).second) {
    return unknown;
  }

  Clock_input_ports                                                   result;
  const Design_clocks                                                 clocks(def.get(), /*hier=*/false);
  std::function<std::optional<uint32_t>(const hhds::Pin_class&, int)> root_port;
  root_port = [&](const hhds::Pin_class& driver, int depth) -> std::optional<uint32_t> {
    if (driver.is_invalid() || depth > 16) {
      return std::nullopt;
    }
    hhds::Pin_class root = driver;
    if (auto cone = clock_op_of(driver, clocks); cone && !cone->clock.is_invalid()) {
      root = cone->clock;
    } else {
      root = control_root(driver).net;
    }
    if (!root.is_invalid() && gu::is_graph_input_pin(root)) {
      return static_cast<uint32_t>(root.get_port_id());
    }

    // A Pyrope `wire x = nil; x = source` is represented as an Or reduction.
    // Once nil/constant arms disappear it has one live operand and is an
    // identity wrapper. Clock-interface discovery must see through it before
    // deciding whether the source is a declared port or an instantiated ICG.
    if (!root.is_invalid() && !root.is_const() && !gu::is_graph_input_pin(root)) {
      const auto aggregate = root.get_master_node();
      if (gu::type_op_of(aggregate) == Ntype_op::Or) {
        hhds::Pin_class only;
        int             live_inputs = 0;
        for (const auto& edge : aggregate.inp_edges()) {
          if (edge.driver.is_invalid() || edge.driver.is_const()) {
            continue;
          }
          only = edge.driver;
          ++live_inputs;
        }
        if (live_inputs == 1) {
          return root_port(only, depth + 1);
        }
      }
    }

    // A clock gate may still be an ordinary Sub at this pre-emission seam.
    // State in one child then receives the output of a sibling ICG, and the
    // local cone walk necessarily stops at the instance boundary.  Decode the
    // strict ICG definition and continue from its bound reference-clock input;
    // its enable is timing control, not the clock root.  This is the same
    // structural match materialize_clock_cells uses later, but read-only here
    // so activation analysis does not depend on graph rewrite order.
    const auto rn
        = root.is_invalid() || root.is_const() || gu::is_graph_input_pin(root) ? hhds::Node_class{} : root.get_master_node();
    if (!rn.is_invalid() && gu::type_op_of(rn) == Ntype_op::Sub) {
      auto gate_def = rn.get_subnode_graph();
      auto gate_io  = rn.get_subnode_io();
      auto gate     = match_icg_def(gate_def.get());
      if (gate && gate_io && root.get_port_id() == gate->out.get_port_id()) {
        uint32_t clock_pid = 0;
        bool     found_pid = false;
        for (const auto& decl : gate_io->get_input_pin_decls()) {
          if (decl.name == gu::pin_name_of(gate->clk_in)) {
            clock_pid = static_cast<uint32_t>(decl.port_id);
            found_pid = true;
            break;
          }
        }
        if (found_pid) {
          for (const auto& edge : rn.inp_edges()) {
            if (static_cast<uint32_t>(edge.sink.get_port_id()) == clock_pid) {
              return root_port(edge.driver, depth + 1);
            }
          }
        }
      }
    }
    return std::nullopt;
  };

  // A clock cone that is PRESENT but does not reduce to a declared input is an
  // unresolved analysis, exactly as on the reset side. A state element with no
  // clock_pin driver at all is not: it commits on the module's implicit clock,
  // so there is no port to name and nothing was lost.
  const auto add_clock = [&](const hhds::Pin_class& driver) {
    if (driver.is_invalid()) {
      return;
    }
    if (auto pid = root_port(driver, 0)) {
      result.ports.insert(*pid);
    } else {
      result.complete = false;
    }
  };

  for (auto n : def->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      gu::for_each_memory_clock_driver(n, add_clock);
      continue;
    }
    // A latch's gate IS its enable: `clock_pin` (pid 2) is RESERVED and tolg
    // refuses to drive it, so `add_clock` sees an invalid driver and the latch
    // contributes no port. It is deliberately NOT reported as incomplete: a
    // conditionally-called latch-only callee is a supported, tested shape
    // (tests/equiv/conditional_latch_call), and refusing it here would reject
    // it outright. Withholding the activation from a latch needs its ENABLE
    // gated rather than a clock port, which this walk does not model.
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      add_clock(gu::get_driver_of_sink_name(n, "clock_pin"));
      continue;
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    // `lgassert` and `fproperty` are recognized PRIMITIVES: tolg materializes
    // them with `create_io` and NO graph body, and they hold no state. Reading
    // that null body as an unwalkable subtree makes every conditionally-called
    // module that contains an assert or a runtime range select `a#[lo..=hi]`
    // report INCOMPLETE -- and a refused instance is then left UNGATED. Same
    // exemption pass_single_edge and the LEC box scan already apply.
    if (auto sio = n.get_subnode_io();
        sio != nullptr && (sio->get_name() == gu::lgassert_module_name || sio->get_name() == gu::fproperty_module_name)) {
      continue;
    }
    const auto  child_graph = n.get_subnode_graph();
    const auto& child       = clock_input_interface(child_graph, cache);
    result.complete         = result.complete && child.complete;
    for (const auto cp : child.ports) {
      bool found = false;
      for (const auto& e : n.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != cp) {
          continue;
        }
        add_clock(e.driver);
        found = true;
        break;
      }
      if (!found) {
        result.complete = false;  // the child clocks state on a port nothing binds
      }
    }
  }

  cache.clock_busy.erase(def.get());
  return cache.clock_memo.emplace(def.get(), std::move(result)).first->second;
}

const absl::flat_hash_set<uint32_t>& clock_input_ports(const std::shared_ptr<hhds::Graph>& def, Clock_port_cache& cache) {
  return clock_input_interface(def, cache).ports;
}

const Reset_input_ports& reset_input_ports(const std::shared_ptr<hhds::Graph>& def, Clock_port_cache& cache) {
  // Not `{}`: an unwalkable subtree (body-less black box, or a walk already in
  // flight) has an empty port list because nothing was LOOKED at, which is the
  // opposite of "no reset state in here" for every caller that decides whether
  // it may skip the instance.
  static const Reset_input_ports unknown{{}, /*complete=*/false};
  if (!def) {
    return unknown;
  }
  if (auto it = cache.reset_memo.find(def.get()); it != cache.reset_memo.end()) {
    return it->second;
  }
  if (!cache.reset_busy.insert(def.get()).second) {
    return unknown;
  }

  Reset_input_ports result;
  auto              add_reset = [&](const hhds::Pin_class& driver, bool active_low) {
    if (driver.is_invalid()) {
      return;
    }
    if (auto root = reset_root_port(driver, active_low)) {
      result.ports.push_back(*root);
    } else {
      result.complete = false;
    }
  };

  for (auto n : def->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      auto reset = gu::get_driver_of_sink_name(n, "reset_pin");
      if (!reset.is_invalid()) {
        add_reset(reset, const_true(gu::get_driver_of_sink_name(n, "negreset")));
      }
      continue;
    }
    if (op == Ntype_op::Memory) {
      // tolg normalizes a Memory's reset sink to active high before it reaches
      // the graph, including an explicitly active-low source reset.
      add_reset(gu::get_driver_of_sink_name(n, "reset"), false);
      continue;
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    // Body-less stateless primitives, exempt for the same reason as in the
    // clock walk above: a null `lgassert`/`fproperty` body is not an unwalkable
    // subtree, so it must not turn the whole interface INCOMPLETE.
    if (auto sio = n.get_subnode_io();
        sio != nullptr && (sio->get_name() == gu::lgassert_module_name || sio->get_name() == gu::fproperty_module_name)) {
      continue;
    }
    const auto  child_graph = n.get_subnode_graph();
    const auto& child       = reset_input_ports(child_graph, cache);
    result.complete         = result.complete && child.complete;
    for (const auto& rp : child.ports) {
      bool found = false;
      for (const auto& e : n.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != rp.port_id) {
          continue;
        }
        add_reset(e.driver, rp.active_low);
        found = true;
        break;
      }
      if (!found) {
        result.complete = false;
      }
    }
  }

  std::sort(result.ports.begin(), result.ports.end(), [](const Reset_input_port& a, const Reset_input_port& b) {
    return std::pair{a.port_id, a.active_low} < std::pair{b.port_id, b.active_low};
  });
  result.ports.erase(std::unique(result.ports.begin(), result.ports.end()), result.ports.end());
  cache.reset_busy.erase(def.get());
  return cache.reset_memo.emplace(def.get(), std::move(result)).first->second;
}

int inline_clock_gate_cells(hhds::Graph* g, std::string_view from_pass, const std::function<bool(const hhds::Graph*)>& is_boxed) {
  if (g == nullptr) {
    return 0;
  }
  absl::flat_hash_set<hhds::Class_index> clock_drivers;
  // Mark every node on the short identity path back to the cell that produced
  // a clock. Activation gating may already have interposed a Clock_cell; hop
  // through its clk_ref so the design's own ICG instance remains visible.
  const auto                             mark_clock_net = [&](hhds::Pin_class d) {
    for (int hops = 0; hops < 16 && !d.is_invalid(); ++hops) {
      if (gu::is_graph_input_pin(d) || d.is_const()) {
        break;
      }
      auto       dn = d.get_master_node();
      const auto op = gu::type_op_of(dn);
      clock_drivers.insert(dn.get_class_index());
      if (op == Ntype_op::Clock_cell) {
        d = gu::get_driver_of_sink_name(dn, "clk_ref");
        continue;
      }
      if (op != Ntype_op::Get_mask && op != Ntype_op::Sext) {
        break;
      }
      d = gu::first_value_driver(dn);
    }
  };

  // 1. Collect the nets that drive local state.
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      gu::for_each_memory_clock_driver(n, mark_clock_net);
      continue;
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      mark_clock_net(gu::get_driver_of_sink_name(n, "clock_pin"));
    }
  }
  // ...AND the pins that drive a CHILD's clock PORT. A gate whose output crosses
  // into a sub-instance is just as much a clock driver as one wired to a local
  // flop — it is simply one definition away — and collecting only the local case
  // left minion's whole vpu/txfma cone with an unfoldable gate.
  Clock_port_cache port_cache;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    const auto  child_graph = n.get_subnode_graph();
    const auto& clk_pids    = clock_input_ports(child_graph, port_cache);
    if (clk_pids.empty()) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      if (clk_pids.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
        mark_clock_net(e.driver);
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
  if (clock_pin.is_invalid() || clock_pin.is_const() || gu::is_graph_input_pin(clock_pin)) {
    return std::nullopt;
  }
  // Descend the boolean-SHAPING wrappers first. A flop's clock_pin is usually
  // the `And` itself, but a LATCH's enable arrives wrapped: tolg lowers
  // `if !clk and en { … }` to `enable = Mux(cone, 0, 1)`, so requiring the
  // driver to BE an And silently missed every Pyrope/slang gated latch. Any
  // inversion picked up on the way folds into the gate's edge below.
  const Phase outer = resolve_phase(clock_pin);
  if (outer.net.is_invalid() || outer.net.is_const() || gu::is_graph_input_pin(outer.net)) {
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
    if (e.driver.is_const()) {
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
    // input) are roots, and an intermediate gate output is neither — so without
    // recursing here the inner gate is filed as an ENABLE, the cone comes out
    // with ZERO clock operands, and the whole chain is refused as "some other
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
  if (const auto d = gu::get_driver_of_sink_name(n, "div"); d.is_const()) {
    const auto& dv = gu::const_of(d);
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

// Peel the WIDTH MASK the Pyrope round trip puts on a one-bit output: it spells
// the width as `(value & 1)`. That outer And is a mask, not the clock gate
// itself; left in place it has one real operand and every strict two-arm gate
// test below rejects the cell (Minion's prim_clk_gate included). Peel only a
// known-one mask with exactly one non-constant operand -- the real gate has two
// and therefore remains the anchor. Shared by `match_icg_def` (the def-body
// matcher) and `state_free_gate_cone` (the latch-less wrapper), which must not
// disagree about what the anchor is.
hhds::Pin_class peel_width_mask(hhds::Pin_class inner) {
  for (int hops = 0; hops < 4 && !inner.is_invalid() && !inner.is_const() && !gu::is_graph_input_pin(inner); ++hops) {
    auto node = inner.get_master_node();
    if (gu::type_op_of(node) != Ntype_op::And) {
      break;
    }
    hhds::Pin_class value;
    int             values = 0;
    bool            mask   = true;
    for (const auto& e : node.inp_edges()) {
      if (e.driver.is_const()) {
        mask = mask && const_is(e.driver, 1);
      } else if (!e.driver.is_invalid()) {
        value = e.driver;
        ++values;
      }
    }
    if (!mask || values != 1) {
      break;
    }
    inner = value;
  }
  return inner;
}

// A def INPUT pin -> the parent pin driving that instance sink. Invalid when
// `def_in` is not a graph input of the def, or when the parent left that port
// dangling: only a port the PARENT already drives can be re-rooted read-only.
hhds::Pin_class parent_driver_of(const hhds::Node_class& inst, const std::shared_ptr<hhds::GraphIO>& gio,
                                 const hhds::Pin_class& def_in) {
  if (def_in.is_invalid() || !gio || !gu::is_graph_input_pin(def_in)) {
    return {};
  }
  const auto nm = gu::pin_name_of(def_in);
  for (const auto& d : gio->get_input_pin_decls()) {
    if (d.name != nm) {
      continue;
    }
    for (const auto& e : inst.inp_edges()) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) == static_cast<uint32_t>(d.port_id)) {
        return e.driver;
      }
    }
    break;
  }
  return {};
}

// An INSTANTIATED gate cell, re-rooted onto the parent READ-ONLY.
//
// The flop's `clock_pin` is a `Sub` output, so the local cone walk necessarily
// stops at the boundary. `root_port` above already decodes the def structurally
// to find the clock ROOT; this adds the ENABLE it deliberately discards, which
// is the whole reason a consumer used to have to INLINE the cell first.
//
// Doing it here rather than in each consumer is the point: `clock_op_of` is the
// ONE recognizer, so lec, the phase schedule and inou.cgen.sim all gain the
// hierarchical answer from this single place -- and pass.synth, which never
// asks, keeps its gate cells intact and as high in the hierarchy as they were
// written (a materializing PASS would have to be explicitly kept out of the
// synth recipe; a query is inert by construction).
//
// LIMIT, refused BY NAME rather than approximated: the def's enable must reduce
// to one of the def's own INPUT PORTS, because only then does it name a pin the
// PARENT already drives. `materialize_clock_cells` handles a deeper in-def cone
// by CLONING it into the caller (Cone_cloner) -- manufacturing the parent pin
// that does not otherwise exist. A reader cannot do that, and must not.
std::optional<Icg_cone> clock_op_depth(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth);

// The STATE-FREE gate flavour: a plain `assign clk_o = clk_i & en;` wrapper is a
// clock gate too, and refusing it leaves the flop it clocks with an unfoldable
// "derived clock" for want of a latch nobody wrote. `inline_clock_gate_cells`
// recognized this arm by INLINING any state-free callee and letting the local
// matcher see the resulting And; read-only, the shape has to be matched in the
// def and re-rooted instead.
//
// `match_icg_def` cannot do this on its own: with no latch there is no
// transparency window to say WHICH operand is the clock, and a def body holds no
// flop to ask. Here in the PARENT that question is answerable --
// `Design_clocks::is_clock` is the same discriminator `resolve_icg` uses locally
// (and the very confusion its keying guards against: picking the ENABLE of a
// `clk & en` as the clock operand).
std::optional<Icg_cone> state_free_gate_cone(const hhds::Pin_class& clock_pin, const hhds::Node_class& inst, hhds::Graph* def,
                                             const std::shared_ptr<hhds::GraphIO>& gio, const Design_clocks& clocks, int depth);

std::optional<Icg_cone> sub_icg_cone(const hhds::Pin_class& clock_pin, const Design_clocks& clocks, int depth) {
  if (clock_pin.is_invalid() || clock_pin.is_const() || gu::is_graph_input_pin(clock_pin)) {
    return std::nullopt;
  }
  auto inst = clock_pin.get_master_node();
  if (inst.is_invalid() || gu::type_op_of(inst) != Ntype_op::Sub) {
    return std::nullopt;
  }
  auto def = inst.get_subnode_graph();
  auto gio = inst.get_subnode_io();
  if (!def || !gio) {
    return std::nullopt;  // body-less blackbox: nothing to recognize
  }
  auto gate = match_icg_def(def.get());
  if (!gate) {
    return state_free_gate_cone(clock_pin, inst, def.get(), gio, clocks, depth);
  }

  // The def output this pin actually reads must be the gate's clock output; an
  // ICG with a second output read elsewhere is not gating THIS pin.
  {
    bool is_gate_out = false;
    for (const auto& d : gio->get_output_pin_decls()) {
      if (d.name == gu::pin_name_of(gate->out)) {
        is_gate_out = static_cast<uint32_t>(clock_pin.get_port_id()) == static_cast<uint32_t>(d.port_id);
        break;
      }
    }
    if (!is_gate_out) {
      return std::nullopt;
    }
  }

  auto clk_src = parent_driver_of(inst, gio, gate->clk_in);
  auto en_src  = parent_driver_of(inst, gio, gate->enable_cone);
  if (clk_src.is_invalid() || en_src.is_invalid()) {
    return std::nullopt;  // in-def enable cone: the caller refuses by name
  }

  Icg_cone cone;
  cone.div = 1;
  cone.enables.push_back(en_src);

  // The parent clock may itself be gated: one guard per cell down to the root,
  // exactly as the materialized-cell branch below does. That path resolves
  // clk_src's own phase internally, so it must NOT be pre-folded here.
  if (depth < kMaxGateChain) {
    if (auto inner = clock_op_depth(clk_src, clocks, depth + 1)) {
      Icg_cone chained = cone;
      absorb_chain(chained, *inner, /*inverted=*/false);
      return chained;
    }
  }
  // `Icg_cone::clock` is contracted to be the clock operand RESOLVED TO ITS
  // ROOT, with any inversion picked up on the way recorded in `clock_inverted`
  // -- exactly what `resolve_icg_depth` and `clock_cell_cone` do for the two
  // other entry points. Handing back the raw instance driver instead would ship
  // a `~clk`-fed gate cell as an un-inverted cone: `Cgen_sim::icg_guards` gates
  // rise-vs-fall commit on `clock_inverted` alone, so the guards would fold into
  // the RISE pass and every flop behind that cell would commit half a period
  // early -- the same silent negedge-vs-posedge loss resolve_icg_depth calls out.
  const Phase cph     = resolve_phase(clk_src);
  cone.clock          = cph.net.is_invalid() ? clk_src : cph.net;
  cone.clock_inverted = cph.inverted;
  return cone;
}

std::optional<Icg_cone> state_free_gate_cone(const hhds::Pin_class& clock_pin, const hhds::Node_class& inst, hhds::Graph* def,
                                             const std::shared_ptr<hhds::GraphIO>& gio, const Design_clocks& clocks, int depth) {
  if (def == nullptr || !gio) {
    return std::nullopt;
  }
  // Entirely state-free, and no nested Sub (opaque here, so treat it as state) --
  // the same test the inliner applied before it absorbed such a callee.
  for (auto dn : def->body().nodes()) {
    const auto dop = gu::type_op_of(dn);
    if (dop == Ntype_op::Latch || dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Memory
        || dop == Ntype_op::Sub) {
      return std::nullopt;
    }
  }
  // The def output this pin reads.
  std::string oname;
  for (const auto& d : gio->get_output_pin_decls()) {
    if (static_cast<uint32_t>(d.port_id) == static_cast<uint32_t>(clock_pin.get_port_id())) {
      oname = d.name;
      break;
    }
  }
  if (oname.empty()) {
    return std::nullopt;
  }
  auto opin = def->get_output_pin(oname);
  if (opin.is_invalid()) {
    return std::nullopt;
  }
  hhds::Pin_class inner;
  for (const auto& e : opin.inp_edges()) {
    inner = e.driver;
    break;
  }
  inner = peel_width_mask(inner);
  if (inner.is_invalid() || inner.is_const() || gu::is_graph_input_pin(inner)) {
    return std::nullopt;
  }
  auto gate_node = inner.get_master_node();
  if (gu::type_op_of(gate_node) != Ntype_op::And) {
    return std::nullopt;
  }
  // Exactly two non-constant operands, and BOTH must be def input ports: only
  // then does each name a pin the parent already drives. Every CONSTANT operand
  // must be an all-ones width mask -- `clk & en & 0` is a tied-off cell whose
  // output never moves, and reporting it as a live gate on `clk` would hand a
  // consumer a commit class the netlist does not have.
  std::vector<hhds::Pin_class> operands;
  for (const auto& e : gate_node.inp_edges()) {
    if (e.driver.is_const()) {
      if (!const_is(e.driver, 1) && !const_is(e.driver, -1)) {
        return std::nullopt;
      }
      continue;
    }
    if (e.driver.is_invalid() || !gu::is_graph_input_pin(e.driver)) {
      return std::nullopt;
    }
    operands.push_back(e.driver);
  }
  if (operands.size() != 2) {
    return std::nullopt;
  }
  auto p0 = parent_driver_of(inst, gio, operands[0]);
  auto p1 = parent_driver_of(inst, gio, operands[1]);
  if (p0.is_invalid() || p1.is_invalid()) {
    return std::nullopt;
  }
  // Which side is the clock? Ask the design, never the port name.
  const bool c0 = clocks.is_clock(p0);
  const bool c1 = clocks.is_clock(p1);
  if (c0 == c1) {
    return std::nullopt;  // neither or both: not a gate we can name
  }
  Icg_cone cone;
  cone.clock = c0 ? p0 : p1;
  cone.div   = 1;
  cone.enables.push_back(c0 ? p1 : p0);
  if (depth < kMaxGateChain) {
    if (auto chained_inner = clock_op_depth(cone.clock, clocks, depth + 1)) {
      Icg_cone chained = cone;
      absorb_chain(chained, *chained_inner, /*inverted=*/false);
      return chained;
    }
  }
  return cone;
}

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
  if (!outer.net.is_invalid() && !gu::is_graph_input_pin(outer.net) && !outer.net.is_const()) {
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
  // An INSTANTIATED gate: recognized read-only and re-rooted onto the parent,
  // so no consumer has to inline the cell to see its enable.
  if (auto sub = sub_icg_cone(clock_pin, clocks, depth)) {
    return sub;
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
    inner = peel_width_mask(inner);  // see the helper: `(value & 1)` is a width mask, not the gate
    if (inner.is_invalid() || inner.is_const() || gu::is_graph_input_pin(inner)) {
      continue;
    }
    const Phase op_ph = resolve_phase(inner);
    if (op_ph.net.is_invalid() || gu::is_graph_input_pin(op_ph.net) || op_ph.net.is_const()) {
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
      if (e.driver.is_invalid() || e.driver.is_const()) {
        continue;  // a width mask, not an operand
      }
      const Phase ph = resolve_phase(e.driver);
      if (!ph.net.is_invalid() && gu::is_graph_input_pin(ph.net)) {
        ++n_ports;
        clk_port = ph.net;
        continue;
      }
      if (!ph.net.is_invalid() && !ph.net.is_const() && gu::type_op_of(ph.net.get_master_node()) == Ntype_op::Latch) {
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
    // transparent on the opposite phase of the same port. Conditional-call
    // lowering may additionally qualify that enable with __valid, spelling the
    // cone as `(!clk) & __valid`; search an And-only qualifier cone instead of
    // requiring the whole expression to reduce to clk. Without the clock-phase
    // leaf check a module that ANDs a clock with an unrelated latched data bit
    // would match and sample its "enable" at an arbitrary time.
    auto       latch_node              = latched.get_master_node();
    const auto latch_enable            = gu::get_driver_of_sink_name(latch_node, "enable");
    // EVERY leaf of that And cone is checked, not just "one of them is the
    // clock phase": the match result carries only `latch_transparent_arm`, so a
    // qualifier operand is DROPPED, and the Clock_cell built from it re-samples
    // its enable on every reference edge instead of holding while the qualifier
    // is low. A vendor/DFT gate spelled `(!clk) & test_en` would therefore tick
    // its gated clock on cycles the RTL holds, advancing downstream registers a
    // cycle early -- wrong state, silently. Only the activation spelling this
    // relaxation exists for (`(!clk) & __valid`, whose guard the caller-side
    // Clock_cell already carries) is accepted; anything else falls back to the
    // ordinary latch model, which is exact.
    const auto is_activation_qualifier = [&](const hhds::Pin_class& pin) {
      const auto root = control_root(pin).net;
      return !root.is_invalid() && gu::is_graph_input_pin(root) && gu::pin_name_of(root) == "__valid";
    };
    std::function<bool(hhds::Pin_class, int, bool&)> scan_enable;
    scan_enable = [&](hhds::Pin_class pin, int depth, bool& saw_clock) {
      if (pin.is_invalid() || depth > 16) {
        return false;
      }
      const auto phase = resolve_phase(pin);
      if (!phase.net.is_invalid() && phase.net.get_class_index() == clk_port.get_class_index()
          && phase.inverted == (gate_op == Ntype_op::And)) {
        saw_clock = true;
        return true;
      }
      if (pin.is_const()) {
        return const_is(pin, 1) || const_is(pin, -1);  // an always-true And operand changes nothing
      }
      if (!gu::is_graph_input_pin(pin) && gu::type_op_of(pin.get_master_node()) == Ntype_op::And) {
        for (const auto& edge : pin.get_master_node().inp_edges()) {
          if (!scan_enable(edge.driver, depth + 1, saw_clock)) {
            return false;
          }
        }
        return true;
      }
      return is_activation_qualifier(pin);
    };
    bool saw_clock_phase = false;
    if (!scan_enable(latch_enable, 0, saw_clock_phase) || !saw_clock_phase) {
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
    if (d.is_const()) {
      return gu::create_const(*parent_, gu::const_of(d));
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
      if (e.driver.is_const()) {
        return gu::create_const(*parent_, gu::const_of(e.driver));
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
    if (p.is_const()) {
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
  // whose gate is its ENABLE (a latch has no clock_pin), so the scan below
  // finds nothing and the cell looks like it has
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
      if (gu::is_graph_input_pin(d) || d.is_const()) {
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

namespace {

[[nodiscard]] bool same_pin(const hhds::Pin_class& a, const hhds::Pin_class& b) {
  return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
}

// The 1-bit driver of an EXISTING `op` node whose operands are exactly {a, b},
// or an invalid pin when there is none.
//
// WHY THE BUILDERS BELOW REUSE INSTEAD OF MINTING. gate_activation_clocks is
// idempotent by PIN IDENTITY: it decides a clock is already gated by comparing
// the cell's `en` against the enable it just built. A freshly created cone
// matches nothing, so a second lowering of the same graph wrapped an
// already-gated clock in another Clock_cell (and orphaned the cone it had
// speculatively built) -- a nested gate chain whose structural digest differs
// between a once- and twice-lowered build of the same source.
[[nodiscard]] hhds::Pin_class existing_binop(Ntype_op op, const hhds::Pin_class& a, const hhds::Pin_class& b) {
  if (a.is_invalid() || b.is_invalid()) {
    return hhds::Pin_class{};
  }
  for (const auto& e : a.out_edges()) {
    auto n = e.sink.get_master_node();
    if (gu::type_op_of(n) != op) {
      continue;
    }
    const auto ins = n.inp_edges();
    if (ins.size() != 2) {
      continue;
    }
    if (!((same_pin(ins[0].driver, a) && same_pin(ins[1].driver, b))
          || (same_pin(ins[0].driver, b) && same_pin(ins[1].driver, a)))) {
      continue;
    }
    // A wider result over the same two operands is design data, not this
    // pass's boolean shape, and must not be narrowed into an enable.
    if (auto out = n.create_driver_pin(0); gu::bits_of(out) == 1) {
      return out;
    }
  }
  return hhds::Pin_class{};
}

[[nodiscard]] hhds::Pin_class logical_not1(hhds::Graph* g, const hhds::Pin_class& a) {
  auto zero = gu::create_const(*g, *Dlop::create_integer(0));
  if (auto found = existing_binop(Ntype_op::EQ, a, zero); !found.is_invalid()) {
    return found;
  }
  auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
  a.connect_sink(eq.create_sink_pin(0));
  zero.connect_sink(eq.create_sink_pin(0));
  auto out = eq.create_driver_pin(0);
  gu::set_bits(out, 1);
  return out;
}

[[nodiscard]] hhds::Pin_class nonzero1(hhds::Graph* g, const hhds::Pin_class& a) {
  if (a.is_invalid() || a.is_const() || gu::bits_of(a) <= 1) {
    return a;
  }
  return logical_not1(g, logical_not1(g, a));
}

[[nodiscard]] hhds::Pin_class logical_or1(hhds::Graph* g, const hhds::Pin_class& a, const hhds::Pin_class& b) {
  if (a.is_invalid()) {
    return b;
  }
  if (b.is_invalid()) {
    return a;
  }
  if (auto found = existing_binop(Ntype_op::Or, a, b); !found.is_invalid()) {
    return found;
  }
  auto op = gu::create_typed_node(*g, Ntype_op::Or);
  a.connect_sink(op.create_sink_pin(0));
  b.connect_sink(op.create_sink_pin(0));
  auto out = op.create_driver_pin(0);
  gu::set_bits(out, 1);
  return out;
}

// Does the cone driving `start` contain `target` (an activation guard's
// control root)? A bounded reverse walk that -- unlike comb_reach above --
// CROSSES state and instance boundaries on purpose: the MATERIALIZED form of
// an activation gate keeps its enable behind a transparent latch
// (`clk & latch(__valid|reset)` closing at the rise), and an instantiated ICG
// cell hides the same cone behind a Sub, so stopping at either boundary would
// un-recognize exactly the already-gated shapes this test exists to skip. The
// walk answers reachability only -- never phase or polarity -- and a cone
// larger than the cap answers false, which falls back to gating (the historic
// behavior) rather than silently withholding a gate the state needs.
//
// The seed itself does not count as a hit: a clock port WIRED to the guard
// net is not evidence of a gate, only a guard folded INSIDE the clock's
// derivation is.
[[nodiscard]] bool cone_reaches(const hhds::Pin_class& start, const hhds::Pin_class& target) {
  if (start.is_invalid() || target.is_invalid()) {
    return false;
  }
  constexpr size_t                       visit_cap = 512;
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid()) {
      continue;
    }
    if (!same_pin(p, start) && same_pin(p, target)) {
      return true;
    }
    if (p.is_const() || gu::is_graph_input_pin(p)) {
      continue;
    }
    if (!seen.insert(p.get_class_index()).second) {
      continue;
    }
    if (seen.size() > visit_cap) {
      return false;
    }
    for (const auto& e : p.get_master_node().inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return false;
}

}  // namespace

int gate_activation_clocks(hhds::Graph* g, std::string_view from_pass, Clock_port_cache& cache) {
  if (g == nullptr) {
    return 0;
  }

  std::vector<hhds::Node_class> instances;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      instances.push_back(n);
    }
  }

  int done = 0;
  for (const auto& inst : instances) {
    auto def = inst.get_subnode_graph();
    auto io  = inst.get_subnode_io();
    if (!def || !io) {
      continue;
    }

    absl::flat_hash_map<uint32_t, std::string>     names;
    absl::flat_hash_map<uint32_t, hhds::Pin_class> bound;
    for (const auto& d : io->get_input_pin_decls()) {
      names.emplace(static_cast<uint32_t>(d.port_id), d.name);
    }
    for (const auto& e : inst.inp_edges()) {
      bound.emplace(static_cast<uint32_t>(e.sink.get_port_id()), e.driver);
    }

    hhds::Pin_class guard;
    for (const auto& [pid, name] : names) {
      if (name == "__valid") {
        if (auto it = bound.find(pid); it != bound.end()) {
          guard = it->second;
        }
        break;
      }
    }
    if (guard.is_invalid()) {
      continue;
    }
    if (guard.is_known_true()) {
      continue;
    }
    // An unconditional child inside an activation-capable definition receives
    // that definition's own __valid input. Its parent boundary already gates
    // the root clock (and opens it for reset), so wrapping every forwarded
    // occurrence only builds redundant nested Clock_cells. A genuinely local
    // path guard is an And/Mux cone and does not reduce to this input root.
    const auto guard_root = control_root(guard).net;
    if (!guard_root.is_invalid() && gu::is_graph_input_pin(guard_root) && gu::pin_name_of(guard_root) == "__valid") {
      continue;
    }

    const auto&           clocks = clock_input_interface(def, cache);
    std::vector<uint32_t> clock_ports(clocks.ports.begin(), clocks.ports.end());
    std::sort(clock_ports.begin(), clock_ports.end());
    if (!clocks.complete) {
      // Fail LOUD, never by omission: this phase replaced tolg's name-based
      // gate, so an instance we decline to gate here is one whose state
      // advances on every cycle while __valid is false -- a silent miscompile
      // in both the emitted RTL and sim, not a missed optimization.
      livehd::diag::err(from_pass, "activation-clock-structural", "time")
          .msg("conditional instance `{}` in `{}` has state whose clock cone is not reducible to a declared input",
               gu::debug_name(inst),
               g->get_name())
          .hint(
              "activation gating can only withhold a clock that reaches the callee through one of its input ports; an "
              "internally derived clock, a body-less callee or a mutually instantiating pair leaves nothing to gate")
          .emit();
      continue;
    }
    if (clock_ports.empty()) {
      continue;  // analyzed and stateless: nothing inside takes an edge, so there is nothing to withhold
    }

    const auto& resets = reset_input_ports(def, cache);
    if (!resets.complete) {
      livehd::diag::err(from_pass, "activation-reset-structural", "time")
          .msg("conditional instance `{}` in `{}` has reset state whose reset cone is not reducible to a declared input",
               gu::debug_name(inst),
               g->get_name())
          .hint("make the callee's synchronous reset derive directly (optionally through inversion) from one input port")
          .emit();
      continue;
    }
    if (resets.ports.size() > 1) {
      livehd::diag::err(from_pass, "activation-reset-ambiguous", "time")
          .msg("conditional instance `{}` in `{}` has {} structural reset inputs; its clock/reset domain mapping is ambiguous",
               gu::debug_name(inst),
               g->get_name(),
               resets.ports.size())
          .hint("a conditionally called module must expose at most one reset domain until per-clock reset mapping is represented")
          .emit();
      continue;
    }

    // RESOLVE the reset binding before minting anything. Every early exit past
    // this point must leave the graph untouched: the enable cone below CREATES
    // nodes, and a `continue` taken after building it leaves EQ/Or nodes with
    // no consumer -- speculative orphans on a phase whose whole contract is
    // idempotence.
    hhds::Pin_class reset_driver;
    bool            reset_active_low = false;
    if (!resets.ports.empty()) {
      const auto& rp = resets.ports.front();
      auto        it = bound.find(rp.port_id);
      if (it == bound.end() || it->second.is_invalid()) {
        livehd::diag::err(from_pass, "activation-reset-unbound", "time")
            .msg("conditional instance `{}` in `{}` leaves its structural reset input unbound", gu::debug_name(inst), g->get_name())
            .emit();
        continue;
      }
      reset_driver     = it->second;
      reset_active_low = rp.active_low;
    }

    // ROUND-TRIP idempotence (the second decline): a clock driver whose cone
    // is ALREADY a function of this activation guard IS the activation gate,
    // materialized as design text -- the emitted `clk & latch(__valid|reset)`
    // read back through a Verilog front end reaches the clock port through the
    // gate's own enable latch. Wrapping it again is not redundant but WRONG:
    // the fresh cell latches the guard ALONE (a re-read callee folds its sync
    // reset into a data mux, so reset_input_ports sees no reset port to OR
    // in), which withholds every reset edge while the call is inactive --
    // measured as the callee's state never leaving its power-on value
    // (conditional_state_call: ref=0, impl=255 after reset). The pin-identity
    // check inside the loop below cannot catch this: the re-read gate is body
    // logic, not a Clock_cell carrying this exact enable pin. Decided per
    // port BEFORE the enable cone is minted, so an all-ports-gated instance
    // leaves no speculative orphan nodes behind.
    const auto            guard_key = guard_root.is_invalid() ? guard : guard_root;
    std::vector<uint32_t> ports_to_gate;
    for (const auto pid : clock_ports) {
      auto it = bound.find(pid);
      if (it == bound.end() || it->second.is_invalid()) {
        continue;
      }
      if (cone_reaches(it->second, guard_key)) {
        continue;  // the design text already conditions this clock net on the guard
      }
      ports_to_gate.push_back(pid);
    }
    // No clock port needs (or has) a gate here, so the loop below would rewire
    // nothing. Building the enable anyway is the orphan case above.
    if (ports_to_gate.empty()) {
      continue;
    }

    hhds::Pin_class enable = nonzero1(g, guard);
    if (!reset_driver.is_invalid()) {
      auto reset_asserted = nonzero1(g, reset_driver);
      if (reset_active_low) {
        reset_asserted = logical_not1(g, reset_asserted);
      }
      enable = logical_or1(g, enable, reset_asserted);
    }

    for (const auto pid : ports_to_gate) {
      auto bit = bound.find(pid);
      if (bit == bound.end() || bit->second.is_invalid()) {
        continue;
      }

      // Idempotence without confusing a design-authored gate for this one:
      // only an existing cell carrying this exact activation/reset enable is
      // considered already inserted. Any other Clock_cell is wrapped, so the
      // two independent enables compose down the clock net.
      auto current = bit->second;
      if (!gu::is_graph_input_pin(current) && !current.is_const()
          && gu::type_op_of(current.get_master_node()) == Ntype_op::Clock_cell
          && same_pin(gu::get_driver_of_sink_name(current.get_master_node(), "en"), enable)) {
        continue;
      }

      auto cell = gu::create_typed_node(*g, Ntype_op::Clock_cell);
      current.connect_sink(gu::setup_sink_by_name(cell, "clk_ref"));
      gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_by_name(cell, "div"));
      enable.connect_sink(gu::setup_sink_by_name(cell, "en"));
      gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(cell, "invert"));
      auto out = cell.create_driver_pin(0);
      gu::set_bits(out, 1);

      for (auto e : inst.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != pid) {
          continue;
        }
        auto sink = e.sink;
        e.del_edge();
        out.connect_sink(sink);
        break;
      }
      ++done;
    }
  }

  if (done > 0) {
    livehd::diag::info(from_pass, "activation-clock-gated", "progress")
        .msg("{}: gated {} conditionally-called instance clock port(s) in `{}`", from_pass, done, g->get_name())
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
