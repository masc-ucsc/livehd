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
  if (is_const_pin(amount)) {
    return 1;  // mapper wires a constant shift directly; no barrel network
  }

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
      if (!is_const_pin(mask_pin)) {
        saw_use = false;
        break;
      }
      const auto mask = hydrate_const(mask_pin);
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
      demand = std::min(full, static_cast<uint64_t>(selected_hi));
    }
  }

  // abc_arith builds one 2:1 mux level per amount bit; a Boolean mux is two
  // ANDs plus one OR. ROB calibration showed that counting those three gates
  // still grouped shift-heavy colors whose ABC optimization time was more than
  // twice the 15-minute ceiling. A 2x shift-complexity safety factor captures
  // that super-linear optimization cost while leaving non-shift logic at the
  // higher shared QoR ceiling.
  const uint64_t     stages          = static_cast<uint64_t>(std::max(1, real_width(amount)));
  constexpr uint64_t mux_gate_factor = 6;
  if (demand > std::numeric_limits<uint64_t>::max() / stages / mux_gate_factor) {
    return std::numeric_limits<uint64_t>::max();
  }
  return std::max<uint64_t>(1, demand * stages * mux_gate_factor);
}

}  // namespace livehd::graph_util
