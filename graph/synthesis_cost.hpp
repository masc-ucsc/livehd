// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Mapper-facing cost estimate shared by synthesis coloring and pass.abc QoR.
// The partition window, its histogram, and ABC's per-region input_ge field must
// use the same unit for measured size/time calibration to be meaningful.

#include <algorithm>
#include <cstdint>
#include <limits>

#include "node_util.hpp"

namespace livehd::graph_util {

namespace shift_detail {

// The replay itself, given what the callers have ALREADY resolved. Both entry
// points below have to know `op`, the `b` driver and `mappable_ge_weight` just
// to decide whether a barrel exists at all, so re-deriving all three inside the
// replay made synthesis_ge_weight walk the node's edges twice per runtime shift
// -- in the pass.color / pass.abc per-node loops.
[[nodiscard]] inline uint64_t mux_count(const hhds::Node_class& node, Ntype_op op, const hhds::Pin_class& amount, uint64_t full) {
  // A RIGHT shift's barrel is built at abc's shift width `cw = max(operand,
  // result)` and then truncated (abc_map), NOT at the result width: a 1024-bit
  // operand read as 20 bits still costs a 1024-bit-deep network per stage. This
  // is the SRA path only -- build_shl takes out_w = the result width.
  const auto     a_pin   = get_driver_of_sink_name(node, "a");
  const uint64_t shift_w = std::max<uint64_t>(full, a_pin.is_invalid() ? 0 : static_cast<uint64_t>(std::max(0, real_width(a_pin))));

  uint64_t demand = full;
  if (op == Ntype_op::SRA) {
    int  selected_hi = 0;
    bool saw_use     = false;
    for (const auto& e : node.out_edges()) {
      const auto consumer = e.sink.get_master_node();
      if (type_op_of(consumer) != Ntype_op::Get_mask) {
        saw_use = false;
        break;
      }
      const auto mask_pin = get_driver_of_sink_name(consumer, "mask");
      if (!mask_pin.is_const()) {
        saw_use = false;
        break;
      }
      const auto& mask = const_of(mask_pin);
      if (mask.is_negative()) {
        saw_use = false;
        break;
      }
      const int wanted = std::max(0, real_width(consumer.create_driver_pin(0)));
      int       found  = 0;
      int       hi     = 0;
      for (int bit = 0; bit < static_cast<int>(mask.get_bits()) && found < wanted; ++bit) {
        if (mask.bit_test(bit)) {
          hi = bit + 1;
          ++found;
        }
      }
      selected_hi = std::max(selected_hi, hi);
      saw_use     = true;
    }
    if (saw_use && selected_hi > 0) {
      demand = std::min(shift_w, static_cast<uint64_t>(selected_hi));
    }
  }

  // abc_arith builds one 2:1 mux level per amount bit.
  //
  // A sliced demand does NOT scale every stage. build_shr_prefix (abc_arith.hpp)
  // trims BACKWARDS: the last stage produces `demand` bits, but stage k still
  // needs `need[k+1] + 2^k` (clamped to the shift width) from the one before it,
  // so only the last few levels narrow at all. Charging `demand` at every stage
  // undercounted a 1024-bit bus sliced to 20 bits by ~40x -- a color the window
  // certified at its GE ceiling then handed ABC an order of magnitude more
  // gates, which is exactly the shift-heavy blow-up the window exists to split.
  // Replay the same recurrence here instead of approximating it; with no slice
  // (demand == full) it collapses to the old stages*full product, so nothing
  // else recalibrates.
  const uint64_t stages = static_cast<uint64_t>(std::max(1, real_width(amount)));
  uint64_t       muxes  = 0;
  if (op == Ntype_op::SRA) {
    uint64_t need = demand;  // need[nb] == the demanded low prefix
    for (uint64_t k = stages; k-- > 0;) {
      muxes             += need;  // stage k emits need[k+1] muxes
      // sh = 2^k capped to the shift width; a shift at/past the top adds nothing.
      const uint64_t sh  = k >= 63 || (uint64_t{1} << k) >= shift_w ? shift_w : uint64_t{1} << k;
      need               = sh >= shift_w ? need : std::min(shift_w, need + sh);
    }
  } else {
    // build_shl (abc_arith.hpp) makes its data vector out_w wide and ZERO-EXTENDS
    // `a` into it, emitting exactly out_w muxes per stage -- a left shift costs
    // the RESULT width, not the operand width, and is never demand-sliced. Only
    // the right shift is built at cw = max(operand, result).
    muxes = full * stages;
  }
  return muxes;
}

}  // namespace shift_detail

// Structural 2:1 MUX count that pass/abc/abc_arith.hpp emits for `node`'s
// barrel network, or 0 when `node` is not a RUNTIME shift (not a SHL/SRA, or a
// constant/absent amount -- the mapper wires those directly).
//
// Factored out of synthesis_ge_weight so the cones predictor
// (graph/predict_abc_size.hpp) charges the SAME structural replay at its own
// per-mux rate. The x6 in synthesis_ge_weight stays there: it is an ABC TIME
// safety factor, not part of the structure.
[[nodiscard]] inline uint64_t shift_mux_count(const hhds::Node_class& node) {
  const auto op = type_op_of(node);
  if (op != Ntype_op::SRA && op != Ntype_op::SHL) {
    return 0;
  }
  const auto amount = get_driver_of_sink_name(node, "b");
  if (amount.is_invalid() || amount.is_const()) {
    return 0;
  }
  return shift_detail::mux_count(node, op, amount, mappable_ge_weight(node));
}

// mappable_ge_weight already accounts for bit width and important type effects
// (for example Mult is width squared and Sub instances are black boxes).
// Runtime shifts need one more dimension: ABC expands one mux level per shift-
// amount bit. For SRA, a narrow constant Get_mask can reduce result demand
// because pass.abc builds only that prefix when both nodes share a region.
[[nodiscard]] inline uint64_t synthesis_ge_weight(const hhds::Node_class& node) {
  const uint64_t full = mappable_ge_weight(node);
  const auto     op   = type_op_of(node);
  if (op != Ntype_op::SRA && op != Ntype_op::SHL) {
    return full;
  }

  const auto amount = get_driver_of_sink_name(node, "b");
  if (amount.is_invalid()) {
    return full;
  }
  if (amount.is_const()) {
    return 1;  // mapper wires a constant shift directly; no barrel network
  }

  // A Boolean mux is two ANDs plus one OR. ROB calibration showed that counting
  // those three gates still grouped shift-heavy colors whose ABC optimization
  // time was more than twice the 15-minute ceiling. A 2x shift-complexity safety
  // factor captures that super-linear optimization cost while leaving non-shift
  // logic at the higher shared QoR ceiling. This factor is a TIME correction, so
  // it lives here and not in shift_mux_count's structural replay.
  const uint64_t     muxes           = shift_detail::mux_count(node, op, amount, full);
  constexpr uint64_t mux_gate_factor = 6;
  if (muxes > std::numeric_limits<uint64_t>::max() / mux_gate_factor) {
    return std::numeric_limits<uint64_t>::max();
  }
  return std::max<uint64_t>(1, muxes * mux_gate_factor);
}

}  // namespace livehd::graph_util
