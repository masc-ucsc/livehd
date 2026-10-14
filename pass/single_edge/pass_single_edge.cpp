// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_single_edge.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <set>
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
  hhds::Node_class            node;
  lc::Commit_class            cc;
  bool                        is_latch = false;
  int                         slot     = 0;
  // A decoded `<clock> & <enables>` clock cone. Present => this element's
  // gated clock is rewritten into its ENABLE (see apply_icg): it commits on the
  // reference clock's edge iff the enables hold, which is exactly what the
  // encoder models natively and strictly cheaper than a per-step commit term.
  std::optional<lc::Icg_cone> icg;
  // This element samples on the edge the enable latch OPENS at -- a FALLING
  // consumer of a low-transparent ICG. The arm bypass is not exact there, so
  // the GATE GUARD (and only the gate guard) is sampled in the rising slot and
  // held; see apply_icg's hold path.
  bool                        icg_hold = false;
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
    if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
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
    for (auto sink : n.inp_sorted_pins()) {  // read-only pin walk
      // PLURAL: a compact loop's carry-in sink holds two drivers
      // (pass/legalize/legalize.cpp:301).
      for (const auto& drv : sink.get_driver_pins()) {
        work.push_back(drv);
      }
    }
  }
}

hhds::Pin_class sink_driver(const hhds::Node_class& n, std::string_view pin) { return gu::get_driver_of_sink_name(n, pin); }

// ---------------------------------------------------------------------------
// Memory ports under a phase divider.
//
// A `Memory` cell's pins are PER PORT: raw_pid = base + port * stride, which is
// how `parse_memory_info` recovers the port id.
//
// PORT DISCOVERY FOLLOWS THE NATIVE CONTRACT.  A port exists when ANY of its
// fields is driven -- `addr`, `din`, `enable`, `clock_pin` -- and `rdport`
// DEFAULTS TO FALSE, i.e. a write port (`Memory_port_info::rdport = false` in
// pass/lean/graph_access.hpp, which cgen and the Lean scanner both rely on).
// Discovering ports from `rdport` pins alone would make a port that carries
// `addr`/`din`/`enable` but no explicit `rdport` INVISIBLE, and a writable cell
// would then be mistaken for a ROM and left ungated.  That is a correctness
// hole, not a cosmetic one.
struct Mem_port {
  uint32_t        port      = 0;
  bool            rdport    = false;  // native default: a WRITE port
  bool            has_rdpin = false;
  hhds::Pin_class enable;
  hhds::Pin_class clock;
  bool            has_addr = false;
  bool            has_din  = false;
};

struct Mem_shape {
  bool                  parsed      = true;   // every consulted policy pin was constant
  bool                  sync_read   = false;  // `type != 0`: a REGISTERED read
  bool                  rising      = true;   // `posclk == 1`
  bool                  mixed_edge  = false;  // `posclk == Ntype::Memory_posclk_mixed`
  bool                  whole_array = false;  // `update`/`update_enable`/`reset` driven
  uint32_t              wensize     = 0;
  std::vector<Mem_port> ports;
};

Mem_shape mem_shape_of(const hhds::Node_class& n) {
  Mem_shape                                   ms;
  const auto                                  stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  absl::flat_hash_map<uint32_t, Mem_port>     byport;
  auto at = [&](uint32_t port) -> Mem_port& {
    auto& p = byport[port];
    p.port  = port;
    return p;
  };
  for (const auto& e : gu::inp_sink_drivers(n)) {
    const auto raw   = static_cast<uint32_t>(e.get_port_id());
    const auto pname = std::string(Ntype::get_sink_name(Ntype_op::Memory, raw % stride));
    const auto port  = raw / stride;
    if (pname == "addr") {
      at(port).has_addr = true;
    } else if (pname == "din") {
      at(port).has_din = true;
    } else if (pname == "enable") {
      at(port).enable = e.driver;
    } else if (pname == "clock_pin") {
      at(port).clock = e.driver;
    } else if (pname == "rdport") {
      if (!e.driver.is_const() || !gu::const_of(e.driver).is_just_i64()) {
        ms.parsed = false;
        continue;
      }
      auto& p     = at(port);
      p.rdport    = gu::const_of(e.driver).to_just_i64() != 0;
      p.has_rdpin = true;
    } else if (pname == "update" || pname == "update_enable" || pname == "reset") {
      // A WHOLE-ARRAY cell: one bulk next-state bus instead of per-entry write
      // ports, and `reset` restores `init` at RUNTIME.  cgen_verilog and
      // pass/lean/graph_access.cpp classify these pins independently of the
      // per-entry write ports, so their presence must be recorded on its own
      // and NOT inferred from whether a write port exists.
      ms.whole_array = true;
    } else if (pname == "type" || pname == "wensize" || pname == "posclk") {
      if (!e.driver.is_const() || !gu::const_of(e.driver).is_just_i64()) {
        ms.parsed = false;
        continue;
      }
      const auto v = gu::const_of(e.driver).to_just_i64();
      if (pname == "type") {
        ms.sync_read = v != 0;
      } else if (pname == "wensize") {
        ms.wensize = v > 0 ? static_cast<uint32_t>(v) : 0;
      } else {
        // `posclk` is TRISTATE: 1 = rising, 0 = falling, and
        // `Ntype::Memory_posclk_mixed` = the ports do not share an edge.
        // Reading "nonzero == rising" would silently call a MIXED cell rising.
        ms.mixed_edge = v == Ntype::Memory_posclk_mixed;
        ms.rising     = v == 1;
      }
    }
  }
  for (auto& [port, p] : byport) {
    // A block carrying only scalar policy pins is not a port.
    if (p.has_addr || p.has_din || !p.enable.is_invalid() || !p.clock.is_invalid() || p.has_rdpin) {
      ms.ports.push_back(p);
    }
  }
  std::sort(ms.ports.begin(), ms.ports.end(), [](const Mem_port& a, const Mem_port& b) { return a.port < b.port; });
  return ms;
}

// ONE VALIDATED WRITE PORT, FROZEN BEFORE ANY MUTATION.
//
// Every fact the gate needs is decided and checked during the refusal pass and
// then CONSUMED from here: nothing is recomputed at mutation time, and there is
// no slot fallback.  A port that could not be classified never reaches this
// vector -- the pass has already returned a refusal.
struct Mem_gate_plan {
  hhds::Node_class node;
  uint32_t         port     = 0;
  hhds::Pin_class  enable;            // INVALID means "absent" -> all lanes write
  uint32_t         wensize  = 0;
  int              slot     = 0;
  // A GATED write clock, decided during the refusal scan and frozen here: the
  // gate's 1-bit condition (already latch-bypassed and phase-checked) and the
  // ROOT its clock resolves to. Invalid `gate` means the port was ungated.
  hhds::Pin_class  gate;
  hhds::Pin_class  clk_root;
};

// A cell with no committing port: an immutable asynchronous ROM.  `update`,
// `update_enable` and whole-array reset cells all carry a write port and are
// NOT this.
bool mem_is_immutable_rom(const Mem_shape& ms) {
  if (ms.sync_read || ms.whole_array) {
    // A registered read commits whatever the write side is, and a whole-array
    // cell commits through its bulk bus even with no per-entry write port --
    // so neither is an immutable ROM, and `update`/`update_enable`/`reset`
    // must be judged BEFORE any ROM classification.
    return false;
  }
  for (const auto& p : ms.ports) {
    if (!p.rdport) {
      return false;
    }
  }
  return true;
}

// The per-port sink pin, by name, at a given port.
hhds::Pin_class mem_port_driver(const hhds::Node_class& n, std::string_view pin, uint32_t port) {
  const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  const auto want   = Ntype::get_sink_pid(Ntype_op::Memory, pin) + port * stride;
  for (const auto& e : gu::inp_sink_drivers(n)) {
    if (static_cast<uint32_t>(e.get_port_id()) == want) {
      return e.driver;
    }
  }
  return hhds::Pin_class{};
}

void mem_drop_port_sink(const hhds::Node_class& n, std::string_view pin, uint32_t port) {
  const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  const auto want   = Ntype::get_sink_pid(Ntype_op::Memory, pin) + port * stride;
  absl::InlinedVector<hhds::Pin_class, 4> doomed;
  for (auto sink : n.inp_sorted_pins()) {
    if (static_cast<uint32_t>(sink.get_port_id()) == want) {
      doomed.push_back(sink);
    }
  }
  for (const auto& sink : doomed) {
    sink.del_sink();
  }
}



