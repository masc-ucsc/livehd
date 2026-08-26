// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Predicted GENERIC-AIG size of one LGraph node -- the unit `pass.color synth
// --set synth_alg=cones` budgets its cone walk in, ranks cone overlaps in, and
// merges colors under (todo/livehd/2c-color-synthcones.html section B).
//
// WHY a second unit next to synthesis_ge_weight. Measured over 1930 regions
// from 7 lhdsuite designs, synthesis GE predicts the MAPPED gate count with a
// median error of 1.8x and a p90 of 5.6x, in BOTH directions: a runtime shift
// over-counts by up to 24x (the x6 in synthesis_cost.hpp is an ABC *time*
// safety factor and deliberately not a size estimate), a Sum under-counts ~5x,
// a Mult 1.5-6x and a register-file array 8-11x. ABC's runtime correlates with
// mapped gates (r~0.9), not with GE (r~0.31 on cva6).
//
// WHAT this is. A stable heuristic read off the STRUCTURE of pass.abc's default
// bit blast (the arms in pass/abc/abc_map.cpp and the builders in
// pass/abc/abc_arith.hpp), scaled by a per-op sky130 fixture: and/or/xor/mux2
// ~1 gate/bit, eq 1.35, lt 2.6, add 5.5, mux4 3.1, mul ~5.9*a*b, shifts ~1.15
// per (bit x amount-bit), flops 0 plus ~1/bit per folded enable/reset mux.
// It is NOT an exact count and does not try to be: ABC folds constants while
// building, the adder architecture is configurable, and the quantity a caller
// ultimately wants (mapped size) is downstream of both. Assigning a score to an
// operation is a SIZING statement, never a claim that pass.abc can blast it.
//
// SCOPE. cones mode's budget / overlap / merge test only. synthesis_ge_weight
// stays the unit of the size window, the `--stats` GE columns, absorb, reduce,
// pass.abc's size tiers and Region_qor::input_ge. Replacing it design-wide is a
// separate task with its own pinned tests.

#include <algorithm>
#include <cstdint>
#include <limits>

#include "node_util.hpp"
#include "synthesis_cost.hpp"

