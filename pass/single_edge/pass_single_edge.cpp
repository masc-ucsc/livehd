// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_single_edge.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

namespace livehd::single_edge {

namespace {

constexpr std::string_view kPass = "pass.single_edge";

// The synthesized phase register's name lives in node_util.hpp because
// pass/lec/query.cpp needs the SAME spelling for its keep-init exception (a
// free phase lets the solver pick the wrong sub-step parity). It must survive
// canon_flop_name IDENTICALLY on both sides of a miter, or it lands in the
// per-side fallback instead of shared_inputs and the two sides start from
// independent phases — the failure mode `clk_prev_` had to be fixed for.
constexpr std::string_view kPhaseName = gu::single_edge_phase_name;

// ---------------------------------------------------------------------------
// Slot assignment
//
// The commit-class -> slot table is exactly M3's landed classifier plus a
// choice of reference clock. It is derived STRUCTURALLY (from the (net, edge)
// pair), never from node or traversal order: the pass runs independently on
// both sides of a miter and a slot disagreement is a false REFUTED.
//
//   ref clock RISE  -> slot 0
//   ref clock FALL  -> slot 1
//   data-gated latch -> the slot of the state its ENABLE cone depends on
//                       (slot 0 when the enable is input-driven)
//
// Coincident (net, edge) => the SAME slot, by construction: the slot is a pure
// function of the class key. That is the soundness precondition condition 5
// names; what it does NOT give is the L1 case, handled by rule 4 below.
struct Element {
  hhds::Node_class node;
  lc::Commit_class cc;
  bool             is_latch = false;
  int              slot     = 0;
  // A decoded `<clock> & <enables>` clock cone. Present => this element's
  // gated clock is rewritten into its ENABLE (see apply_icg): it commits on the
  // reference clock's edge iff the enables hold, which is exactly what the
  // encoder models natively and strictly cheaper than a per-step commit term.
  std::optional<lc::Icg_cone> icg;
};

// Backward combinational reach to STATE elements (stops at any register), so a
// data-gated latch can inherit the slot of whatever drives its enable.
void comb_state_reach(const hhds::Pin_class& start, absl::flat_hash_set<hhds::Class_index>& hit) {
  if (start.is_invalid()) {
    return;
  }
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  while (!work.empty()) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || gu::is_const_pin(p) || gu::is_graph_input_pin(p)) {
      continue;
    }
    if (!seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n = p.get_master_node();
    if (gu::is_type_register(n)) {
      hit.insert(n.get_class_index());
      continue;
    }
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      continue;  // opaque instance
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
}

hhds::Pin_class sink_driver(const hhds::Node_class& n, std::string_view pin) {
  return gu::get_driver_of_sink_name(n, pin);
}

void drop_sink(const hhds::Node_class& n, std::string_view pin) {
  const auto pid = Ntype::get_sink_pid(gu::type_op_of(n), pin);
  if (pid == livehd::Port_invalid) {
    return;
  }
  std::vector<hhds::Edge_class> doomed;
  for (const auto& e : n.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      doomed.push_back(e);
    }
  }
  for (const auto& e : doomed) {  // never delete while iterating (hhds rule)
    e.del_edge();
  }
}

std::string label_of(const hhds::Node_class& n) { return gu::debug_name(n); }

void refuse(bool quiet, std::string_view code, const std::string& msg, std::string_view hint) {
  if (quiet) {
    return;
  }
  livehd::diag::err(kPass, code, "unsupported").msg("{}", msg).hint(hint).emit();
}

// ---------------------------------------------------------------------------

// The ENABLE-LATCH BYPASS, resolved during ANALYSIS because the rewrite loop
// retypes latches into flops as it goes -- so by the time it reached a gated
// element the enable latch might already be a Flop and the bypass would
// silently not fire, leaving the enable a full cycle late. (Measured exactly
// that way: the emitted netlist came out `if (enl)`, the latch's held Q,
// instead of the value it was passing through.)
//
// A real ICG latches its enable on the opposite phase purely to suppress
// glitches: the latch closes exactly when the element samples, so the element
// sees what the latch was passing THROUGH. Reading its Q instead is the classic
// L1 error.
//
// `latch_transparent_arm` only recognizes tolg's HOLD-MUX shape (`gate ? d : q`
// -> `d`). A latch whose `din` is the raw D -- the yosys D/EN shape, and also
// what Pyrope emits for a plain `if !clk { enl = en }` where `en` is a module
// input -- has no mux, so the arm comes back invalid and the bypass would
// silently not fire (measured as `if (enl)` in the emitted netlist, 19/29
// cycles mismatching the source under iverilog). latch_contract.cpp's own
// ICG-def matcher already falls back this way; do the same here.
//
// ONE helper for flops and memories, on purpose: a parity or a bypass decided in
// two places is how the two drift.
void bypass_enable_latches(std::vector<hhds::Pin_class>& ens) {
  for (auto& en : ens) {
    if (en.is_invalid() || gu::is_const_pin(en) || gu::is_graph_input_pin(en)) {
      continue;
    }
    auto dn = en.get_master_node();
    if (gu::type_op_of(dn) != Ntype_op::Latch) {
      continue;
    }
    auto arm = lc::latch_transparent_arm(dn);
    if (arm.is_invalid()) {
      arm = gu::get_driver_of_sink_name(dn, "din");  // raw D/EN shape
    }
    if (!arm.is_invalid()) {
      en = arm;
    }
  }
}

// A clocked `Memory` cell under a phase divider. Its WRITE ports -- and, for a
// sync-read memory, the read-data registers its read enables capture -- commit
// on an edge and so own a slot exactly like a flop. A combinational array
// (`type == 2`) has no edge and is recorded only so the rewrite skips it
// knowingly.
struct Mem_element {
  hhds::Node_class node;
  lc::Commit_class cc;        // the one (net, edge) its clocked ports commit on
  int              slot = 0;
  int64_t          type = 0;  // graph/cell.cpp pid 7: 0 async-read, 1 sync-read, 2 array
  // A port clocked through an ICG cone (`<clock> & <enables>`): its commit class
  // is the CLOCK's, and the enables become part of that port's `enable` in the
  // rewrite -- the same fold a gated flop gets (see apply of `Element::icg`).
  std::vector<std::pair<hhds::Port_id, std::vector<hhds::Pin_class>>> icg_enables;
};

// The Memory cell's per-port pins live at raw pid `port * Memory_port_stride +
// base`, the cell-global ones at `base` alone (graph/cell.hpp). These read one
// by RAW pid; `get_driver_of_sink_name` cannot, because a per-port name is
// spelled "<pid><base>" and the helper asserts a single driver besides.
hhds::Pin_class mem_sink_driver(const hhds::Node_class& n, hhds::Port_id raw_pid) {
  for (const auto& e : n.inp_edges()) {
    if (e.sink.get_port_id() == raw_pid) {
      return e.driver;
    }
  }
  return {};
}

std::optional<int64_t> mem_sink_const(const hhds::Node_class& n, hhds::Port_id raw_pid) {
  auto d = mem_sink_driver(n, raw_pid);
  if (d.is_invalid() || !gu::is_const_pin(d)) {
    return std::nullopt;
  }
  auto v = gu::hydrate_const(d);
  if (!v.is_just_i64()) {
    return std::nullopt;
  }
  return v.to_just_i64();
}

struct Plan {
  std::vector<Element>            elems;
  std::vector<Mem_element>        mems;
  absl::flat_hash_map<std::string, int> slot_of_key;
  int                             slots = 1;
  bool                            ok    = false;
  std::string                     ref_net;  // class key of the reference clock ("" = none)
  std::string                     why;   // failure reason when !ok
  std::string                     code;  // diagnostic code when !ok
  hhds::Pin_class                 ref_clk_pin;  // the reference clock net (may be invalid = implicit)
};

Plan build_plan(hhds::Graph* g, const lc::Design_clocks& clocks) {
  Plan plan;

  // 1. Collect every state element with its commit class.
  absl::flat_hash_map<std::string, int> per_net;  // net_key -> #elements on it (clock role only)
  for (auto n : g->fast_class()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      // A clocked memory commits on an edge exactly like a flop, but it is not a
      // `plan.elems` entry: it does not vote for the reference clock (a design's
      // time base is set by its flops), and its commit class is read from the
      // cell once the reference is known -- step 4 below. Under a divider its
      // committing ports are slot-gated in the rewrite (`Plan::mems`).
      continue;
    }
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch) {
      continue;
    }
    // A recognized CLOCK GATE (`Clock_cell`) in the control cone is NOT modelled
    // here. This pass folds an INLINE `<clock> & <enables>` cone into the flop's
    // enable (resolve_icg below), but a materialized cell carries the
    // glitch-free SAMPLING CONTRACT -- the enable is sampled on the inactive
    // phase before the cell's own active edge, which for the active-low flavour
    // (`clk | ~en`) is the phase before the FALL. There is no sub-period in this
    // lowering to sample in, so slotting the endpoint would silently drop the
    // gate (commit every slot, ungated) or read it half a period early. Decline
    // and let the formal phase schedule own it -- it models the sample point
    // explicitly (todo/livehd/2f-lec, 2f-latch M10).
    if (auto cr = lc::control_root(sink_driver(n, op == Ntype_op::Latch ? "enable" : "clock_pin"),
                                   /*stop_at_clock_cell=*/true);
        !cr.net.is_invalid() && !gu::is_graph_input_pin(cr.net) && !gu::is_const_pin(cr.net)
        && gu::type_op_of(cr.net.get_master_node()) == Ntype_op::Clock_cell) {
      plan.code = "clock-cell-unsupported";
      plan.why  = "state element `" + label_of(n)
                + "` is clocked through a recognized clock gate (Clock_cell), whose enable sample point has no "
                  "representation in a slot lowering";
      return plan;
    }
    auto cc = lc::commit_class_of(n, &clocks);
    if (!cc) {
      plan.code = "unresolved-commit-class";
      plan.why  = "state element `" + label_of(n) + "` has a control cone that does not resolve to a root net";
      return plan;
    }
    Element e;
    e.node     = n;
    e.cc       = *cc;
    e.is_latch = op == Ntype_op::Latch;
    {
      // A flop's gate lives on `clock_pin`; a LATCH's lives on `enable` (its
      // gate IS its enable). Same cone, same fold: the clock half becomes the
      // commit class, the data half becomes the flop enable.
      e.icg = lc::resolve_icg(sink_driver(n, e.is_latch ? "enable" : "clock_pin"), clocks);
      if (e.icg) {
        bypass_enable_latches(e.icg->enables);
      }
    }
    plan.elems.push_back(e);
    if (cc->role == lc::Net_role::Clock) {
      ++per_net[cc->net_key()];
    }
  }
  if (plan.elems.empty()) {
    plan.ok    = true;
    plan.slots = 1;
    return plan;
  }

  // 2. Reference clock = the clock net carrying the most state. Ties break on
  //    the KEY (a string), never on traversal order, so both miter sides agree.
  std::string ref_net;
  int         best = -1;
  for (const auto& [k, cnt] : per_net) {
    if (cnt > best || (cnt == best && k < ref_net)) {
      best    = cnt;
      ref_net = k;
    }
  }
  if (per_net.size() >= 2) {
    // A second clock DOMAIN. Its ratio to the reference is unknown here (a
    // free, testbench-driven clock genuinely has none), and slotting it anyway
    // would silently change what the design means. Decline: pass/lec's
    // detected-edge branch and sim's M6 model both still handle it honestly.
    plan.code = "multi-clock-no-ratio";
    plan.why  = std::format("{} clock nets and no known integer ratio between them", per_net.size());
    return plan;
  }
  // The reference clock is the RESOLVED ROOT of the class, not the raw
  // clock_pin driver. They differ whenever the cone carries an inversion or a
  // width mask (`always @(posedge ~clk)` arrives as `And(Not(Get_mask(clk)),1)`)
  // -- and the root is what every normalized element gets rebound to below, so
  // taking the raw driver would propagate the derived cone into the output.
  plan.ref_net     = ref_net;
  plan.ref_clk_pin = {};
  for (const auto& e : plan.elems) {
    // A LATCH counts as a candidate too, as long as its role is Clock -- then its
    // `net` IS the clock root (a data-gated latch is Net_role::Data and is
    // excluded by the same test). Skipping latches here left a design whose only
    // state is clock-gated latches with NO reference clock at all, so the retyped
    // flops came out clockless and cgen emitted `always @(posedge 'hx)`.
    if (e.cc.role == lc::Net_role::Clock && !e.cc.implicit_clock && e.cc.net_key() == ref_net) {
      plan.ref_clk_pin = e.cc.net;
      break;
    }
  }

  // 3. Slot per class key. Clock-role classes on the reference net take their
  //    slot from the EDGE; a data-gated latch inherits the slot of the state
  //    its enable depends on.
  absl::flat_hash_map<hhds::Class_index, int> slot_of_node;  // for the data pass below
  for (auto& e : plan.elems) {
    if (e.cc.role != lc::Net_role::Clock) {
      continue;
    }
    if (e.cc.net_key() != ref_net) {
      plan.code = "off-reference-clock";
      plan.why  = "state element `" + label_of(e.node) + "` commits on a net that is not the reference clock";
      return plan;
    }
    e.slot                              = e.cc.rising ? 0 : 1;
    plan.slot_of_key[e.cc.key()]        = e.slot;
    slot_of_node[e.node.get_class_index()] = e.slot;
  }
  for (auto& e : plan.elems) {
    if (e.cc.role == lc::Net_role::Clock) {
      continue;
    }
    // Data-gated: the window closes when the enable deasserts, and the enable
    // is a function of state, so it closes on THAT state's commit edge.
    absl::flat_hash_set<hhds::Class_index> hit;
    comb_state_reach(sink_driver(e.node, "enable"), hit);
    absl::flat_hash_set<int> slots;
    for (const auto& ix : hit) {
      if (auto it = slot_of_node.find(ix); it != slot_of_node.end()) {
        slots.insert(it->second);
      }
    }
    if (slots.size() > 1) {
      plan.code = "ambiguous-data-gate";
      plan.why  = "latch `" + label_of(e.node)
               + "` has an enable driven by state in more than one commit class, so its closing edge is ambiguous";
      return plan;
    }
    e.slot                       = slots.empty() ? 0 : *slots.begin();
    plan.slot_of_key[e.cc.key()] = e.slot;
    // NOT written back into slot_of_node. That map holds CLOCK-role slots only,
    // so a data-gated latch's slot is a function of clock-role state alone --
    // order-independent by construction. Writing data-gated results back made
    // the slot of a latch whose enable depends on ANOTHER data-gated latch
    // depend on fast_class() iteration order, and the task page's condition 2
    // is binding: slots are derived structurally, never from traversal order,
    // because the two miter sides run this independently and a disagreement is
    // a false REFUTED.
  }

  // 4. Clocked memories. Not a vote for the reference clock and not a
  //    `plan.elems` entry, but every clocked port owns a slot the way a flop
  //    does: a WRITE port commits its data on its clock edge, and a sync-read
  //    (`type == 1`) port commits its read-data register. A combinational
  //    array (`type == 2`) has no edge and is recorded only to be skipped.
  //
  //    The cell carries ONE global `posclk`, and the readers wire one clock
  //    per port that all name the same net (yosys: `WR_CLK[i]` bit extracts of
  //    one port). `Memory_posclk_mixed` marks the shape whose ports do NOT
  //    share an edge; that is a multi-clock memory, not a slot lowering, and is
  //    refused. A memory with no clock driver at all rides the reference clock
  //    -- there is nothing else it could be riding.
  {
    const auto stride     = Ntype::Memory_port_stride;
    const auto pid_clk    = Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin");
    const auto pid_posclk = Ntype::get_sink_pid(Ntype_op::Memory, "posclk");
    const auto pid_type   = Ntype::get_sink_pid(Ntype_op::Memory, "type");
    for (auto n : g->fast_class()) {
      if (gu::type_op_of(n) != Ntype_op::Memory) {
        continue;
      }
      Mem_element me;
      me.node = n;
      me.type = mem_sink_const(n, pid_type).value_or(0);
      if (me.type == 2) {
        plan.mems.push_back(me);
        continue;
      }
      // `type` is defined as 0 async / 1 sync / 2 array (graph/cell.cpp pid 7).
      // The yosys reader writes yosys's per-read-port `RD_CLK_ENABLE` BITMASK
      // into it, so a memory whose read ports MIX clocked and unclocked reads
      // arrives as, e.g., 24 (= 0b11000: read ports 3 and 4 registered, 0-2
      // combinational). Measured on core-et's minion_frontend_thread_buffer.
      // pass.lean refuses that value too; refusing it HERE matters because
      // treating 24 as "not 1" would leave the two clocked read-data registers
      // unslotted -- committing on every sub-step -- which is precisely the
      // wrong lowering this pass exists never to emit.
      if (me.type != 0 && me.type != 1) {
        plan.code = "memory-type-unsupported";
        plan.why  = std::format("memory `{}` has type={} (not async 0 / sync 1 / array 2): its read ports mix "
                                "clocked and unclocked reads, which the Memory cell's one global `type` cannot "
                                "represent",
                                label_of(n), me.type);
        return plan;
      }
      const auto posclk = mem_sink_const(n, pid_posclk).value_or(1);
      if (posclk == Ntype::Memory_posclk_mixed) {
        plan.code = "memory-mixed-edge";
        plan.why  = "memory `" + label_of(n) + "` commits on more than one clock edge across its ports";
        return plan;
      }
      const bool pos = posclk != 0;
      // One commit class per COMMITTING port, then they must all agree.
      // Resolved first and checked second so a refusal can NAME every port's
      // root: a "different clock nets" message that does not say which nets is
      // not a diagnostic, it is a dead end.
      //
      // Only ports that commit have an edge to agree on. An async read
      // (`type != 1`, `rdport` set) is combinational: yosys emits its
      // `RD_CLK` as a constant 0 (measured on core-et's `prim_rf_1r1w_preview`:
      // `always_ff @(posedge rf_clk_i)` write, `assign rd_data_o = rf_q[..]`
      // read), and the reader excludes it from the polarity vote for the same
      // reason. Treating that constant as a second clock net refused every one
      // of the 17 register-file blocks this lowering exists for.
      const auto pid_rd = Ntype::get_sink_pid(Ntype_op::Memory, "rdport");
      std::vector<std::pair<hhds::Port_id, lc::Commit_class>> per_port;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() % stride != pid_clk || e.driver.is_invalid()) {
          continue;
        }
        const auto port    = static_cast<hhds::Port_id>(e.sink.get_port_id() / stride);
        const bool is_read = mem_sink_const(n, port * stride + pid_rd).value_or(0) != 0;
        if (is_read && me.type != 1) {
          continue;  // async read: no commit, no edge
        }
        // A committing port whose clock is a CONSTANT is not clocked at all: it
        // is a level-sensitive write, i.e. a LATCH ARRAY (yosys emits core-et's
        // `gen_latch.u_rf.rf_q` register file as a `$mem_v2` write port with
        // `WR_CLK_ENABLE = 0` and the clock tied off). A latch commits when its
        // enable CLOSES, which is a latch-phase analysis of the enable cone this
        // lowering does not do for memories yet -- and slotting it as if it were
        // edge-triggered would be exactly the double-negation class of silent
        // full-cycle error this pass exists to refuse.
        if (gu::is_const_pin(e.driver)) {
          plan.code = "memory-latch-array";
          plan.why  = std::format("memory `{}` port {} has a constant clock: a level-sensitive (latch-array) write, "
                                  "which has no edge to slot",
                                  label_of(n), port);
          return plan;
        }
        lc::Commit_class cc;
        cc.role = lc::Net_role::Clock;
        // A GATED clock (`<clock> & <enables>`) commits on its CLOCK operand's
        // edge, gated by the enables -- exactly a gated flop. Its commit class
        // is the clock's, and the enables fold into the port's `enable` in the
        // rewrite. Without this the And node itself becomes the "net" and a
        // memory in a gated domain looks like a second clock (measured:
        // minion_frontend_thread_buffer's `buffer_pc`, whose 37 sibling flops
        // on `clock_gated` fold fine while the memory refused as
        // off-reference).
        if (auto icg = lc::resolve_icg(e.driver, clocks)) {
          bypass_enable_latches(icg->enables);
          cc.net    = icg->clock;
          cc.rising = pos != icg->clock_inverted;
          me.icg_enables.emplace_back(port, std::move(icg->enables));
        } else {
          // The same walk a flop's `clock_pin` gets: the root net, with an
          // inversion in the cone flipping the edge the `posclk` pin names.
          const auto cr = lc::control_root(e.driver);
          if (cr.net.is_invalid()) {
            plan.code = "unresolved-commit-class";
            plan.why  = "memory `" + label_of(n) + "` has a clock cone that does not resolve to a root net";
            return plan;
          }
          if (gu::is_const_pin(cr.net)) {
            // Same latch-array shape, one width mask deeper (`Get_mask(1'b0)`).
            plan.code = "memory-latch-array";
            plan.why  = std::format("memory `{}` port {} has a clock that resolves to a constant: a level-sensitive "
                                    "(latch-array) write, which has no edge to slot",
                                    label_of(n), port);
            return plan;
          }
          cc.net    = cr.net;
          cc.rising = pos != cr.inverted;
        }
        per_port.emplace_back(port, cc);
      }
      const auto root_desc = [](const lc::Commit_class& cc) -> std::string {
        if (cc.implicit_clock) {
          return "implicit";
        }
        if (cc.net.is_invalid()) {
          return "unresolved";
        }
        if (gu::is_graph_input_pin(cc.net)) {
          return std::string(gu::pin_name_of(cc.net));
        }
        if (gu::is_const_pin(cc.net)) {
          return "const";
        }
        // An internal root: the node, its op id, and the pin's name if it has one.
        const auto pn = std::string(gu::pin_name_of(cc.net));
        return gu::debug_name(cc.net.get_master_node())
               + std::format("[op={}]", static_cast<int>(gu::type_op_of(cc.net.get_master_node())))
               + (pn.empty() ? "" : "/" + pn);
      };
      std::optional<lc::Commit_class> mcc;
      for (const auto& [port, cc] : per_port) {
        if (mcc && mcc->key() != cc.key()) {
          std::string desc;
          for (const auto& [pp, pc] : per_port) {
            desc += std::format(" port{}:{}{}", pp, root_desc(pc), pc.rising ? "+" : "-");
          }
          plan.code = "memory-mixed-edge";
          plan.why  = "memory `" + label_of(n) + "` has ports on different clock nets or edges:" + desc;
          return plan;
        }
        mcc = cc;
      }
      if (!mcc) {
        lc::Commit_class cc;
        cc.implicit_clock = true;
        cc.role           = lc::Net_role::Clock;
        cc.rising         = pos;
        mcc               = cc;
      } else if (mcc->net_key() != ref_net) {
        plan.code = "off-reference-clock";
        plan.why  = std::format("memory `{}` commits on `{}` ({}), not the reference clock `{}`", label_of(n),
                                root_desc(*mcc), mcc->rising ? "rise" : "fall", ref_net);
        return plan;
      }
      me.cc   = *mcc;
      me.slot = mcc->rising ? 0 : 1;
      plan.mems.push_back(me);
    }
  }

  int max_slot = 0;
  for (const auto& e : plan.elems) {
    max_slot = std::max(max_slot, e.slot);
  }
  for (const auto& me : plan.mems) {
    if (me.type != 2) {
      max_slot = std::max(max_slot, me.slot);
    }
  }
  plan.slots = max_slot + 1;
  plan.ok    = true;
  return plan;
}