void drop_sink(const hhds::Node_class& n, std::string_view pin) {
  const auto pid = Ntype::get_sink_pid(gu::type_op_of(n), pin);
  if (pid == hhds::Port_invalid) {
    return;
  }
  // Snapshot the pins, then delete: inp_sorted_pins() is a view over live
  // storage, so the del_sink() calls must land after the walk (the hhds rule
  // the edge form obeyed by collecting `doomed` first).
  absl::InlinedVector<hhds::Pin_class, 4> doomed;
  for (auto sink : n.inp_sorted_pins()) {
    if (sink.get_port_id() == pid) {
      doomed.push_back(sink);
    }
  }
  for (const auto& sink : doomed) {
    sink.del_sink();  // one driver per sink pin
  }
}

std::string label_of(const hhds::Node_class& n) { return gu::debug_name(n); }

void refuse(bool quiet, std::string_view code, const std::string& msg, std::string_view hint) {
  if (quiet) {
    return;
  }
  livehd::diag::err(kPass, code, "unsupported").msg("{}", msg).hint(hint).emit();
}

// Why a Clock_cell GATE CHAIN cannot be folded into ONE endpoint enable, or
// `None`. One walk, but the reason is reported back so each refusal keeps the
// diagnostic code and wording it already had.
enum class Chain_refusal { None, Invert, Div, Unbounded };

// The WHOLE chain has to be walked, not just the outermost cell, because
// `clock_op_of` absorbs every cell's enable into ONE endpoint enable:
//
//   invert — an active-low cell buried in the chain contributes an enable the
//     hardware samples before the FALL, which the fold would then evaluate at
//     the reference RISE: half a period out, and silent.
//   div — worse than invisible. `absorb_chain` does `outer.div = inner.div`, so
//     an OUTER cell with div=2 over an inner div=1 folds to a cone reading
//     div == 1 and the `e.icg->div != 1` backstop below never fires; the
//     divider then gets retimed into an endpoint enable, which is exactly the
//     clock-blindness class the div refusal exists to stop.
//
// single_edge is deliberately STRICTLY MORE CONSERVATIVE than the same walk in
// pass/lec/phase_sched.cpp (`resolve_chain`): that one sets `guard_before_fall`
// only for a CONSTANT `invert` and only refuses a CONSTANT `div`, while a
// NON-CONSTANT one refuses here — its sample phase / divisor is not decidable,
// and single_edge's only move is to decline, never to mis-schedule. Do NOT
// "fix" the divergence by relaxing this side; the two are allowed to disagree
// in the refusing direction, not in the accepting one.
//
// Scan ONE MORE cell than the fold can absorb: `clock_op_depth` recurses while
// `depth < kMaxGateChain` starting at depth 0, so it folds up to
// kMaxGateChain+1 == 17 cells (graph/latch_contract.cpp). The extra hop is what
// lets a legal 17-cell chain run off its end (a non-Clock_cell root) instead of
// tripping the bound. kMaxGateChain lives in that file's anonymous namespace,
// so the value is duplicated here — keep the two in step.
constexpr int kMaxChainScan = 18;

Chain_refusal chain_refusal(hhds::Pin_class cell_out) {
  for (int hops = 0; hops < kMaxChainScan; ++hops) {
    if (cell_out.is_invalid()) {
      return Chain_refusal::None;  // an unconnected clk_ref: the `!e.icg` backstop below catches it
    }
    const auto cr = lc::control_root(cell_out, /*stop_at_clock_cell=*/true);
    if (cr.net.is_invalid() || gu::is_graph_input_pin(cr.net) || cr.net.is_const()) {
      return Chain_refusal::None;  // the chain ended on a real net
    }
    const auto cell = cr.net.get_master_node();
    if (gu::type_op_of(cell) != Ntype_op::Clock_cell) {
      return Chain_refusal::None;
    }
    // A NON-CONSTANT `invert` counts too: the sample phase is then not even
    // decidable here, so fail closed rather than assume the positive flavour.
    if (const auto inv = sink_driver(cell, "invert"); !inv.is_invalid() && (!inv.is_known_false())) {
      return Chain_refusal::Invert;
    }
    // Same fail-closed rule for `div`: `clock_cell_cone` only reads the pin when
    // it is CONST, so a non-constant divisor leaves `Icg_cone::div` at its 1
    // default and reads downstream as "no division at all".
    if (const auto d = sink_driver(cell, "div"); !d.is_invalid()) {
      if (!d.is_const()) {
        return Chain_refusal::Div;
      }
      const auto& dv = gu::const_of(d);
      if (!dv.is_just_i64() || dv.to_just_i64() != 1) {
        return Chain_refusal::Div;
      }
    }
    cell_out = sink_driver(cell, "clk_ref");
  }
  // The bound is exhausted — a chain deeper than the fold can absorb, or a
  // cyclic clock cone (nothing type-checks one). FAIL CLOSED: `clock_op_of`
  // absorbed those cells into one endpoint enable regardless, so returning
  // "nothing wrong" here would ship precisely the silent mis-timing this walk
  // exists to catch.
  return Chain_refusal::Unbounded;
}