namespace livehd::graph_util {

namespace predict_detail {

// Every product and sum below saturates: a 1024-bit Mult inside a 64-bit
// accumulator would otherwise wrap to a SMALL score and let the merge fold a
// monster region.
[[nodiscard]] inline uint64_t sat_mul(uint64_t a, uint64_t b) {
  if (a == 0 || b == 0) {
    return 0;
  }
  if (a > std::numeric_limits<uint64_t>::max() / b) {
    return std::numeric_limits<uint64_t>::max();
  }
  return a * b;
}

[[nodiscard]] inline uint64_t sat_add(uint64_t a, uint64_t b) {
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return std::numeric_limits<uint64_t>::max();
  }
  return a + b;
}

// An unknown required width degrades to 1 -- it must never make a LIVE
// operation free, which is how a mis-ordered pass (color before bitwidth) would
// otherwise hand ABC an unbudgeted region.
[[nodiscard]] inline uint64_t atleast1(uint64_t w) { return w == 0 ? 1 : w; }

// One inp_edges() pass, everything the table below needs from it. inp_edges()
// is eager and scans the node's whole edge slot list, so a caller that asked
// three separate questions would scan three times.
struct Fanin_shape {
  uint64_t terms      = 0;  // drivers on the value sinks (multi-driver folds)
  uint64_t arms       = 0;  // Mux/Hotmux: drivers on pid >= 1 (pid 0 is `s`)
  uint64_t widest     = 0;  // widest driver width across all sinks
  bool     has_enable = false;
  bool     has_reset  = false;
};

// The `enable` / `reset_pin` sink pids of the register cells. Spelled out
// rather than asked of Ntype::get_sink_pid: that lookup's fast path derives the
// pid from the leading char for 'a'..'f' and then ASSERTS the name round-trips,
// so `get_sink_pid(Fflop, "enable")` -- a pin an Fflop does not have -- trips a
// debug assert instead of returning invalid. Flop and Latch share pids by
// construction (cell.cpp); an Fflop has no enable and keeps reset_pin at 7.
struct Ctrl_pids {
  hhds::Port_id enable = livehd::Port_invalid;
  hhds::Port_id reset  = livehd::Port_invalid;
};

[[nodiscard]] inline Ctrl_pids ctrl_pids(Ntype_op op) {
  switch (op) {
    case Ntype_op::Flop:
    case Ntype_op::Latch : return {4, 7};
    case Ntype_op::Fflop : return {livehd::Port_invalid, 7};
    default              : return {};
  }
}

[[nodiscard]] inline Fanin_shape fanin_shape(const hhds::Node_class& node, Ntype_op op) {
  Fanin_shape sh;
  const auto  ctrl   = ctrl_pids(op);
  const auto  en_pid = ctrl.enable;
  const auto  rp_pid = ctrl.reset;
  for (const auto& e : node.inp_edges()) {
    const auto pid = e.sink.get_port_id();
    ++sh.terms;
    if (pid != 0) {
      ++sh.arms;
    }
    if (const auto b = bits_of(e.driver); b > 0 && static_cast<uint64_t>(b) > sh.widest) {
      sh.widest = static_cast<uint64_t>(b);
    }
    if (en_pid != livehd::Port_invalid && pid == en_pid) {
      sh.has_enable = true;
    }
    if (rp_pid != livehd::Port_invalid && pid == rp_pid) {
      sh.has_reset = true;
    }
  }
  return sh;
}

// 9 * a_bits * b_bits for the two-operand case (partial-product ANDs plus one
// adder row per operand bit), generalized to the folded multi-driver `as` sink
// by accumulating the running product width. Operand widths, NOT out_width^2 --
// that is the ge_weight approximation this table exists to replace.
[[nodiscard]] inline uint64_t mult_score(const hhds::Node_class& node) {
  uint64_t acc   = 0;  // width of the running product
  uint64_t total = 0;
  for (const auto& e : node.inp_edges()) {
    const uint64_t w = atleast1(static_cast<uint64_t>(std::max(0, bits_of(e.driver))));
    if (acc == 0) {
      acc = w;
      continue;
    }
    total = sat_add(total, sat_mul(9, sat_mul(acc, w)));
    acc   = sat_add(acc, w);
  }
  return total;
}

// `a % 2^k` is rewritten to an And before ABC ever sees it, so charge it as one.
// Any other remainder is a blackbox here (see the Div arm).
[[nodiscard]] inline bool rem_is_power_of_two_mask(const hhds::Node_class& node) {
  const auto b = get_driver_of_sink_name(node, "b");
  if (b.is_invalid() || !is_const_pin(b)) {
    return false;
  }
  const auto v = hydrate_const(b);
  if (v.is_negative()) {
    return false;
  }
  // Exactly one set bit == a power of two.
  int set = 0;
  for (int bit = 0; bit < static_cast<int>(v.get_bits()); ++bit) {
    if (v.bit_test(bit) && ++set > 1) {
      return false;
    }
  }
  return set == 1;
}

}  // namespace predict_detail