// Rule 4 (the US7624363 L1 case), as a PRECONDITION of the lowering rather
// than an optimization. A latch is transparent right up to its closing edge, so
// anything that commits at that SAME instant reads the latch's D, not its held
// Q. The flop-with-enable model gives it the held Q instead: a persistent
// FULL-CYCLE error, silent in Verilog and invisible to a symmetric gate.
//
// Slotting does not fix this — it REPRODUCES it, because coincident classes
// share a slot by construction. So refuse (M3 rule 4's "explicit rejection"
// arm); rewriting the latch to a buffer is the other legal answer and is left
// for when a real design needs it.
bool check_rule4(const Plan& plan, bool quiet) {
  bool ok = true;
  for (const auto& l : plan.elems) {
    if (!l.is_latch) {
      continue;
    }
    auto q = l.node.get_driver_pin(0);
    if (q.is_invalid()) {
      continue;
    }
    for (const auto& s : plan.elems) {
      // Keyed on the COMMIT CLASS (net AND edge), not on the slot. Two elements
      // can share a slot without sharing an edge — a data-gated latch defaults
      // to slot 0 alongside the posedge flops, and that pairing is the ordinary
      // flop-with-enable abstraction the whole task is scoped to (it is what
      // `lhd sim` and cgen already ship). The L1 hazard is narrower: the latch
      // closes on the SAME edge of the SAME net that the reader samples on.
      if (s.node.get_class_index() == l.node.get_class_index() || s.cc.key() != l.cc.key()) {
        continue;
      }
      absl::flat_hash_set<hhds::Class_index> hit;
      comb_state_reach(sink_driver(s.node, "din"), hit);
      if (!hit.contains(l.node.get_class_index())) {
        continue;
      }
      ok = false;
      if (quiet) {
        // A PROBE (dry run) must stay silent: under `formal.phase_sched` a
        // decline is not a failure -- the caller hands the design to the formal
        // phase schedule instead -- and an error-severity diagnostic here would
        // fail an otherwise clean, PROVEN run through the CLI's error count.
        return false;
      }
      livehd::diag::err(kPass, "coincident-commit-edge", "unsupported")
          .msg("latch `{}` and state element `{}` commit at the SAME edge, and `{}` reads the latch combinationally",
               label_of(l.node), label_of(s.node), label_of(s.node))
          .hint("the latch is still transparent at that edge, so the real hardware samples its D while the "
                "commit-at-closing-edge model samples its held Q — a persistent full-cycle error. Move the two onto "
                "opposite phases (a master/slave pair), or replace the latch with a buffer if it is meant to be one")
          .emit();
      break;
    }
    // The same L1 hazard through a MEMORY: a write (or sync-read) port that
    // commits on the latch's closing edge and whose addr/data/enable cone reads
    // the latch's Q samples its D in hardware and its held Q in the model.
    // Same key test, same reach walk, over the port operand pins instead of
    // `din`.
    if (!ok) {
      continue;
    }
    const auto stride  = Ntype::Memory_port_stride;
    const auto pid_rd  = Ntype::get_sink_pid(Ntype_op::Memory, "rdport");
    const auto pid_en  = Ntype::get_sink_pid(Ntype_op::Memory, "enable");
    const auto pid_ad  = Ntype::get_sink_pid(Ntype_op::Memory, "addr");
    const auto pid_din = Ntype::get_sink_pid(Ntype_op::Memory, "din");
    for (const auto& me : plan.mems) {
      if (me.type == 2 || me.cc.key() != l.cc.key()) {
        continue;
      }
      absl::flat_hash_set<hhds::Class_index> hit;
      for (const auto& e : me.node.inp_edges()) {
        const auto raw = e.sink.get_port_id();
        const auto off = raw % stride;
        if (off != pid_en && off != pid_ad && off != pid_din) {
          continue;
        }
        const auto p       = static_cast<hhds::Port_id>(raw / stride);
        const bool is_read = mem_sink_const(me.node, p * stride + pid_rd).value_or(0) != 0;
        if (is_read && me.type != 1) {
          continue;  // an async read commits nothing
        }
        comb_state_reach(e.driver, hit);
      }
      if (!hit.contains(l.node.get_class_index())) {
        continue;
      }
      ok = false;
      if (quiet) {
        return false;
      }
      livehd::diag::err(kPass, "coincident-commit-edge", "unsupported")
          .msg("latch `{}` and memory `{}` commit at the SAME edge, and the memory's port reads the latch "
               "combinationally",
               label_of(l.node), label_of(me.node))
          .hint("the latch is still transparent at that edge, so the real hardware writes its D while the "
                "commit-at-closing-edge model writes its held Q — a persistent full-cycle error. Move the two onto "
                "opposite phases, or replace the latch with a buffer if it is meant to be one")
          .emit();
      break;
    }
  }
  return ok;
}