// WHICH BIT of a clock root a control cone reads. `control_root` follows every
// Get_mask to the WHOLE input (tolg relies on that), so `clks[0]` and `clks[1]`
// share one commit class keyed `clks`. This walks the same hops while tracking
// the bit the consumer's edge is detected on (bit 0 of the driver), the way
// pass/lec/encode.cpp's Clk_root does, and returns it once the walk lands on
// `root`. nullopt = the bit is not decidable here (a non-constant select, a
// boolean of a multi-bit value, an ambiguous gate): every caller fails closed.
//
// A two-operand And is a clock GATE (`clk & en`, the shape `resolve_icg`
// folds): bit-wise, so the clock bit is the bit of whichever operand reaches
// `root`. Both operands reaching it (`clks[0] & clks[1]`) is ambiguous.
std::optional<int> clock_bit_on(hhds::Pin_class p, const hhds::Pin_class& root, int bit, int depth) {
  for (int hops = 0; hops < 64 && depth < 8; ++hops) {
    if (p.is_invalid() || p.is_const()) {
      return std::nullopt;
    }
    if (gu::is_graph_input_pin(p)) {
      if (p.get_class_index() != root.get_class_index()) {
        return std::nullopt;
      }
      if (const int w = gu::bits_of(p); w > 0 && bit >= w) {
        return std::nullopt;  // a bit past the input: its extension, not a clock bit
      }
      return bit;
    }
    const auto n  = p.get_master_node();
    const auto op = gu::type_op_of(n);
    // A BOOLEAN of the value (`c ? 1 : 0`, `c == 0/1`): only bit 0 of the
    // result carries it, and it is a single bit of `c` only when `c` is 1 bit.
    auto follow_bool = [&](const hhds::Pin_class& c) -> bool {
      if (bit != 0 || c.is_invalid() || gu::bits_of(c) != 1) {
        return false;
      }
      p = c;
      return true;
    };
    if (op == Ntype_op::Mux) {
      hhds::Pin_class sel, arm0, arm1;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        const auto pid = in.get_port_id();
        (pid == 0 ? sel : pid == 1 ? arm0 : arm1) = in.driver;
      }
      const bool a0 = arm0.is_const() && gu::const_of(arm0).is_just_i64() && gu::const_of(arm0).to_just_i64() == 0;
      const bool a1 = arm1.is_const() && gu::const_of(arm1).is_just_i64() && gu::const_of(arm1).to_just_i64() == 0;
      if (arm0.is_invalid() || arm1.is_invalid() || a0 == a1 || !follow_bool(sel)) {
        return std::nullopt;
      }
      continue;
    }
    if (op == Ntype_op::EQ || op == Ntype_op::Xor) {
      hhds::Pin_class val;
      int             live = 0;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        if (!in.driver.is_const()) {
          val = in.driver;
          ++live;
        }
      }
      if (live != 1 || !follow_bool(val)) {
        return std::nullopt;
      }
      continue;
    }
    if (op == Ntype_op::Not) {
      p = gu::first_value_driver(n);  // bit-wise: the bit stays put
      continue;
    }
    if (op == Ntype_op::And || op == Ntype_op::Or) {
      std::vector<hhds::Pin_class> live;
      for (const auto& in : gu::inp_sink_drivers(n)) {
        if (in.driver.is_const()) {
          // A constant must leave the tracked bit alone (set for And, clear for
          // Or); otherwise the bit is a constant, not a clock.
          const auto& c = gu::const_of(in.driver);
          if (!c.is_just_i64() || bit >= 62) {
            return std::nullopt;
          }
          const bool set = ((c.to_just_i64() >> bit) & 1) != 0;
          if (set != (op == Ntype_op::And)) {
            return std::nullopt;
          }
          continue;
        }
        live.push_back(in.driver);
      }
      if (live.size() == 1) {
        p = live.front();
        continue;
      }
      if (op != Ntype_op::And || live.empty()) {
        return std::nullopt;
      }
      std::optional<int> found;
      for (const auto& l : live) {
        if (auto b = clock_bit_on(l, root, bit, depth + 1)) {
          if (found && *found != *b) {
            return std::nullopt;
          }
          found = b;
        }
      }
      return found;
    }
    if (op == Ntype_op::Get_mask) {
      const auto range = gu::bit_range(n);
      if (!range || range->first + bit >= range->second) {
        return std::nullopt;
      }
      bit += range->first;
      p = gu::first_value_driver(n);
      continue;
    }
    if (op == Ntype_op::Sext) {
      if (bit != 0) {
        return std::nullopt;  // only bit 0 is position-independent of the sign point
      }
      p = gu::first_value_driver(n);
      continue;
    }
    if (op == Ntype_op::Clock_cell) {
      if (bit != 0) {
        return std::nullopt;
      }
      p = gu::get_driver_of_sink_name(n, "clk_ref");
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------

struct Plan {
  std::vector<Element>                  elems;
  absl::flat_hash_map<std::string, int> slot_of_key;
  int                                   slots = 1;
  bool                                  ok    = false;
  std::string                           ref_net;      // class key of the reference clock ("" = none)
  std::string                           why;          // failure reason when !ok
  std::string                           code;         // diagnostic code when !ok
  hhds::Pin_class                       ref_clk_pin;  // the reference clock net (may be invalid = implicit)
  // The BIT of `ref_clk_pin` the reference edge is on, when that net is a
  // multi-bit input (-1 = the net itself). The rewrite rebinds every clock_pin
  // to the reference, and a bare multi-bit input reads as its bit 0, so a bit
  // k>0 gets its own select there.
  int                                   ref_clk_bit = -1;
  // The reference clock's name for the cross-side agreement check: `ref_net`,
  // plus `[k]` for a bit k>0 of a bus (encode.cpp's Clk_root spelling), so a
  // side on clks[0] and one on clks[1] are two different time bases.
  std::string                           ref_clock_name;
};

Plan build_plan(hhds::Graph* g, const lc::Design_clocks& clocks) {
  Plan plan;

  // 1. Collect every state element with its commit class.
  absl::flat_hash_map<std::string, int> per_net;  // net_key -> #elements on it (clock role only)
  // Multi-bit clock inputs: net_key -> the distinct bits used ("?<elem>" = not
  // decidable), and the bit itself when decided.
  absl::flat_hash_map<std::string, std::set<std::string>> bus_bits;
  absl::flat_hash_map<std::string, int>                   bus_bit_of;
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Memory) {
      // A memory rides the reference clock. Leaving it untouched is correct at
      // P = 1 and WRONG at P > 1 (it would commit every sub-step), so it is
      // allowed here and rejected below once P is known.
      continue;
    }
    if (op != Ntype_op::Flop && op != Ntype_op::Fflop && op != Ntype_op::Latch) {
      continue;
    }
    const auto control            = sink_driver(n, op == Ntype_op::Latch ? "enable" : "clock_pin");
    const auto cr                 = lc::control_root(control, /*stop_at_clock_cell=*/true);
    const bool through_clock_cell = !cr.net.is_invalid() && !gu::is_graph_input_pin(cr.net) && !cr.net.is_const()
                                    && gu::type_op_of(cr.net.get_master_node()) == Ntype_op::Clock_cell;
    if (through_clock_cell) {
      // The positive gate flavour (`clk & en_latched`) can be retimed exactly
      // into the endpoint enable at the same reference edge. The active-low
      // flavour samples before the falling edge and needs a sub-period the slot
      // model does not have, so it remains a named refusal — ANYWHERE in the
      // chain, because the fold below absorbs every cell's enable into one.
      // Same reach for `div`: the per-cell scan is the ONLY place it is visible,
      // since the fold overwrites the outer cell's divisor with the inner one's
      // (the `e.icg->div != 1` test below stays as a backstop for the shapes
      // that never reach a Clock_cell chain at all).
      const auto refusal = chain_refusal(cr.net);
      if (refusal == Chain_refusal::Invert) {
        plan.code = "clock-cell-sample-phase";
        plan.why  = "state element `" + label_of(n)
                    + "` is clocked through an active-low Clock_cell whose enable sample phase cannot be represented";
        return plan;
      }
      if (refusal == Chain_refusal::Div) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n)
                    + "` is clocked through a Clock_cell chain that divides the clock, which cannot be retimed to one "
                      "endpoint enable";
        return plan;
      }
      if (refusal == Chain_refusal::Unbounded) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n)
                    + "` is clocked through a Clock_cell chain deeper than the fold can absorb, so it cannot be retimed "
                      "to one endpoint enable";
        return plan;
      }
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
      e.icg = through_clock_cell ? lc::clock_op_of(control, clocks) : lc::resolve_icg(control, clocks);
      if (through_clock_cell && (!e.icg || e.icg->div != 1)) {
        plan.code = "clock-cell-unsupported";
        plan.why  = "state element `" + label_of(n) + "` has a Clock_cell that cannot be retimed to one endpoint enable";
        return plan;
      }
      if (e.icg) {
        // Resolve the ENABLE-LATCH BYPASS here, during ANALYSIS, because the
        // rewrite loop retypes latches into flops as it goes -- so by the time
        // it reached this flop the enable latch might already be a Flop and the
        // bypass would silently not fire, leaving the enable a full cycle late.
        // (Measured exactly that way: the emitted netlist came out `if (enl)`,
        // the latch's held Q, instead of the value it was passing through.)
        //
        // A real ICG latches its enable on the opposite phase purely to
        // suppress glitches: the latch closes exactly when the flop samples, so
        // the flop sees what the latch was passing THROUGH. Reading its Q
        // instead is the classic L1 error.
        for (auto& en : e.icg->enables) {
          if (en.is_invalid() || en.is_const() || gu::is_graph_input_pin(en)) {
            continue;
          }
          auto dn = en.get_master_node();
          if (gu::type_op_of(dn) != Ntype_op::Latch) {
            continue;
          }
          // The bypass is exact ONLY for a latch on the OPPOSITE phase: open for
          // the whole phase before the gated edge, shut for the whole phase
          // after it. A latch open on the GATED phase (`if (clk) en_l = en;
          // clk & en_l`) passes a change of `en` straight through while the
          // gated clock is high -- an extra, glitch edge the real gate was built
          // to suppress -- so reading its arm PROVED that twin equal to a real
          // clock gate. The same phase rule pass/lec/phase_sched.cpp applies to
          // its own bypass; here it fails closed rather than mis-time.
          const bool closed_after = e.cc.rising;  // the reference level after the gated edge
          const bool opposite_here
              = lc::gated_latch_closed_at(dn, e.icg->clock, closed_after) && lc::latch_open_at(dn, e.icg->clock, !closed_after);
          // THE OTHER EDGE OF THE SAME GATE. One ICG legitimately drives both a
          // rising and a falling consumer: CORE-ET's half-cycle register file
          // (tech_generic/prim_rf_1r1w_preview -- `always_ff @(negedge
          // rf_clk_i)` for the data delay against `always_ff @(posedge
          // rf_clk_i)` for the array) sits behind minion_dcache_top's
          // `u_clk_gate`. For the FALLING consumer of a low-transparent gate
          // the latch OPENS at the sampling edge, so Q != D there and the arm
          // bypass is not exact -- but `clk & en_latch` still produces that
          // falling edge iff the value held across the high phase was 1. A
          // lowering question, not a refusal.
          //
          // FLOPS ONLY. A latch behind a gate has its own window and its own
          // commit rules, and a sampled enable is the wrong instrument for it.
          const bool opposite_for_other_edge = !e.cc.rising && !e.is_latch
                                               && lc::gated_latch_closed_at(dn, e.icg->clock, /*closed_level=*/true)
                                               && lc::latch_open_at(dn, e.icg->clock, /*level=*/false);
          if (opposite_for_other_edge && !opposite_here) {
            e.icg_hold = true;
          }
          if (!opposite_here && !opposite_for_other_edge) {
            plan.code = "clock-gate-latch-phase";
            plan.why  = "state element `" + label_of(n) + "` is clocked through a gate whose enable latch `" + label_of(dn)
                        + "` is not transparent exactly on the phase opposite the gated edge";
            return plan;
          }
          if (auto arm = lc::latch_transparent_arm(dn); !arm.is_invalid()) {
            en = arm;
          }
        }
      }
    }
    // A GATED LATCH is lowered to "commit at the closing edge iff the gate's
    // enable held", i.e. a gated-OFF clock HOLDS it. That is only the latch's
    // behavior when the gated-off clock keeps its window SHUT. A window open
    // while the gated clock is LOW (`if (!(clk & en_l)) p = d`, minion's
    // register-file preview latch) is transparent for the WHOLE period when
    // gated off, which no slot can express -- lowering it anyway PROVED it
    // equal to a latch that really holds. Decline -- unless every reader of the
    // latch is masked by that same gated clock (an ICG's own enable latch on a
    // gated clock), which makes the gated-off window unobservable.
    if (e.is_latch && e.icg && e.cc.role == lc::Net_role::Clock
        && !lc::gated_latch_closed_at(n, e.icg->clock, /*closed_level=*/e.cc.rising) && !lc::gated_latch_masked_off(n)) {
      plan.code = "gated-latch-open-when-off";
      plan.why  = "latch `" + label_of(n)
                  + "` has a gated clock window that stays transparent while the gate is off, which one commit edge "
                    "cannot represent";
      return plan;
    }
    plan.elems.push_back(e);
    if (cc->role == lc::Net_role::Clock) {
      ++per_net[cc->net_key()];
      // A clock that is ONE BIT of a multi-bit input: record WHICH bit. The
      // commit class is keyed by the whole input (control_root follows every
      // Get_mask to it), so clks[0] and clks[1] would otherwise share one slot
      // and one reference edge -- a flop moved between the bits normalized to
      // the identical graph. Only a DATA-role latch is exempt: its enable is
      // not a clock, it keeps that enable through the rewrite, and its slot
      // comes from the state the enable reads, never from this key.
      if (!cc->implicit_clock && !cc->net.is_invalid() && gu::is_graph_input_pin(cc->net) && gu::bits_of(cc->net) > 1) {
        const auto bit = clock_bit_on(control, cc->net, 0, 0);
        bus_bits[cc->net_key()].insert(bit ? std::to_string(*bit) : "?" + label_of(n));
        if (bit) {
          bus_bit_of[cc->net_key()] = *bit;
        }
      }
    }
  }
  // Decline only when the bits are AMBIGUOUS: two different bits of one input
  // (or a bit this walk cannot decide) clock the design. One bit, used alone,
  // is a single clock and normalizes like any other (the reference rebind below
  // selects that bit). Sorted for a deterministic diagnostic.
  {
    std::vector<std::string> keys;
    for (const auto& [k, bits] : bus_bits) {
      if (bits.size() > 1 || !bus_bit_of.contains(k)) {
        keys.push_back(k);
      }
    }
    std::sort(keys.begin(), keys.end());
    if (!keys.empty()) {
      std::string list;
      for (const auto& b : bus_bits[keys.front()]) {
        list += (list.empty() ? "" : ", ") + (b.front() == '?' ? "an undecidable bit (`" + b.substr(1) + "`)" : "bit " + b);
      }
      plan.code = "clock-bus-bit";
      plan.why  = "the multi-bit input `" + keys.front() + "` clocks state on " + list
                 + "; edge normalization keys a clock by its input net and cannot tell the bits of one bus apart";
      return plan;
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
  plan.ref_net        = ref_net;
  plan.ref_clock_name = ref_net;
  plan.ref_clk_pin    = {};
  if (auto it = bus_bit_of.find(ref_net); it != bus_bit_of.end()) {
    plan.ref_clk_bit = it->second;
    if (it->second != 0) {
      plan.ref_clock_name += "[" + std::to_string(it->second) + "]";
    }
  }
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
    e.slot                                 = e.cc.rising ? 0 : 1;
    plan.slot_of_key[e.cc.key()]           = e.slot;
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
    // depend on body().nodes() iteration order, and the task page's condition 2
    // is binding: slots are derived structurally, never from traversal order,
    // because the two miter sides run this independently and a disagreement is
    // a false REFUTED.
  }

  int max_slot = 0;
  for (const auto& e : plan.elems) {
    max_slot = std::max(max_slot, e.slot);
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
bool check_rule4(const Plan& plan, hhds::Graph* g, const lc::Design_clocks& clocks, bool quiet) {
  bool ok = true;
  // MEMORY WRITE PORTS ARE READERS TOO.
  //
  // `plan.elems` holds flops and latches, so when memories were refused
  // outright they could not be on either side of this rule.  Now that a write
  // port is admitted and slotted it reads `addr`, `din` and `enable`
  // combinationally, and if one of those cones reaches a latch that closes on
  // the SAME edge the port commits on, it is the identical L1 hazard: the real
  // hardware samples the latch's D, the lowering gives it the held Q.  Leaving
  // Memory out of the candidate list would bypass rule 4 exactly where the new
  // support created the exposure.
  struct Mem_reader {
    hhds::Node_class node;
    lc::Commit_class cc;
    uint32_t         port = 0;
  };
  std::vector<Mem_reader> mem_readers;
  if (g != nullptr) {
    for (auto n : g->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Memory) {
        continue;
      }
      const auto ms = mem_shape_of(n);
      if (!ms.parsed || mem_is_immutable_rom(ms)) {
        continue;
      }
      for (const auto& mp : ms.ports) {
        if (mp.rdport) {
          continue;
        }
        auto cc = lc::commit_class_of_clock(mp.clock.is_invalid() ? plan.ref_clk_pin : mp.clock, ms.rising, &clocks);
        if (cc && cc->role == lc::Net_role::Clock) {
          mem_readers.push_back({n, *cc, mp.port});
        }
      }
    }
  }
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
               label_of(l.node),
               label_of(s.node),
               label_of(s.node))
          .hint(
              "the latch is still transparent at that edge, so the real hardware samples its D while the "
              "commit-at-closing-edge model samples its held Q — a persistent full-cycle error. Move the two onto "
              "opposite phases (a master/slave pair), or replace the latch with a buffer if it is meant to be one")
          .emit();
      break;
    }
    // ...and the same test against every admitted memory write port.
    for (const auto& mr : mem_readers) {
      if (mr.cc.key() != l.cc.key()) {
        continue;
      }
      absl::flat_hash_set<hhds::Class_index> hit;
      for (const char* field : {"addr", "din", "enable"}) {
        comb_state_reach(mem_port_driver(mr.node, field, mr.port), hit);
      }
      if (!hit.contains(l.node.get_class_index())) {
        continue;
      }
      ok = false;
      if (quiet) {
        return false;
      }
      livehd::diag::err(kPass, "coincident-commit-edge", "unsupported")
          .msg("latch `{}` and memory `{}` write port {} commit at the SAME edge, and the port reads the latch "
               "combinationally",
               label_of(l.node),
               label_of(mr.node),
               mr.port)
          .hint(
              "the latch is still transparent at that edge, so the real hardware writes its D while the "
              "commit-at-closing-edge model writes its held Q. Move the latch and the memory onto opposite phases, "
              "or replace the latch with a buffer if it is meant to be one")
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
  return pc.is_known_false();
}