// Predicted 2-input AIG-size score of `node`. 0 means "mints no gate here":
// pure wiring, a constant, or a blackbox whose logic is weighed elsewhere.
//
// The per-op constants are ONE table and are pinned by
// graph/predict_abc_size_smoke.cpp -- change them there together or the cones
// budget, its overlap ranking and its merge cap stop agreeing with each other.
[[nodiscard]] inline uint64_t predict_abc_size(const hhds::Node_class& node) {
  using namespace predict_detail;

  if (node.is_invalid() || is_builtin_node(node)) {
    return 0;
  }
  const auto op = type_op_of(node);

  switch (op) {
    // ---- pure wiring / constants: an AIG spends nothing on these -------------
    case Ntype_op::Invalid:
    case Ntype_op::IO:
    case Ntype_op::Nconst:
    case Ntype_op::Concat:
    case Ntype_op::Sext:
    // A Not is a complement EDGE in an AIG, never a node.
    case Ntype_op::Not: return 0;

    // ---- blackboxes: not blasted here ---------------------------------------
    // Memory (default memory=false stays a hard macro), Div, a stateful or
    // combinational Sub, a Clock_cell, a LUT and an AttrSet all leave this def's
    // region without contributing gates to it. memory=true lowers memories AFTER
    // coloring and stamps the result with the memory's color -- that color is
    // then under-predicted, the same limitation synthesis_ge_weight has.
    case Ntype_op::Memory:
    case Ntype_op::Div:
    case Ntype_op::Sub:
    case Ntype_op::Clock_cell:
    case Ntype_op::LUT:
    case Ntype_op::AttrSet: return 0;

    case Ntype_op::Rem:
      return rem_is_power_of_two_mask(node) ? atleast1(ge_detail::out_width(node)) : 0;

    // ---- masks: constant mask is a bit rename, a runtime mask is real logic --
    case Ntype_op::Get_mask:
    case Ntype_op::Set_mask: {
      const auto mask = get_driver_of_sink_name(node, "mask");
      if (mask.is_invalid() || is_const_pin(mask)) {
        return 0;
      }
      return atleast1(ge_detail::out_width(node));  // ~1 gate/bit of masking
    }

    // ---- shifts: a constant amount is a rename, a runtime one is a barrel ----
    case Ntype_op::SHL:
    case Ntype_op::SRA: return sat_mul(3, shift_mux_count(node));

    // ---- bitwise ------------------------------------------------------------
    case Ntype_op::And:
    case Ntype_op::Or: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(atleast1(ge_detail::out_width(node)), sh.terms == 0 ? 0 : sh.terms - 1);
    }
    case Ntype_op::Xor: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(3, sat_mul(atleast1(ge_detail::out_width(node)), sh.terms == 0 ? 0 : sh.terms - 1));
    }
    case Ntype_op::Ror: {
      const auto sh = fanin_shape(node, op);
      return atleast1(sh.widest);  // one OR level per operand bit
    }

    // ---- muxes --------------------------------------------------------------
    // A 2:1 mux is 3 AIG nodes; an N-arm Mux is an (N-1)-deep chain of them.
    case Ntype_op::Mux: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(3, sat_mul(atleast1(ge_detail::out_width(node)), sh.arms == 0 ? 0 : sh.arms - 1));
    }
    // One-hot select: an AND per arm plus the OR tree collapsing them, ~2/bit
    // per arm and no chain discount.
    case Ntype_op::Hotmux: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(2, sat_mul(atleast1(ge_detail::out_width(node)), sh.arms));
    }

    // ---- arithmetic / compare ----------------------------------------------
    // Ripple-carry full adder: 2 XORs plus the carry, sharing the half-XOR.
    case Ntype_op::Sum: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(8, sat_mul(atleast1(ge_detail::out_width(node)), sh.terms == 0 ? 0 : sh.terms - 1));
    }
    // Subtractor chain, one bit of result.
    case Ntype_op::LT:
    case Ntype_op::GT: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(5, atleast1(sh.widest));
    }
    // XNOR per bit plus the AND chain that reduces them.
    case Ntype_op::EQ: {
      const auto sh = fanin_shape(node, op);
      return sat_mul(4, sat_mul(atleast1(sh.widest), sh.terms == 0 ? 0 : sh.terms - 1));
    }
    case Ntype_op::Mult: return mult_score(node);

    // ---- state --------------------------------------------------------------
    // A level-sensitive Latch is an UNCONDITIONAL native boundary in pass.abc
    // (abc_map.cpp `latch_boundary`, 2f-latch M2): q enters the AIG as a fresh
    // PI and din/enable leave as POs, so the node itself mints no gate and
    // nothing is folded into it. Its control logic is charged to the driver
    // nodes that compute it, like any other cut.
    case Ntype_op::Latch: return 0;

    // A Flop, by contrast, becomes an ABC latch in sequential mode and pass.abc
    // folds `reset ? rval : (enable ? din : Q)` into its D (abc_map.cpp,
    // "wire each latch's data-in (D) to the folded next-state"). The storage
    // element is still 0 AIG; the two folded muxes are ~3 AIG nodes per bit each.
    case Ntype_op::Flop:
    case Ntype_op::Fflop: {
      const auto     sh   = fanin_shape(node, op);
      const uint64_t bits = atleast1(ge_detail::out_width(node));
      uint64_t       s    = 0;
      if (sh.has_enable) {
        s = sat_add(s, sat_mul(3, bits));
      }
      if (sh.has_reset) {
        s = sat_add(s, sat_mul(3, bits));
      }
      return s;
    }

    default: return atleast1(ge_detail::out_width(node));
  }
}

}  // namespace livehd::graph_util