// `posclk` known-false on a LATCH means the enable is active LOW: transparent
// while enable == 0. A `Flop` has no polarity pin with that meaning — pid 6 on
// a Flop is the CLOCK EDGE — so carrying the pin across the retype would read
// back as "negedge flop" and silently invert the design's timing. That is the
// double-negation miscompile this whole task exists to prevent.
//
// The sound lowering is to move the polarity into the enable CONE, where the
// Pyrope/slang shapes already carry it: enable := !enable, then drop the pin.
// This is NOT the refused "bare polarity-pin flip": that flip left `din`'s hold
// mux keyed on the UN-inverted condition, so the latch wrote itself forever.
// Here the enable and the din keep the SAME condition — only the test is
// inverted — which is exactly what cgen_verilog emits for this cell
// (`neg_en` -> `if (!en)`), so the two agree by construction.
bool latch_is_active_low(const hhds::Node_class& latch) {
  auto pc = sink_driver(latch, "posclk");
  return !pc.is_invalid() && gu::is_const_pin(pc) && gu::hydrate_const(pc).is_known_false();
}

// True when `din` carries tolg's hold mux (`cond ? d : q`), i.e. a Mux on din
// with this latch's own Q as an arm. An active-LOW latch that ALSO has one is a
// shape no emitter produces: the hold would be keyed on the un-inverted
// condition while the enable test is inverted, so the two disagree. Refuse it.
bool has_hold_mux(const hhds::Node_class& latch) {
  auto q   = latch.get_driver_pin(0);
  auto din = sink_driver(latch, "din");
  if (q.is_invalid() || din.is_invalid() || gu::is_const_pin(din) || gu::is_graph_input_pin(din)) {
    return false;
  }
  auto n = din.get_master_node();
  if (gu::type_op_of(n) != Ntype_op::Mux) {
    return false;
  }
  for (const auto& e : n.inp_edges()) {
    if (!e.driver.is_invalid() && e.driver.get_class_index() == q.get_class_index()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool needed(hhds::Graph* g, const std::vector<hhds::Graph*>& defs) {
  if (g != nullptr && lc::needs_single_edge(g).needed) {
    return true;
  }
  for (auto* d : defs) {
    if (d != nullptr && lc::needs_single_edge(d).needed) {
      return true;
    }
  }
  return false;
}

Result normalize(hhds::Graph* g, const std::vector<hhds::Graph*>& defs, const Options& opts) {
  Result     r;
  const bool quiet = opts.quiet || opts.dry_run;
  if (g == nullptr) {
    return r;
  }

  const lc::Design_clocks clocks(g);
  const auto              need = lc::needs_single_edge(g, &clocks);
  // A def only forces our hand when it holds something the ENGINES GET WRONG --
  // a Latch (the encoder refuses the cell) or a negedge flop (it is blind to
  // the edge). Keying this on the full trigger instead swept in ">= 2 clock
  // nets", so any hierarchical design with a second clock net ANYWHERE in its
  // library -- or any `--lib` cell model that happens to have one -- refused
  // every run, including designs that instantiate none of it.
  // Only defs this design actually INSTANTIATES matter. Scanning every graph the
  // caller handed us refused on library modules the design never touches -- and
  // it also went stale the moment a clock-gate cell was inlined, because
  // inline_sub deliberately leaves the def in the library for its other
  // instantiation sites. Walk `g`'s own instance tree instead.
  absl::flat_hash_set<hhds::Graph*> reachable;
  {
    std::vector<hhds::Graph*> work{g};
    while (!work.empty()) {
      auto* cur = work.back();
      work.pop_back();
      for (auto n : cur->fast_class()) {
        if (gu::type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        if (auto sub = n.get_subnode_graph(); sub && reachable.insert(sub.get()).second) {
          work.push_back(sub.get());
        }
      }
    }
  }
  // `defs` is the caller's ALLOW-LIST: it already excludes defs the user
  // COLLAPSED or TRUSTED, which are blackboxes whose contents are assumed equal
  // and never encoded -- `formal.lec.trust` is the documented escape hatch for
  // exactly "the encoder cannot model this leaf", and refusing on its contents
  // would pre-empt the mechanism that makes such a design provable at all.
  // So consider a def only when it is BOTH instantiated and allowed.
  absl::flat_hash_set<hhds::Graph*> allowed(defs.begin(), defs.end());
  bool        def_needs = false;
  std::string def_why;  // which def, and WHAT is in it (the diagnostic must say)
  for (auto* d : reachable) {
    if (d == nullptr || !allowed.contains(d)) {
      continue;
    }
    const auto dn = lc::needs_single_edge(d);
    if (dn.n_latches > 0 || dn.n_negedge_flops > 0) {
      def_needs = true;
      def_why   = std::format("def `{}` holds {}", d->get_name(), dn.why);
      break;
    }
  }
  // What the pass OWES a lowering for: a Latch (the encoder refuses the cell)
  // and a negedge flop (the encoder is blind to the edge). A design that merely
  // has two clock nets is already handled honestly downstream — pass/lec's
  // detected-edge branch REFUTES two-clock-vs-one-clock today — so declining it
  // here would turn a correct verdict into an unsupported exit.
  // A gated-clock flop is on the list because encoding it as a gated CLOCK is
  // what the encoder cannot do soundly OR cheaply: it has no clock-identity
  // model (so gated and ungated look identical unless the ICG name heuristic
  // happens to match) and a gate costs a per-step commit term. Rewritten into
  // the flop's ENABLE it is just an ordinary flop.
  //
  // ...but ONLY in a single-clock design. Folding a gate into an enable
  // expresses the flop's timing relative to THE reference clock, so a gate on a
  // SECOND domain (`gclk = clk_b & gate` while other flops run on `clock`) has
  // no such reference and the fold would be meaningless. There we owe nothing:
  // leave it skipped for pass/lec's multi-clock machinery, exactly as before.
  const bool icg_foldable = need.n_icg_flops > 0 && need.n_clock_nets <= 1;
  const bool must_fix     = need.n_latches > 0 || need.n_negedge_flops > 0 || icg_foldable || def_needs;
  if (!must_fix && opts.force_slots <= 1) {
    r.reason = need.needed ? std::format("skipped: {} — no latch or negedge state to lower", need.why)
                           : "skipped: no latch, no negedge state, one clock net";
    return r;
  }
  if (def_needs) {
    // A Latch inside a resolution-library def is NOT covered by fixing the top
    // graph: pass/lec/encode.cpp screens defs separately and sends a stateful
    // one to the blackbox path, so the design would silently keep an opaque
    // per-side transition — the free-per-side-constant CEX, with no diagnostic.
    r.error  = true;
    r.reason = def_why + ", and normalizing across a module boundary is not supported yet";
    refuse(quiet, "hier-unsupported", std::format("{}: {}", g->get_name(), r.reason),
           "flatten the design (--set formal.flatten=true), blackbox the def (--set formal.lec.trust=<def>), or "
           "normalize the def separately");
    return r;
  }

  auto plan = build_plan(g, clocks);
  if (!plan.ok) {
    if (!must_fix) {
      r.reason = std::format("skipped: {}", plan.why);  // nothing owed here; downstream handles it
      return r;
    }
    r.error  = true;
    r.reason = plan.why;
    refuse(quiet, plan.code, std::format("{}: {}", g->get_name(), plan.why),
           "edge normalization declines rather than half-transform: a partial lowering is a silent full-cycle error");
    return r;
  }
  plan.slots = std::max(plan.slots, opts.force_slots);
  if (!check_rule4(plan, quiet)) {
    r.error  = true;
    r.reason = "coincident commit edges between a latch and another state element";
    return r;
  }
  if (plan.slots > 2) {
    r.error  = true;
    r.reason = std::format("{} slots needed; only P<=2 is implemented", plan.slots);
    refuse(quiet, "too-many-slots", std::format("{}: {}", g->get_name(), r.reason), "");
    return r;
  }

  // Does this def REWRITE a clock? A latch retype moves when its enable
  // commits, and an ICG fold moves the gate into a flop enable; a def with only
  // plain posedge flops rewrites nothing and cannot mis-time a child.
  bool rewrites_a_clock = false;
  for (const auto& e : plan.elems) {
    if (e.is_latch || e.icg) {
      rewrites_a_clock = true;
      break;
    }
  }

  // Fail closed on shapes a partial lowering would silently mis-time.
  for (auto n : g->fast_class()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Fflop) {
      r.error  = true;
      r.reason = "fluid flop `" + label_of(n) + "` has no commit-class model";
      refuse(quiet, "fflop-unsupported", std::format("{}: {}", g->get_name(), r.reason), "");
      return r;
    }
    // A Memory is no longer refused at P > 1: build_plan classified it (or
    // declined the design) and the rewrite slot-gates its committing ports.
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto sub = n.get_subnode_graph();
    if (!sub) {
      continue;  // blackbox / property cell: no state of ours to re-time
    }
    // ...and only under a DIVIDER, for the same reason a memory's ports need
    // slotting there. What this refuses is re-timing a child we cannot reach:
    // at P>1 the parent's
    // state moves onto slots driven by a phase counter that is not threaded
    // through the child's ports, so the child would keep counting in the old
    // time base. At P=1 there IS no phase counter and no slot -- a latch
    // retype and an ICG folded into a flop enable both leave every clock and
    // every instance boundary exactly where they were -- so a stateful child
    // is simply not this pass's business.
    //
    // Scoping this is what lets a def with an inlined clock gate fold at all:
    // a real one instantiates FIFOs and register files (minion's
    // `minion_dcache_reduce` holds `u_ba_alloc_fifo`), so the unscoped guard
    // refused every def that mattered and the gated flops stayed unencodable.
    bool child_has_state = false;
    for (auto sn : sub->fast_class()) {
      if (gu::is_type_register(sn)) {
        child_has_state = true;
        break;
      }
    }
    if (child_has_state && plan.slots > 1) {
      r.error  = true;
      r.reason = "instance `" + label_of(n) + "` holds state; the phase divider is not port-threaded";
      refuse(quiet, "hier-unsupported", std::format("{}: {}", g->get_name(), r.reason),
             "flatten the design before normalizing (all three M8 gate fixtures are flat)");
      return r;
    }
    // P=1 is NOT automatically safe for a stateful child. The claim above --
    // "a latch retype and an ICG folded into a flop enable leave every clock and
    // every instance boundary exactly where they were" -- holds only while the
    // rewritten clock stays INSIDE this def. When an ICG's gated clock also
    // crosses into a stateful child's clock port, the fold retypes the enable
    // latch to a flop for the LOCAL flops while the child keeps consuming the
    // gate, so the two halves of one clock gate disagree by a full cycle and the
    // pass silently ships a design whose submodule gating is off by one -- where
    // it used to refuse honestly.
    //
    // The discriminator is cheap and exact: a child clocked by a PLAIN clock
    // input is untouched by anything this pass does (that is the minion shape
    // the narrowing exists for), while a child clocked by IN-GRAPH LOGIC is
    // clocked by something we may be about to rewrite. Refuse only the latter,
    // and only when this def actually rewrites a clock (a latch or a folded
    // ICG); a def with plain posedge state normalizes nothing and stays allowed.
    if (child_has_state && rewrites_a_clock) {
      auto csio = n.get_subnode_io();
      for (const auto& e : n.inp_edges()) {
        // Clock ports only, by the SHARED spelling notion (Design_clocks::
        // name_looks_like_clock) rather than a private list, so this and the
        // rest of M8 agree on what a clock port is.
        std::string_view pname;
        for (const auto& d : csio->get_input_pin_decls()) {
          if (csio->get_input_port_id(d.name) == e.sink.get_port_id()) {
            pname = d.name;
            break;
          }
        }
        if (pname.empty() || !lc::Design_clocks::name_looks_like_clock(pname)) {
          continue;
        }
        // A PLAIN clock input (possibly through the width mask the readers add)
        // is untouched by this pass; anything else is in-graph logic.
        if (gu::is_graph_input_pin(e.driver)
            || (gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Get_mask
                && [&] {
                     for (const auto& ge : e.driver.get_master_node().inp_edges()) {
                       if (gu::is_graph_input_pin(ge.driver)) {
                         return true;
                       }
                     }
                     return false;
                   }())) {
          continue;
        }
        {
          r.error  = true;
          r.reason = "instance `" + label_of(n) + "` holds state and is clocked by in-graph logic this pass rewrites";
          refuse(quiet, "hier-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                 "the gate would be re-timed for this def's own flops but not for the child's; flatten the design, or "
                 "trust the child def, before normalizing");
          return r;
        }
      }
    }
  }

  // An active-LOW latch is lowered by inverting its ENABLE. Two shapes make
  // that impossible, and both are refused rather than approximated.
  for (const auto& e : plan.elems) {
    if (!e.is_latch || !latch_is_active_low(e.node)) {
      continue;
    }
    if (sink_driver(e.node, "enable").is_invalid()) {
      r.error  = true;
      r.reason = "latch `" + label_of(e.node) + "` is active-low with no enable driver (permanently transparent)";
      refuse(quiet, "raw-latch-unsupported", std::format("{}: {}", g->get_name(), r.reason),
             "a permanently transparent latch is a buffer, not a state element; drop the cell");
      return r;
    }
    if (has_hold_mux(e.node)) {
      r.error  = true;
      r.reason = "latch `" + label_of(e.node) + "` has an active-low polarity pin AND a hold mux on din";
      refuse(quiet, "raw-latch-unsupported", std::format("{}: {}", g->get_name(), r.reason),
             "the hold mux is keyed on the un-inverted condition while the enable test is inverted, so the two "
             "disagree — no emitter produces this shape and lowering it would pick one arbitrarily");
      return r;
    }
  }

  if (opts.dry_run) {
    // Everything above is analysis; nothing has been mutated. Report what the
    // rewrite WOULD do so a miter can pick one shared time base first.
    r.applied   = plan.slots > 1 || r.latches_retyped > 0 || need.n_latches > 0 || need.n_negedge_flops > 0;
    r.slots     = plan.slots;
    r.ref_clock = plan.ref_net;
    r.reason  = std::format("plan: P={} slots ({})", plan.slots, need.why.empty() ? "forced" : need.why);
    return r;
  }

  // ---- the rewrite -------------------------------------------------------
  hhds::Pin_class slot_pred[2];
  if (plan.slots > 1) {
    // Phase divider off the design's OWN clock: one free-running clock and a
    // CONCRETELY INITIALIZED counter, so at unroll step j the solver knows
    // `phase == j mod P` statically and every slot predicate const-folds.
    auto ph = gu::create_typed_node(*g, Ntype_op::Flop);
    ph.attr(hhds::attrs::name).set(std::string{kPhaseName});
    auto phq = ph.create_driver_pin(0);
    gu::set_bits(phq, 1);
    gu::set_unsign(phq);
    phq.attr(livehd::attrs::pin_name).set(std::string{kPhaseName});
    gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(ph, "initial"));
    if (!plan.ref_clk_pin.is_invalid()) {
      plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(ph, "clock_pin"));
    }
    // Reset the divider with the DESIGN's own reset, copied off a state element
    // that already has one. Without it the phase starts wherever the BMC's
    // after_reset prologue leaves it: that engine deliberately discards flop
    // `initial` values (a free start is the sound over-approximation), and a
    // free phase lets the solver choose the WRONG sub-step parity, which makes
    // the period-boundary guard fire mid-period and refute an equivalent
    // design. pass/lec/query.cpp also keeps this one key's init as a documented
    // exception (graph_util::is_single_edge_phase_key) so a design with no
    // reset port is covered too — belt and braces, deliberately.
    for (const auto& e : plan.elems) {
      auto rp = sink_driver(e.node, "reset_pin");
      if (rp.is_invalid()) {
        continue;
      }
      rp.connect_sink(gu::setup_sink_by_name(ph, "reset_pin"));
      if (auto nr = sink_driver(e.node, "negreset"); !nr.is_invalid()) {
        nr.connect_sink(gu::setup_sink_by_name(ph, "negreset"));
      }
      break;
    }
    // P == 2: next = !phase (a 1-bit counter). P > 2 is refused above.
    auto nxt = gu::create_typed_node(*g, Ntype_op::Not);
    phq.connect_sink(nxt.create_sink_pin(0));
    auto nxtq = nxt.create_driver_pin(0);
    gu::set_bits(nxtq, 1);
    gu::set_unsign(nxtq);
    nxtq.connect_sink(gu::setup_sink_by_name(ph, "din"));

    for (int k = 0; k < plan.slots; ++k) {
      auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
      phq.connect_sink(eq.create_sink_pin(0));
      gu::create_const(*g, *Dlop::create_integer(k)).connect_sink(eq.create_sink_pin(0));
      auto eqq = eq.create_driver_pin(0);
      gu::set_bits(eqq, 1);
      gu::set_unsign(eqq);
      slot_pred[k] = eqq;
    }
  }

  for (const auto& e : plan.elems) {
    if (e.is_latch) {
      // `posclk` on a Latch is the ENABLE POLARITY, not an edge. Carrying it
      // onto a Flop would silently re-read it as "negedge" — the exact
      // double-negation this task exists to prevent. Move the polarity into the
      // enable cone (where every Pyrope/slang latch already carries it), then
      // drop the pin.
      if (latch_is_active_low(e.node)) {
        auto en  = sink_driver(e.node, "enable");
        auto inv = gu::create_typed_node(*g, Ntype_op::Not);
        en.connect_sink(inv.create_sink_pin(0));
        auto iq = inv.create_driver_pin(0);
        gu::set_bits(iq, 1);
        gu::set_unsign(iq);
        drop_sink(e.node, "enable");
        iq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      // ---- a CLOCK-gated latch: the gate is ABSORBED by the slot ----------
      // Not AND-ed with it. A latch whose gate IS a clock commits exactly once
      // per period, at one edge, and the slot already says which — so the gate
      // has no residual meaning. Leaving it in place would keep the normalized
      // design READING THE CLOCK AS DATA, and after normalization one clock
      // tick is one SUB-step, so that cone would be evaluated P times per
      // source period: the enable becomes `clk & (phase==slot)`, which commits
      // only when the free clock input happens to be high at the slot step.
      // Measured with iverilog: the latch outputs stay X.
      //
      // So the enable becomes the slot predicate alone, and `din` becomes the
      // TRANSPARENT arm of tolg's hold mux (`gate ? d : q` -> `d`): the value
      // the window was passing through when it closed, which is exactly what
      // commit-at-closing-edge commits. A DATA gate is left completely alone —
      // it is real logic, and collapsing the two cases is what would blind the
      // transparent-low polarity canary.
      if (e.cc.role == lc::Net_role::Clock) {
        if (has_hold_mux(e.node)) {
          auto q   = e.node.get_driver_pin(0);
          auto mux = sink_driver(e.node, "din").get_master_node();
          hhds::Pin_class transparent;
          for (const auto& me : mux.inp_edges()) {
            if (me.sink.get_port_id() == 0) {
              continue;  // the selector (the gate itself)
            }
            if (!me.driver.is_invalid() && me.driver.get_class_index() != q.get_class_index()) {
              transparent = me.driver;
            }
          }
          if (transparent.is_invalid()) {
            r.error  = true;
            r.reason = "clock-gated latch `" + label_of(e.node) + "` has a hold mux with no transparent arm";
            refuse(quiet, "latch-shape-unsupported", std::format("{}: {}", g->get_name(), r.reason), "");
            return r;
          }
          drop_sink(e.node, "din");
          transparent.connect_sink(gu::setup_sink_by_name(e.node, "din"));
        }
        // No hold mux (the yosys raw-D shape): `din` is already the transparent
        // value, so only the gate has to go.
        drop_sink(e.node, "enable");
      }
      drop_sink(e.node, "posclk");
      gu::set_type_op(e.node, Ntype_op::Flop);
      ++r.latches_retyped;  // clock_pin is bound by the shared rebind below
    } else if (!e.cc.rising) {
      // A negedge flop becomes a posedge flop in slot 1.
      drop_sink(e.node, "posclk");
    }
    // ---- CLOCK GATING -> FLOP ENABLE --------------------------------------
    // An ICG is the commonest latch structure in hardware, and modelling it as
    // a gated CLOCK is both unsound here (the encoder has no clock-identity
    // model, so gated and ungated look identical) and wasteful (a per-step
    // commit term on every flop). A gated clock IS a flop enable: the flop
    // commits on the reference edge iff the enables hold there. So fold the
    // enables into `enable` and let the ordinary Flop encoding do the work.
    //
    // The ENABLE-LATCH BYPASS is what makes this exact. A real ICG latches its
    // enable on the opposite phase purely to suppress glitches: the latch closes
    // exactly when the flop samples, so the flop sees the value the latch was
    // passing THROUGH, not its previously-held one. Taking the latch's Q instead
    // would be a full cycle late -- and would also make the latch and the flop a
    // coincident-edge pair, which rule 4 then (correctly, for that shape)
    // rejects. Taking its transparent arm is the US7624363 L1->buffer rewrite,
    // applied exactly where it belongs.
    if (e.icg) {
      std::vector<hhds::Pin_class> ens(e.icg->enables);  // already latch-bypassed in build_plan
      if (auto old_en = sink_driver(e.node, "enable"); !old_en.is_invalid()) {
        ens.push_back(old_en);
      }
      hhds::Pin_class acc;
      for (const auto& en : ens) {
        if (acc.is_invalid()) {
          acc = en;
          continue;
        }
        auto andn = gu::create_typed_node(*g, Ntype_op::And);
        acc.connect_sink(andn.create_sink_pin(0));
        en.connect_sink(andn.create_sink_pin(0));
        acc = andn.create_driver_pin(0);
        gu::set_bits(acc, 1);
        gu::set_unsign(acc);
      }
      drop_sink(e.node, "enable");
      if (!acc.is_invalid()) {
        acc.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      ++r.icg_folded;
    }

    // ---- every element now commits on the REFERENCE clock ----------------
    // After normalization the slot predicate carries ALL of the timing, so any
    // derived clock cone left on clock_pin is not just redundant, it is
    // actively harmful: the encoder still sees a derived clock and (correctly,
    // since M8) REFUSES to model it, so a design the pass had just lowered
    // successfully came back UNKNOWN. Rebinding to the resolved root also
    // removes the inversion of a `posedge ~clk` flop, whose edge is now
    // expressed by its slot instead.
    if (!plan.ref_clk_pin.is_invalid()) {
      auto cur = sink_driver(e.node, "clock_pin");
      if (cur.is_invalid() || cur.get_class_index() != plan.ref_clk_pin.get_class_index()) {
        drop_sink(e.node, "clock_pin");
        plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(e.node, "clock_pin"));
      }
    }

    if (plan.slots > 1) {
      // ---- SYNC reset moves INSIDE the slot gate --------------------------
      // The encoder nests reset OUTSIDE the commit (`ITE(rst, init, ITE(en,
      // din, self))`), i.e. ASYNC for every flop. Harmless while one step is
      // one commit; WRONG the moment a flop only owns some steps, because its
      // reset then also lands in slots it does not own. Measured on
      // flop_reset_matrix: the ref spells a sync reset with `reset_pin` (reset
      // outside the gate) and its own round-trip spells the identical reset as
      // an ordinary `if reset { … }` in the DATA path (inside the gate), so at
      // P=2 the two disagreed at the first checked step — a FALSE REFUTED on a
      // design that is its own round-trip.
      //
      // Normalize toward the data-path form, which is what "synchronous" means:
      //     din    := rst ? initial : din
      //     enable := enable | rst          (reset WINS over the enable)
      // then drop the pin. An ASYNC reset is left on `reset_pin` — outside the
      // gate is exactly right for it, and it is also the form M7 ruled a
      // lowered latch's reset must keep.
      if (auto rstp = sink_driver(e.node, "reset_pin"); !rstp.is_invalid()) {
        auto asyncp = sink_driver(e.node, "async");
        // A LATCH's reset is inherently ASYNCHRONOUS -- there is no clock edge
        // for it to synchronize to, which is M7's landed ruling and why cgen
        // emits it as the FIRST branch ahead of the transparency test. So a
        // retyped latch's reset must stay OUTSIDE the slot gate even when it
        // carries no `async` attribute. Folding it in would gate the reset on
        // the slot predicate, and an async reset pulse that falls between two
        // normalized clock edges would be silently lost.
        const bool is_async = e.is_latch
                              || (!asyncp.is_invalid() && gu::is_const_pin(asyncp)
                                  && !gu::hydrate_const(asyncp).is_known_false());
        if (!is_async) {
          auto negp = sink_driver(e.node, "negreset");
          auto test = rstp;
          if (!negp.is_invalid() && gu::is_const_pin(negp) && !gu::hydrate_const(negp).is_known_false()) {
            auto inv = gu::create_typed_node(*g, Ntype_op::Not);
            rstp.connect_sink(inv.create_sink_pin(0));
            test = inv.create_driver_pin(0);
            gu::set_bits(test, 1);
            gu::set_unsign(test);
          }
          auto din  = sink_driver(e.node, "din");
          auto init = sink_driver(e.node, "initial");
          const int qw = std::max(1, static_cast<int>(gu::bits_of(e.node.get_driver_pin(0))));
          if (init.is_invalid()) {
            init = gu::create_const(*g, *Dlop::create_integer(0));
          }
          if (!din.is_invalid()) {
            // Mux pin ids: 0 = selector, 1 = arm taken when the selector is 0,
            // 2 = arm taken when it is 1. Reset value on the 1 arm.
            auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
            test.connect_sink(mux.create_sink_pin(0));
            din.connect_sink(mux.create_sink_pin(1));
            init.connect_sink(mux.create_sink_pin(2));
            auto mq = mux.create_driver_pin(0);
            gu::set_bits(mq, qw);
            gu::set_unsign(mq);
            drop_sink(e.node, "din");
            mq.connect_sink(gu::setup_sink_by_name(e.node, "din"));
          }
          if (auto en = sink_driver(e.node, "enable"); !en.is_invalid()) {
            auto orn = gu::create_typed_node(*g, Ntype_op::Or);
            en.connect_sink(orn.create_sink_pin(0));
            test.connect_sink(orn.create_sink_pin(0));
            auto oq = orn.create_driver_pin(0);
            gu::set_bits(oq, 1);
            gu::set_unsign(oq);
            drop_sink(e.node, "enable");
            oq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
          }
          drop_sink(e.node, "reset_pin");
          drop_sink(e.node, "negreset");
          // `initial` goes with it. It is BOTH the reset value and the flop's
          // power-on value, and the reset value now lives in the mux above — so
          // keeping the pin leaves a concrete power-on value that the same
          // design's own round-trip (which spells the sync reset in the data
          // path from the start) does not have. Measured: that asymmetry is
          // what the state pairing reports as "kind/init mismatch", and an
          // unpaired reset-less flop gets a FREE per-side constant, i.e. the
          // q(ref=254 impl=255) step-1 counterexample.
          drop_sink(e.node, "initial");
        }
      }

      // enable &= (phase == slot)
      auto pred = slot_pred[e.slot];
      auto old  = sink_driver(e.node, "enable");
      if (old.is_invalid()) {
        pred.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      } else {
        auto andn = gu::create_typed_node(*g, Ntype_op::And);
        old.connect_sink(andn.create_sink_pin(0));
        pred.connect_sink(andn.create_sink_pin(0));
        auto aq = andn.create_driver_pin(0);
        gu::set_bits(aq, 1);
        gu::set_unsign(aq);
        drop_sink(e.node, "enable");
        aq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      ++r.flops_slotted;
    }
  }

  // ---- clocked memories: slot-gate every port that COMMITS -----------------
  // A memory's write port commits on its clock edge exactly as a flop does, so
  // under a divider it must commit only in its slot -- left alone it would write
  // on every sub-step, which is the shape this pass used to refuse outright.
  // The gate is the one a flop gets, `enable &= (phase == slot)`, applied to:
  //   * every WRITE port's `enable`;
  //   * a SYNC-read (`type == 1`) port's `enable`, because that enable is what
  //     captures the read-data REGISTER (`if ren then table[addr] else old` in
  //     the certificate), and a register is state that commits;
  //   * NOT an async (`type == 0`) read port: its read is combinational and
  //     always visible, and gating it would read as zero in the other slot.
  // A `type == 2` array is combinational through and through and is skipped.
  // The byte-enable lanes (`wensize`) are data, not the commit, and are not
  // touched.
  if (plan.slots > 1) {
    const auto stride     = Ntype::Memory_port_stride;
    const auto pid_en     = Ntype::get_sink_pid(Ntype_op::Memory, "enable");
    const auto pid_rd     = Ntype::get_sink_pid(Ntype_op::Memory, "rdport");
    const auto pid_clk    = Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin");
    const auto pid_posclk = Ntype::get_sink_pid(Ntype_op::Memory, "posclk");
    for (const auto& me : plan.mems) {
      if (me.type == 2) {
        continue;
      }
      // Every raw pid on the cell names the port it belongs to.
      std::vector<hhds::Port_id> ports;
      for (const auto& e : me.node.inp_edges()) {
        ports.push_back(static_cast<hhds::Port_id>(e.sink.get_port_id() / stride));
      }
      std::sort(ports.begin(), ports.end());
      ports.erase(std::unique(ports.begin(), ports.end()), ports.end());

      const auto pred = slot_pred[me.slot];
      for (const auto p : ports) {
        // `rdport` absent reads as a write port, exactly as pass.lean's walk
        // defaults it (Memory_port_info::rdport = false).
        const bool is_read = mem_sink_const(me.node, p * stride + pid_rd).value_or(0) != 0;
        if (is_read && me.type != 1) {
          continue;  // async read: combinational, nothing commits
        }
        const auto en_pid  = static_cast<hhds::Port_id>(p * stride + pid_en);
        const auto en_name = std::string(Ntype::get_sink_name(Ntype_op::Memory, en_pid));
        // enable := slot predicate & old enable & every ICG enable on this port
        std::vector<hhds::Pin_class> terms{pred};
        if (auto old = mem_sink_driver(me.node, en_pid); !old.is_invalid()) {
          terms.push_back(old);
        }
        for (const auto& [ip, ens] : me.icg_enables) {
          if (ip == p) {
            terms.insert(terms.end(), ens.begin(), ens.end());
          }
        }
        hhds::Pin_class acc;
        for (const auto& t : terms) {
          if (acc.is_invalid()) {
            acc = t;
            continue;
          }
          auto andn = gu::create_typed_node(*g, Ntype_op::And);
          acc.connect_sink(andn.create_sink_pin(0));
          t.connect_sink(andn.create_sink_pin(0));
          acc = andn.create_driver_pin(0);
          gu::set_bits(acc, 1);
          gu::set_unsign(acc);
        }
        drop_sink(me.node, en_name);
        acc.connect_sink(gu::setup_sink_by_name(me.node, en_name));
      }
      // Every commit is now expressed by its slot on the reference clock. A
      // negedge `posclk` left in place would be re-read as a second inversion
      // (the same double negation the latch path guards against), so drop it --
      // the pin's absence is posedge -- and rebind any explicit clock to the
      // resolved root, as the flop loop does.
      if (mem_sink_const(me.node, pid_posclk).value_or(1) == 0) {
        drop_sink(me.node, std::string(Ntype::get_sink_name(Ntype_op::Memory, pid_posclk)));
      }
      if (!plan.ref_clk_pin.is_invalid()) {
        for (const auto p : ports) {
          const auto clk_pid = static_cast<hhds::Port_id>(p * stride + pid_clk);
          auto       cur     = mem_sink_driver(me.node, clk_pid);
          if (cur.is_invalid() || cur.get_class_index() == plan.ref_clk_pin.get_class_index()) {
            continue;  // implicit clock, or already the root
          }
          const auto nm = std::string(Ntype::get_sink_name(Ntype_op::Memory, clk_pid));
          drop_sink(me.node, nm);
          plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(me.node, nm));
        }
      }
      ++r.memories_slotted;
    }
  }

  // ---- period-boundary guard on the design's obligations -----------------
  // Keeps ALL period knowledge inside this pass instead of smearing P through
  // Lec_options, both fork-race codecs and the verify cache key. Without it a
  // phase-1 step has one half of the design committed and the other not, so a
  // relational assert (`bq == aq`) is outright FALSE mid-period and plain BMC
  // refutes it well inside the bound.
  if (plan.slots > 1) {
    // The boundary is `phase == 0`, NOT `phase == P-1`. A step READS state and
    // COMMITS at its end, so the state visible at the phase-k step is what
    // committed through slot k-1. The whole period is therefore settled — every
    // slot committed, exactly the end-of-cycle observation model the fixtures
    // pin — at the phase-0 step that STARTS the next period. Traced by hand on
    // flop_verify_posneg: at a phase-1 step `a` has committed and `b` has not,
    // so `bq == aq` is outright false there and plain BMC refutes it.
    const auto boundary = slot_pred[0];
    for (auto n : g->fast_class()) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto sio = n.get_subnode_io();
      if (sio == nullptr
          || (sio->get_name() != gu::fproperty_module_name && sio->get_name() != gu::lgassert_module_name)) {
        continue;
      }
      // ASSERTS ONLY. An `fproperty` carries "<kind>\x1f<loc>\x1f<msg>" in its
      // name attr, and the kinds are assert | assert_always | assume. Guarding
      // an ASSUME to the period boundary is not a weakening, it is a semantic
      // change: an environment assume must constrain EVERY step, and
      // `boundary implies cond` leaves the mid-period steps unconstrained. The
      // engine then finds a mid-period input assignment the assume was there to
      // exclude, the assume gets reclassified and refutes, and it takes the
      // correct assert down with it -- a FALSE REFUTED at P>1 on a design whose
      // only sin was having an input assume.
      {
        const auto nm   = std::string(gu::node_name_of(n));
        const auto sep  = nm.find('\x1f');
        const auto kind = sep == std::string::npos ? nm : nm.substr(0, sep);
        if (!kind.empty() && kind.rfind("assert", 0) != 0) {
          continue;  // assume (or any future non-assert kind): leave it alone
        }
      }
      const auto      cond_pid = sio->get_input_port_id("cond");
      hhds::Pin_class cond;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == cond_pid) {
          cond = e.driver;
          break;
        }
      }
      if (cond.is_invalid()) {
        continue;
      }
      // boundary implies cond  ==  !boundary | cond
      auto notb = gu::create_typed_node(*g, Ntype_op::Not);
      boundary.connect_sink(notb.create_sink_pin(0));
      auto nbq = notb.create_driver_pin(0);
      gu::set_bits(nbq, 1);
      gu::set_unsign(nbq);
      auto orn = gu::create_typed_node(*g, Ntype_op::Or);
      nbq.connect_sink(orn.create_sink_pin(0));
      cond.connect_sink(orn.create_sink_pin(0));
      auto oq = orn.create_driver_pin(0);
      gu::set_bits(oq, 1);
      gu::set_unsign(oq);
      std::vector<hhds::Edge_class> doomed;
      for (const auto& e : n.inp_edges()) {
        if (e.sink.get_port_id() == cond_pid) {
          doomed.push_back(e);
        }
      }
      for (const auto& e : doomed) {
        e.del_edge();
      }
      oq.connect_sink(n.create_sink_pin("cond"));
    }
  }

  r.applied   = true;
  r.slots     = plan.slots;
  r.ref_clock = plan.ref_net;
  r.reason  = std::format("P={} slots, {} latch(es) retyped, {} gated clock(s) folded into an enable, {} element(s) "
                          "slotted, {} memory(ies) slotted ({})",
                          r.slots, r.latches_retyped, r.icg_folded, r.flops_slotted, r.memories_slotted,
                          need.why.empty() ? "def-driven" : need.why);
  livehd::diag::info(kPass, "single-edge-applied", "progress")
      .msg("edge normalization on `{}`: {}", g->get_name(), r.reason)
      .emit();
  return r;
}

}  // namespace livehd::single_edge