// True when `din` carries tolg's hold mux (`cond ? d : q`), i.e. a Mux on din
// with this latch's own Q as an arm. An active-LOW latch that ALSO has one is a
// shape no emitter produces: the hold would be keyed on the un-inverted
// condition while the enable test is inverted, so the two disagree. Refuse it.
bool has_hold_mux(const hhds::Node_class& latch) {
  auto q   = latch.get_driver_pin(0);
  auto din = sink_driver(latch, "din");
  if (q.is_invalid() || din.is_invalid() || din.is_const() || gu::is_graph_input_pin(din)) {
    return false;
  }
  auto n = din.get_master_node();
  if (gu::type_op_of(n) != Ntype_op::Mux) {
    return false;
  }
  for (auto sink : n.inp_sorted_pins()) {
    const auto drv = sink.get_driver_pin();
    if (!drv.is_invalid() && drv.get_class_index() == q.get_class_index()) {
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

  const lc::Design_clocks           clocks(g);
  const auto                        need = lc::needs_single_edge(g, &clocks);
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
      for (auto n : cur->body().nodes()) {
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
  bool                              def_needs = false;
  std::string                       def_why;  // which def, and WHAT is in it (the diagnostic must say)
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
    refuse(quiet,
           "hier-unsupported",
           std::format("{}: {}", g->get_name(), r.reason),
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
    refuse(quiet,
           plan.code,
           std::format("{}: {}", g->get_name(), plan.why),
           "edge normalization declines rather than half-transform: a partial lowering is a silent full-cycle error");
    return r;
  }
  plan.slots = std::max(plan.slots, opts.force_slots);
  if (!check_rule4(plan, g, clocks, quiet)) {
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
  //
  // Every memory write port that survives this scan is recorded in
  // `mem_gate_plans` with the facts the gate will use.  The gate below consumes
  // them and recomputes nothing.
  std::vector<Mem_gate_plan> mem_gate_plans;
  for (auto n : g->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Fflop) {
      r.error  = true;
      r.reason = "fluid flop `" + label_of(n) + "` has no commit-class model";
      refuse(quiet, "fflop-unsupported", std::format("{}: {}", g->get_name(), r.reason), "");
      return r;
    }
    if (op == Ntype_op::Memory && plan.slots > 1) {
      // The blanket refusal this replaces fired on EVERY Memory at P>1.  What
      // actually mis-times under a phase divider is a port that COMMITS on a
      // sub-step it does not own, so the shape and the CLOCK decide.
      //
      // Every refusal here happens BEFORE any mutation: the plan is built and
      // validated first, and a design that cannot be lowered is returned
      // untouched rather than half-rewritten.
      const auto ms = mem_shape_of(n);
      if (!ms.parsed) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a non-constant type/wensize/posclk/rdport pin";
        refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
               "the port shape must be constant before a slot gate can be placed");
        return r;
      }
      // JUDGED BEFORE ANY ROM CLASSIFICATION.
      if (ms.whole_array) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` is a WHOLE-ARRAY cell (`update`/`update_enable`/`reset` driven)";
        refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
               "one bulk next-state bus instead of per-entry write ports; this pass gates per-entry write "
               "enables and has no model for the bulk commit");
        return r;
      }
      if (ms.mixed_edge) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` declares MIXED port polarity (posclk == Memory_posclk_mixed)";
        refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
               "its ports do not share a committing edge, so one slot cannot describe the cell");
        return r;
      }
      if (mem_is_immutable_rom(ms)) {
        continue;  // nothing commits: an immutable asynchronous ROM
      }
      if (ms.sync_read) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a synchronous read port; its read register would commit on every sub-step";
        refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
               "synchronous read-port timing is not preserved by the slot gate yet");
        return r;
      }
      if (ms.wensize == 0) {
        r.error  = true;
        r.reason = "memory `" + label_of(n) + "` has a committing port but no positive wensize";
        refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
               "the write-enable width must be known to gate the whole vector");
        return r;
      }
      // THE CLOCK, per committing port, through the SAME classifier a flop
      // uses.  `commit_class_of` returns nullopt for a Memory by contract, so
      // calling it here would have silently yielded slot 0 for every port
      // whatever its clock or `posclk` was.
      for (const auto& port : ms.ports) {
        if (port.rdport) {
          continue;  // an async read is combinational: re-evaluated every sub-step
        }
        // AN ABSENT ENABLE MEANS ALL LANES WRITE.
        //
        // That is the native contract, not a default invented here: both the
        // pinned old and the master simulator's `emit_wen` spell an invalid
        // enable pin as `Slop<wensize>::create_integer(-1)`
        // (inou/cgen/cgen_sim.cpp), and the LLVM path agrees.  An earlier
        // comment here claimed the contract was silent; it is not, and that
        // claim is withdrawn.  The gate below needs no wide constant for it:
        // the slot mask IS the gated enable when every lane is enabled.
        const auto clk_pin = port.clock.is_invalid() ? plan.ref_clk_pin : port.clock;
        // A GATED clock is refused.  `commit_class_of_clock` resolves an ICG to
        // its ROOT net and drops the gate's enable conditions -- which the flop
        // path then re-introduces by folding them into the element's `enable`.
        // This pass does no such folding for a Memory, so accepting a gated
        // memory clock would silently discard the gate.
        // A GATED write clock is FOLDED INTO THE WRITE ENABLE, the same move the
        // flop path makes -- `commit_class_of_clock` resolves an ICG to its
        // ROOT and drops the gate's conditions, so they have to be
        // re-introduced or the gate is silently discarded.
        //
        // The vector width is what makes this different from a flop. A write
        // enable is `wensize` bits, one per lane, so the gate cannot simply be
        // ANDed in: it is combined with the slot predicate and the pair is
        // widened by the SAME Sext + Get_mask idiom the slot mask already uses
        // (see the long note at the rewrite), which is what keeps every lane
        // and mints no wide shared constant.
        //
        // Everything here is decided ONCE, during this scan, and frozen into
        // the plan; the rewrite reclassifies nothing.
        hhds::Pin_class port_gate;
        hhds::Pin_class port_root;
        if (!clk_pin.is_invalid()) {
          if (const auto icg = lc::resolve_icg(clk_pin, clocks)) {
            if (icg->div != 1) {
              r.error  = true;
              r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                         + " has a gated clock with div=" + std::to_string(icg->div);
              refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                     "a divided memory clock is a second commit class, not a gate");
              return r;
            }
            // THE ENABLE-LATCH BYPASS, with the SAME phase proof the flop path
            // demands. A real ICG latches its enable on the opposite phase, so
            // the latch CLOSES as the memory samples and the cell sees what it
            // was passing through. Reading its held Q instead is a full cycle
            // late. A latch that is not opposite-phase is refused here rather
            // than mis-timed -- the sampled-held guard the flop path uses for a
            // FALLING consumer is deliberately NOT extended to a Memory in this
            // increment, and a falling memory is refused further down anyway.
            for (auto en : icg->enables) {
              if (!en.is_invalid() && !en.is_const() && !gu::is_graph_input_pin(en)) {
                if (auto dn = en.get_master_node(); gu::type_op_of(dn) == Ntype_op::Latch) {
                  const bool closed_after = ms.rising;
                  if (!lc::gated_latch_closed_at(dn, icg->clock, closed_after)
                      || !lc::latch_open_at(dn, icg->clock, !closed_after)) {
                    r.error  = true;
                    r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                               + " is clocked through a gate whose enable latch `" + label_of(dn)
                               + "` is not transparent exactly on the phase opposite the gated edge";
                    refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                           "edge normalization declines rather than half-transform: a partial lowering is a "
                           "silent full-cycle error");
                    return r;
                  }
                  if (auto arm = lc::latch_transparent_arm(dn); !arm.is_invalid()) {
                    en = arm;
                  }
                }
              }
              if (en.is_invalid()) {
                continue;
              }
              if (port_gate.is_invalid()) {
                port_gate = en;
              } else {
                auto andn = gu::create_typed_node(*g, Ntype_op::And);
                port_gate.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
                en.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
                port_gate = andn.create_driver_pin(0);
                gu::set_bits(port_gate, 1);
                gu::set_unsign(port_gate);
              }
            }
            if (port_gate.is_invalid()) {
              r.error  = true;
              r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                         + " has a gated clock whose gate carries no usable enable";
              refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                     "folding a gate with no condition would drop it");
              return r;
            }
            port_root = icg->clock;
          }
        }
        auto cc = lc::commit_class_of_clock(clk_pin, ms.rising, &clocks);
        if (!cc || cc->role != lc::Net_role::Clock) {
          r.error  = true;
          r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                     + " has a clock cone that does not resolve to a root net";
          refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                 "the committing edge must be classified before the port can be slotted");
          return r;
        }
        if (!cc->implicit_clock && cc->net_key() != plan.ref_net) {
          r.error  = true;
          r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                     + " commits on a net that is not the reference clock";
          refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                 "an off-reference memory clock is not rebound by this pass");
          return r;
        }
        // TWO BITS OF ONE CLOCK BUS ARE DIFFERENT CLOCKS.  `build_plan` already
        // tracks this with `clock_bit_on`/`ref_clk_bit`; comparing `net_key()`
        // alone would accept a memory clocked by a DIFFERENT bit of the same
        // multi-bit input and then gate it against the reference bit's phase.
        if (!cc->implicit_clock && !cc->net.is_invalid() && gu::is_graph_input_pin(cc->net)
            && gu::bits_of(cc->net) > 1) {
          const auto bit = clock_bit_on(clk_pin, cc->net, 0, 0);
          if (!bit || (plan.ref_clk_bit >= 0 && *bit != plan.ref_clk_bit)) {
            r.error  = true;
            r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                       + " commits on a different bit of the clock bus than the reference";
            refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                   "two bits of one clock bus are two clocks; this pass rebinds only the reference bit");
            return r;
          }
        }
        // A FALLING committing edge is REFUSED, not slotted.
        //
        // A negedge flop is lowered by RETYPING it to posedge and gating it to
        // the fall slot; the two together are what make the result
        // single-edge.  A Memory has no equivalent retype here yet -- this pass
        // does not touch `clock_pin` or `posclk` on a Memory -- so gating alone
        // would leave the cell still committing on the fall while ALSO being
        // slot-gated, i.e. a half-lowering whose emission still carries an
        // inverted memory clock.  Refuse it by name until the rebinding is
        // implemented and validated, rather than emit that.
        //
        // This is also what makes the per-port classification load-bearing in
        // this increment: its job here is to DECIDE ADMISSIBILITY.  Before it,
        // `commit_class_of` returned nullopt for every Memory and a falling
        // memory was silently gated to slot 0.
        if (!cc->rising) {
          r.error  = true;
          r.reason = "memory `" + label_of(n) + "` write port " + std::to_string(port.port)
                     + " commits on the FALLING edge";
          refuse(quiet, "memory-unsupported", std::format("{}: {}", g->get_name(), r.reason),
                 "a falling-edge memory would need its clock rebound and its polarity retyped, which this pass "
                 "does not yet do for a Memory; gating alone would leave an inverted memory clock in a "
                 "single-edge design");
          return r;
        }
        // THE SLOT, decided here and FROZEN.  There is no fallback: the class
        // resolved, is on the reference net and bit, and is rising, so either
        // the key is in `slot_of_key` or the rise/fall map gives it.
        int slot = 0;
        if (auto it = plan.slot_of_key.find(cc->key()); it != plan.slot_of_key.end()) {
          slot = it->second;
        } else {
          slot = cc->rising ? 0 : 1;  // the same map build_plan uses
        }
        mem_gate_plans.push_back({n, port.port, port.enable, ms.wensize, slot, port_gate, port_root});
      }
      // everything else is handled by the slot gate below
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto sub = n.get_subnode_graph();
    if (!sub) {
      continue;  // blackbox / property cell: no state of ours to re-time
    }
    // ...and only under a DIVIDER, exactly like the Memory case above. What
    // this refuses is re-timing a child we cannot reach: at P>1 the parent's
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
    for (auto sn : sub->body().nodes()) {
      if (gu::is_type_register(sn)) {
        child_has_state = true;
        break;
      }
    }
    if (child_has_state && plan.slots > 1) {
      r.error  = true;
      r.reason = "instance `" + label_of(n) + "` holds state; the phase divider is not port-threaded";
      refuse(quiet,
             "hier-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
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
      for (auto sink : n.inp_sorted_pins()) {
        const auto       drv = sink.get_driver_pin();
        // Clock ports only, by the SHARED spelling notion (Design_clocks::
        // name_looks_like_clock) rather than a private list, so this and the
        // rest of M8 agree on what a clock port is.
        std::string_view pname;
        for (const auto& d : csio->get_input_pin_decls()) {
          if (csio->get_input_port_id(d.name) == sink.get_port_id()) {
            pname = d.name;
            break;
          }
        }
        if (pname.empty() || !lc::Design_clocks::name_looks_like_clock(pname)) {
          continue;
        }
        // A PLAIN clock input (possibly through the width mask the readers add)
        // is untouched by this pass; anything else is in-graph logic.
        if (gu::is_graph_input_pin(drv) || (gu::type_op_of(drv.get_master_node()) == Ntype_op::Get_mask && [&] {
              for (auto gsink : drv.get_master_node().inp_sorted_pins()) {
                if (gu::is_graph_input_pin(gsink.get_driver_pin())) {
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
          refuse(quiet,
                 "hier-unsupported",
                 std::format("{}: {}", g->get_name(), r.reason),
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
      refuse(quiet,
             "raw-latch-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
             "a permanently transparent latch is a buffer, not a state element; drop the cell");
      return r;
    }
    if (has_hold_mux(e.node)) {
      r.error  = true;
      r.reason = "latch `" + label_of(e.node) + "` has an active-low polarity pin AND a hold mux on din";
      refuse(quiet,
             "raw-latch-unsupported",
             std::format("{}: {}", g->get_name(), r.reason),
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
    r.ref_clock = plan.ref_clock_name;
    r.reason    = std::format("plan: P={} slots ({})", plan.slots, need.why.empty() ? "forced" : need.why);
    return r;
  }

  // ---- the rewrite -------------------------------------------------------
  if (plan.ref_clk_bit > 0 && !plan.ref_clk_pin.is_invalid()) {
    // The reference edge is on bit k>0 of a bus input. Every rebound clock_pin
    // reads its edge on bit 0 of its driver, so binding the bare bus would move
    // the whole design onto clks[0]. Select the bit once and rebind to that.
    auto sel = gu::create_get_mask(*g, plan.ref_clk_pin, plan.ref_clk_bit).create_driver_pin(0);
    gu::set_bits(sel, 1);
    gu::set_unsign(sel);
    plan.ref_clk_pin = sel;
  }
  hhds::Pin_class slot_pred[2];
  // One sampled gate guard per GATE, not per consumer. The key is a PLANNED
  // identity -- the gate's clock root, its reference edge, and the guard cone's
  // own pins -- built BEFORE any node is created, which is what
  // `pass/lec/phase_sched.cpp` does (`root_key` + polarity + `cone_digest` per
  // guard, then `guard_cones`). Keying on a freshly built AND node instead
  // would give every COMPOUND guard a fresh id and so a separate held state,
  // sharing only the single-enable case by accident.
  absl::flat_hash_map<std::string, hhds::Pin_class> held_guards;
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
    // This divider describes the time base, independently of the design's
    // resets. Copying one state element's reset here changes the phase when
    // that reset is folded into mapped latch logic on the other side of LEC.
    // The encoder preserves this synthetic key's concrete initial value
    // (graph_util::is_single_edge_phase_key), including during after_reset,
    // so both designs advance through the same slots throughout the prologue.
    // P == 2: next = !phase (a 1-bit counter). P > 2 is refused above.
    auto nxt = gu::create_typed_node(*g, Ntype_op::Not);
    phq.connect_sink(livehd::graph_util::setup_sink_pid(nxt, 0));
    auto nxtq = nxt.create_driver_pin(0);
    gu::set_bits(nxtq, 1);
    gu::set_unsign(nxtq);
    nxtq.connect_sink(gu::setup_sink_by_name(ph, "din"));

    for (int k = 0; k < plan.slots; ++k) {
      auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
      phq.connect_sink(livehd::graph_util::setup_sink_pid(eq, 0));
      gu::create_const(*g, *Dlop::create_integer(k)).connect_sink(livehd::graph_util::setup_sink_pid(eq, 0));
      auto eqq = eq.create_driver_pin(0);
      gu::set_bits(eqq, 1);
      gu::set_unsign(eqq);
      slot_pred[k] = eqq;
    }
  }

  // ---- the memory slot gate ------------------------------------------------
  // A write port must commit ONLY on the sub-step its memory owns.  The flop
  // form above is `enable &= (phase == slot)` on a ONE-BIT enable; a write
  // enable is a VECTOR -- one bit per lane, `wensize` wide, and on the retained
  // B03/B11 graphs that is 16, 27, 34, 69, 70 or 8224 bits, never 1.  ANDing a
  // 1-bit predicate into it would keep lane 0 and silently drop every higher
  // lane, which is a different design.
  //
  // NO WIDE CONSTANT IS MINTED.  `gu::create_const` returns a CANONICAL SHARED
  // pin (`create_constant` deduplicates by value representation;
  // graph/node_util.hpp:326 and pinned HHDS hhds/graph.hpp:1815) and
  // `gu::set_bits` (node_util.hpp:838) writes that pin's own attribute, so a
  // `wensize` zero arm stamped with `set_bits` restamps the ONE zero every
  // other reader shares -- measured at b6b0a5cb6 as B11's shared zero going raw
  // width 0 -> 69 and B03's 0 -> 27, against Concat lanes of 9 and 1.
  //
  // The mask is therefore built from the predicate with the repository's own
  // sizing idiom (pass/abc/abc_satopt_test's `a AND Sext(c, 1)`): a SIGNED
  // sign-extension of the 1-bit predicate is all-ones when it holds and
  // all-zeros when it does not, at whatever width its own driver declares.
  // Every pin stamped below is this gate's OWN driver pin.
  //
  //     enable' = enable AND sext(phase == slot, 1)      (enable present)
  //     enable' =           sext(phase == slot, 1)       (enable ABSENT)
  //
  // The second line is the NATIVE contract for an absent enable -- all lanes
  // write, `Slop<wensize>::create_integer(-1)` in both the pinned old and the
  // master `emit_wen` (inou/cgen/cgen_sim.cpp) -- expressed without a wide
  // constant, because the mask already IS an all-ones vector of the right
  // width whenever the slot is active.
  //
  // Active: the ORIGINAL vector bit for bit, so every high lane survives.
  // Quiet: a width-correct all-zero vector, so no lane commits.  Geometry,
  // initial/default contents, `fwd`, `posclk`, read latency and the clock pins
  // are untouched.
  //
  // EVERY FACT HERE IS FROZEN.  `mem_gate_plans` was filled during the refusal
  // scan; nothing is reclassified here and there is no slot fallback.
  for (const auto& mp : mem_gate_plans) {
    // THE MASK MUST SURVIVE BITWIDTH, NOT JUST LEAVE THIS PASS WIDE.
    //
    // `Sext(pred, 1)` is a ONE-BIT SIGNED value (0 or -1).  Stamping 69 bits on
    // its driver here does NOT stick: `Bitwidth::process_sext` recomputes the
    // range from the POSITION operand -- `bw.set_sbits_range(sign_max)` with
    // `sign_max = 1` -- so the emitted node came out `Op_Sext width 1`, and the
    // certificate's `Op_And` does not sign-extend its operands.  The emitted
    // gate was therefore `And(enable_69, mask_1)`, which can only ever enable
    // bit 0: lanes 1..68 could never be written.  Measured in the retained v4
    // certificate at origin 6792.
    //
    // The widening idiom this design ALREADY uses is `Get_mask` against an
    // all-ones constant -- see origin 5268 in the same certificate,
    // `Op_GetMask w69 [Op_Sext w69, const70 2^69-1]`.  `mask_window_const`
    // builds that constant at any width, so the mask becomes a genuine
    // `wensize`-wide all-ones (predicate high) or all-zeros (predicate low),
    // and `Get_mask`'s own result width is what carries it through bitwidth.
    //
    // No BV or Sext semantics are changed and no unsigned operand is widened:
    // the only new value is this gate's own mask constant.
    // THE GATE JOINS THE PREDICATE, not the vector. Both are one bit here, so
    // the AND is a 1-bit AND and the Sext + Get_mask below widens the PAIR to
    // `wensize` -- the gate therefore reaches every lane, and no wide constant
    // is minted for it either.
    auto pred = slot_pred[mp.slot];
    if (!mp.gate.is_invalid()) {
      auto gandn = gu::create_typed_node(*g, Ntype_op::And);
      pred.connect_sink(livehd::graph_util::setup_sink_pid(gandn, 0));
      mp.gate.connect_sink(livehd::graph_util::setup_sink_pid(gandn, 0));
      pred = gandn.create_driver_pin(0);
      gu::set_bits(pred, 1);
      gu::set_unsign(pred);
    }
    auto sx = gu::create_typed_node(*g, Ntype_op::Sext);
    pred.connect_sink(sx.create_sink_pin(0));
    gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(sx.create_sink_pin(1));
    auto sxq = sx.create_driver_pin(0);
    gu::set_sbits(sxq, mp.wensize);

    auto gm = gu::create_typed_node(*g, Ntype_op::Get_mask);
    // `connect_mask_operands` installs the value AND the `lo`/`hi` endpoint
    // constants the readers require: `graph_util::bit_range` reads those pins,
    // and `pass.lean` refuses a Get_mask without them.  The window is
    // [0, wensize): every lane of the sign-extended predicate.
    livehd::graph_util::connect_mask_operands(gm, sxq, 0, static_cast<int>(mp.wensize));
    auto mask = gm.create_driver_pin(0);
    gu::set_bits(mask, mp.wensize);
    gu::set_unsign(mask);

    hhds::Pin_class gated;
    if (mp.enable.is_invalid()) {
      gated = mask;  // the native all-lanes default, at full width
    } else {
      auto andn = gu::create_typed_node(*g, Ntype_op::And);
      mp.enable.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
      mask.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
      gated = andn.create_driver_pin(0);
      gu::set_bits(gated, mp.wensize);
      gu::set_unsign(gated);
    }
    mem_drop_port_sink(mp.node, "enable", mp.port);
    const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);
    gated.connect_sink(mp.node.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "enable") + mp.port * stride));
    // THE GATED CLOCK MUST GO. Folding the gate into the enable while leaving
    // the gated net on `clock_pin` would apply the gate twice and, worse, leave
    // a DERIVED clock in a design this pass exists to make single-edge -- the
    // encoder refuses one, so the cell would come back UNKNOWN after a
    // successful lowering. Rebind to the root the gate resolved to.
    if (!mp.gate.is_invalid() && !mp.clk_root.is_invalid()) {
      mem_drop_port_sink(mp.node, "clock_pin", mp.port);
      mp.clk_root.connect_sink(
          mp.node.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin") + mp.port * stride));
      ++r.mem_ports_ungated;
    }
    ++r.mem_ports_slotted;
  }
  {
    absl::flat_hash_set<hhds::Class_index> seen;
    for (const auto& mp : mem_gate_plans) {
      if (seen.insert(mp.node.get_class_index()).second) {
        ++r.mems_slotted;
      }
    }
  }


  std::vector<hhds::Pin_class> orphaned_clocks;  // old clock_pin drivers the rebind below disconnects
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
        en.connect_sink(livehd::graph_util::setup_sink_pid(inv, 0));
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
          auto            q   = e.node.get_driver_pin(0);
          auto            mux = sink_driver(e.node, "din").get_master_node();
          hhds::Pin_class transparent;
          for (auto msink : mux.inp_sorted_pins()) {
            if (msink.get_port_id() == 0) {
              continue;  // the selector (the gate itself)
            }
            const auto mdrv = msink.get_driver_pin();
            if (!mdrv.is_invalid() && mdrv.get_class_index() != q.get_class_index()) {
              transparent = mdrv;
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
      const auto and1 = [&](const hhds::Pin_class& a, const hhds::Pin_class& b) {
        if (a.is_invalid()) {
          return b;
        }
        if (b.is_invalid()) {
          return a;
        }
        auto andn = gu::create_typed_node(*g, Ntype_op::And);
        a.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        b.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        auto aq = andn.create_driver_pin(0);
        gu::set_bits(aq, 1);
        gu::set_unsign(aq);
        return aq;
      };
      // THE PLANNED GUARD IDENTITY, from pins that already exist: the gate's
      // clock root, the reference edge it is sampled against, and every pin of
      // the guard cone, sorted so operand order cannot split one gate in two.
      // Nothing here allocates, so two consumers of the SAME gate agree on it
      // however many enables the gate has.
      std::string guard_key;
      {
        std::vector<uint64_t> cone;
        cone.reserve(e.icg->enables.size());
        for (const auto& en : e.icg->enables) {
          cone.push_back(en.is_invalid() ? 0 : static_cast<uint64_t>(en.get_class_index().value));
        }
        std::sort(cone.begin(), cone.end());
        guard_key = std::format("ck:{}:{}",
                                e.icg->clock.is_invalid() ? 0 : static_cast<uint64_t>(e.icg->clock.get_class_index().value),
                                e.cc.rising ? '+' : '-');
        for (const auto c : cone) {
          guard_key += std::format("&{}", c);
        }
      }

      // THE GATE GUARD, on its own. Only the gate's own enables go in here --
      // the element's LOCAL enable is deliberately left out and combined after
      // any sampling below.
      hhds::Pin_class gate_acc;
      for (const auto& en : e.icg->enables) {  // already latch-bypassed in build_plan
        gate_acc = and1(gate_acc, en);
      }

      // THE HOLD PATH, for a falling consumer of a low-transparent gate. The
      // gate reproduces its falling edge iff the value captured before the rise
      // and held across the high phase was 1, so that value -- not the live arm
      // -- is this consumer's gate condition. It is SAMPLED in the rising slot.
      //
      // This is the parity pass/lec/phase_sched.cpp already uses: a guard
      // sampled on the inactive phase immediately before the CELL's own active
      // edge, "parity of the CELL, not of the consumer" (guard_before_fall /
      // guard_sample), and keyed so one gate contributes ONE guard
      // (guard_key / guard_cones) rather than one per consumer. `icg_held`
      // counts the consumers served; `held_guards` is the sharing.
      //
      // WHY ONLY THE GATE GUARD. The consumer's own enable is evaluated at ITS
      // commit, not at the rise. Sampling the conjunction instead would freeze
      // the local enable half a period early: with the gate on and the local
      // enable 0 at the rise and 1 at the fall it would suppress a commit the
      // design makes, and 1-at-rise / 0-at-fall would invent one. B01's
      // `wr_data_en_i` is exactly such a local enable.
      //
      // EPOCH. The divider initializes to 0 and slot 0 is therefore the FIRST
      // normalized step, so the sampling commit always precedes the first
      // slot-1 read and the held flop's power-on value is never observed. It is
      // still given a concrete 0 so the encoder has a definite initial state
      // rather than a free one.
      if (e.icg_hold && !gate_acc.is_invalid() && plan.slots > 1 && !slot_pred[0].is_invalid()) {
        auto       it  = held_guards.find(guard_key);
        if (it == held_guards.end()) {
          auto hold = gu::create_typed_node(*g, Ntype_op::Flop);
          gate_acc.connect_sink(gu::setup_sink_by_name(hold, "din"));
          slot_pred[0].connect_sink(gu::setup_sink_by_name(hold, "enable"));
          if (!plan.ref_clk_pin.is_invalid()) {
            plan.ref_clk_pin.connect_sink(gu::setup_sink_by_name(hold, "clock_pin"));
          }
          gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(hold, "initial"));
          auto hq = hold.create_driver_pin(0);
          gu::set_bits(hq, 1);
          gu::set_unsign(hq);
          it = held_guards.emplace(guard_key, hq).first;
        }
        gate_acc = it->second;
        ++r.icg_held;
      }

      // The LOCAL enable joins here, unsampled, so it is read at this
      // element's own commit. The slot predicate is ANDed in further below.
      hhds::Pin_class acc = gate_acc;
      if (auto old_en = sink_driver(e.node, "enable"); !old_en.is_invalid()) {
        acc = and1(acc, old_en);
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
        if (!cur.is_invalid()) {
          orphaned_clocks.push_back(cur);
        }
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
        auto       asyncp   = sink_driver(e.node, "async");
        // A LATCH's reset is inherently ASYNCHRONOUS -- there is no clock edge
        // for it to synchronize to, which is M7's landed ruling and why cgen
        // emits it as the FIRST branch ahead of the transparency test. So a
        // retyped latch's reset must stay OUTSIDE the slot gate even when it
        // carries no `async` attribute. Folding it in would gate the reset on
        // the slot predicate, and an async reset pulse that falls between two
        // normalized clock edges would be silently lost.
        const bool is_async = e.is_latch || (asyncp.is_const() && !asyncp.is_known_false());
        if (!is_async) {
          auto negp = sink_driver(e.node, "negreset");
          auto test = rstp;
          if (negp.is_const() && !negp.is_known_false()) {
            auto inv = gu::create_typed_node(*g, Ntype_op::Not);
            rstp.connect_sink(livehd::graph_util::setup_sink_pid(inv, 0));
            test = inv.create_driver_pin(0);
            gu::set_bits(test, 1);
            gu::set_unsign(test);
          }
          auto      din  = sink_driver(e.node, "din");
          auto      init = sink_driver(e.node, "initial");
          const int qw   = std::max(1, static_cast<int>(gu::bits_of(e.node.get_driver_pin(0))));
          if (init.is_invalid()) {
            init = gu::create_const(*g, *Dlop::create_integer(0));
          }
          if (!din.is_invalid()) {
            // Mux pin ids: 0 = selector, 1 = arm taken when the selector is 0,
            // 2 = arm taken when it is 1. Reset value on the 1 arm.
            auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
            test.connect_sink(livehd::graph_util::setup_sink_pid(mux, 0));
            din.connect_sink(livehd::graph_util::setup_sink_pid(mux, 1));
            init.connect_sink(livehd::graph_util::setup_sink_pid(mux, 2));
            auto mq = mux.create_driver_pin(0);
            gu::set_bits(mq, qw);
            gu::set_unsign(mq);
            drop_sink(e.node, "din");
            mq.connect_sink(gu::setup_sink_by_name(e.node, "din"));
          }
          if (auto en = sink_driver(e.node, "enable"); !en.is_invalid()) {
            auto orn = gu::create_typed_node(*g, Ntype_op::Or);
            en.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
            test.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
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
        old.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        pred.connect_sink(livehd::graph_util::setup_sink_pid(andn, 0));
        auto aq = andn.create_driver_pin(0);
        gu::set_bits(aq, 1);
        gu::set_unsign(aq);
        drop_sink(e.node, "enable");
        aq.connect_sink(gu::setup_sink_by_name(e.node, "enable"));
      }
      ++r.flops_slotted;
    }
  }

  // ---- the derived clock cones the rebind left behind -------------------
  // A cone that only ever fed a clock_pin is dead once every element commits
  // on the reference clock, but it is not GONE: the encoder skips a node with
  // no fanout, not a node whose fanout is itself dead, so the head of such a
  // cone is still encoded -- as DATA. For a negedge register behind an ICG
  // (`INV(Clock_cell)` in a mapped netlist) that head reads the timing-only
  // Clock_cell output and the whole def came back UNKNOWN. Remove what became
  // unreachable, stopping at state, instances, IO and anything still read.
  {
    std::vector<hhds::Pin_class>           work = std::move(orphaned_clocks);
    absl::flat_hash_set<hhds::Class_index> gone;  // a DAG reaches a node twice; never touch a deleted one
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p)) {
        continue;
      }
      auto n = p.get_master_node();
      if (gone.contains(n.get_class_index())) {
        continue;
      }
      const auto op = gu::type_op_of(n);
      if (n.is_invalid() || n.has_out_edges() || gu::is_type_register(n) || op == Ntype_op::Sub || op == Ntype_op::Memory) {
        continue;
      }
      for (auto sink : n.inp_sorted_pins()) {
        for (const auto& drv : sink.get_driver_pins()) {
          work.push_back(drv);
        }
      }
      gone.insert(n.get_class_index());
      n.del_node();
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
    for (auto n : g->body().nodes()) {
      if (gu::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      auto sio = n.get_subnode_io();
      if (sio == nullptr || !gu::is_marker_module_name(sio->get_name())) {
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
      for (auto sink : n.inp_sorted_pins()) {
        if (sink.get_port_id() == cond_pid) {
          cond = sink.get_driver_pin();
          break;
        }
      }
      if (cond.is_invalid()) {
        continue;
      }
      // boundary implies cond  ==  !boundary | cond
      auto notb = gu::create_typed_node(*g, Ntype_op::Not);
      boundary.connect_sink(livehd::graph_util::setup_sink_pid(notb, 0));
      auto nbq = notb.create_driver_pin(0);
      gu::set_bits(nbq, 1);
      gu::set_unsign(nbq);
      auto orn = gu::create_typed_node(*g, Ntype_op::Or);
      nbq.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
      cond.connect_sink(livehd::graph_util::setup_sink_pid(orn, 0));
      auto oq = orn.create_driver_pin(0);
      gu::set_bits(oq, 1);
      gu::set_unsign(oq);
      // Snapshot first: the pin walk is a view over live storage.
      absl::InlinedVector<hhds::Pin_class, 4> doomed;
      for (auto sink : n.inp_sorted_pins()) {
        if (sink.get_port_id() == cond_pid) {
          doomed.push_back(sink);
        }
      }
      for (const auto& sink : doomed) {
        sink.del_sink();
      }
      oq.connect_sink(n.create_sink_pin("cond"));
    }
  }

  r.applied   = true;
  r.slots     = plan.slots;
  r.ref_clock = plan.ref_clock_name;
  r.reason    = std::format(
      "P={} slots, {} latch(es) retyped, {} gated clock(s) folded into an enable ({} sampled-held), {} element(s) "
      "slotted, {} memory(ies) slotted over {} write port(s) ({})",
      r.slots,
      r.latches_retyped,
      r.icg_folded,
      r.icg_held,
      r.flops_slotted,
      r.mems_slotted,
      r.mem_ports_slotted,
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
      if (sp && !livehd::copy_with_callees(outlib, *srclib, sp->get_name())) {
        livehd::diag::err("pass.single_edge", "copy-failed", "internal")
            .msg("pass.single_edge: could not copy module '{}' into {}", sp->get_name(), out)
            .fatal();
        return;
      }
    }
  }
}
