// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>

#include "absl/container/flat_hash_map.h"
#include "node_util.hpp"

namespace livehd::cprop_value {

// A local proof from integer constants and explicit cell operands. Pin width
// and sign annotations, including those on IO and state, are never evidence.
// -1 means unknown; zero means the value is identically zero. Depth bounds keep
// cyclic and shared expressions from turning this peephole into range analysis.
inline int unsigned_width_impl(const hhds::Pin_class& pin, int depth, absl::flat_hash_map<hhds::Class_index, int>& memo) {
  namespace gu = graph_util;
  if (pin.is_invalid() || depth > 32 || memo.size() > 256) {
    return -1;
  }
  if (pin.is_const()) {
    const auto& value = gu::const_of(pin);
    if (value.is_negative() || !value.is_numeric()) {
      return -1;
    }
    if (value.has_unknowns()) {
      // A mixed constant with a known zero sign still has a finite unsigned
      // envelope. Unknown-sign constants must remain unbounded.
      const int width = value.get_bits() - 1;
      return width >= 0 && value.sra_op(Dlop::create_integer(width))->is_known_zero() ? width : -1;
    }
    return value.get_last_bit_set() + 1;
  }
  if (gu::is_graph_input_pin(pin) || gu::is_graph_output_pin(pin)) {
    return -1;
  }
  auto node = pin.get_master_node();
  auto op   = gu::type_op_of(node);
  if (op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::Ror) {
    return 1;
  }
  if (op == Ntype_op::Concat) {
    const auto width = gu::concat_total_width(node);
    return width > 0 ? width : -1;
  }
  if (op == Ntype_op::Get_mask) {
    auto mask = gu::get_driver_of_sink_name(node, "mask");
    if (mask.is_const()) {
      const auto& value = gu::const_of(mask);
      if (!value.is_negative() && !value.has_unknowns()) {
        const auto count = value.popcount();
        return count;
      }
    }
    return -1;
  }
  if (auto found = memo.find(pin.get_class_index()); found != memo.end()) {
    return found->second;
  }
  memo.emplace(pin.get_class_index(), -1);  // A cycle cannot prove a bound.
  if (op == Ntype_op::Set_mask) {
    const auto base  = gu::get_driver_of_sink_name(node, "a");
    const auto mask  = gu::get_driver_of_sink_name(node, "mask");
    const int  width = unsigned_width_impl(base, depth + 1, memo);
    if (width < 0 || !mask.is_const()) {
      return -1;
    }
    const auto& value = gu::const_of(mask);
    if (value.is_negative() || value.has_unknowns()) {
      return -1;
    }
    return memo[pin.get_class_index()] = std::max(width, value.get_last_bit_set() + 1);
  }
  // Only these operators preserve a finite nonnegative envelope. For And,
  // one bounded operand is enough even when the other is negative/unbounded.
  if (op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
    return -1;
  }
  const auto control_end = op == Ntype_op::Hotmux ? gu::hotmux_control_end(node) : 0;
  int        width       = -1;
  for (const auto& edge : node.inp_edges()) {
    if ((op == Ntype_op::Mux && edge.sink.get_port_id() == 0)
        || (op == Ntype_op::Hotmux && gu::is_hotmux_control(edge.sink.get_port_id(), control_end))) {
      continue;
    }
    const int arm = unsigned_width_impl(edge.driver, depth + 1, memo);
    if (op == Ntype_op::And) {
      if (arm >= 0) {
        width = width < 0 ? arm : std::min(width, arm);
      }
    } else {
      if (arm < 0) {
        return -1;
      }
      width = std::max(width, arm);
    }
  }
  return memo[pin.get_class_index()] = width;
}

inline int unsigned_width(const hhds::Pin_class& pin) {
  absl::flat_hash_map<hhds::Class_index, int> memo;
  return unsigned_width_impl(pin, 0, memo);
}

inline bool is_bool01(const hhds::Pin_class& pin) {
  const int width = unsigned_width(pin);
  return width >= 0 && width <= 1;
}

}  // namespace livehd::cprop_value