// ---------------------------------------------------------------------------
// Eprp surface

static Pass_plugin single_edge_plugin("pass_single_edge", Pass_single_edge::setup);

Pass_single_edge::Pass_single_edge(const Eprp_var& var) : Pass("pass.single_edge", var) {}

void Pass_single_edge::setup() {
  Eprp_method m("pass.single_edge",
                "Edge normalization: remove latches and negedge state so every element is a posedge flop (verification "
                "and simulation only; never on the synthesis path)",
                &Pass_single_edge::work);
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  register_pass(m);
}

void Pass_single_edge::work(Eprp_var& var) {
  const auto top = std::string{var.get("top", "")};
  const auto out = std::string{var.get("out", "")};

  std::vector<hhds::Graph*> all;
  hhds::Graph*              g = nullptr;
  for (const auto& sp : var.graphs) {
    if (!sp) {
      continue;
    }
    all.push_back(sp.get());
    if (!top.empty() && sp->get_name() == top) {
      g = sp.get();
    }
  }
  if (g == nullptr) {
    if (all.size() == 1) {
      g = all.front();
    } else {
      livehd::diag::err("pass.single_edge", "no-top", "unsupported")
          .msg("pass.single_edge: top module '{}' not found in the input library", top)
          .fatal();
      return;
    }
  }
  std::vector<hhds::Graph*> defs;
  for (auto* d : all) {
    if (d != g) {
      defs.push_back(d);
    }
  }

  // Same pre-step the formal commands run: bring an instantiated clock-gate
  // cell's gate into the body so the fold below can see it.
  if (const int nc = lc::inline_clock_gate_cells(g, "pass.single_edge"); nc > 0) {
    livehd::diag::info("pass.single_edge", "clock-gate-inlined", "progress")
        .msg("pass.single_edge: inlined {} clock-gate cell(s) into `{}`", nc, g->get_name())
        .emit();
  }
  const auto res = livehd::single_edge::normalize(g, defs, {});
  if (res.error) {
    livehd::diag::err("pass.single_edge", "normalize-refused", "unsupported")
        .msg("pass.single_edge refused '{}': {}", g->get_name(), res.reason)
        .fatal();
    return;
  }
  if (!res.applied) {
    livehd::diag::info("pass.single_edge", "single-edge-skipped", "progress")
        .msg("pass.single_edge on `{}`: {}", g->get_name(), res.reason)
        .emit();
  }
  if (!out.empty()) {
    // The rewrite happened IN PLACE in the input library, so emitting is a
    // straight cross-library copy of every reachable module (the caller already
    // refused out == in).
    auto* srclib = g->get_io() != nullptr ? g->get_io()->get_library() : nullptr;
    if (srclib == nullptr) {
      livehd::diag::err("pass.single_edge", "no-library", "internal")
          .msg("pass.single_edge: the input graphs have no owning library to copy from")
          .fatal();
      return;
    }
    auto& outlib = livehd::Hhds_graph_library::instance(out);
    for (const auto& sp : var.graphs) {
      if (sp && !outlib.copy_from(*srclib, sp->get_name())) {
        livehd::diag::err("pass.single_edge", "copy-failed", "internal")
            .msg("pass.single_edge: could not copy module '{}' into {}", sp->get_name(), out)
            .fatal();
        return;
      }
    }
  }
}
