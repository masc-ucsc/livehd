// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <optional>

#include "node_util.hpp"

namespace livehd::muxctx {
struct Bool_condition {
  hhds::Pin_class base;
  bool            true_when_base = true;
};

inline std::optional<bool> const_truth(const hhds::Pin_class& pin) {
  if (pin.is_invalid() || !pin.is_const() || graph_util::const_of(pin).has_unknowns()) {
    return {};
  }
  return !graph_util::const_of(pin).is_known_zero();
}

// Decode one producer using its already-computed input facts. Callers own
// caching, forwarding/generation checks and their structural bool01 proof.
// The bounded scan never recursively walks a shared control cone.
template <typename Lookup, typename Boolean>
std::optional<Bool_condition> compute_condition(const hhds::Pin_class& pin, const Lookup& lookup, const Boolean& boolean) {
  namespace gu = graph_util;
  if (pin.is_invalid()) {
    return {};
  }
  if (pin.is_const() || gu::is_graph_input_pin(pin)) {
    return Bool_condition{pin, true};
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  if (op != Ntype_op::Mux && op != Ntype_op::EQ && op != Ntype_op::Xor) {
    return Bool_condition{pin, true};
  }
  const size_t                   count = op == Ntype_op::Mux ? 3 : 2;
  std::array<hhds::Pin_class, 3> inputs;
  size_t                         used = 0;
  for (auto sink : node.inp_sorted_pins()) {
    if (used == count || sink.get_port_id() != used) {
      return Bool_condition{pin, true};
    }
    inputs[used++] = sink.get_driver_pin();
  }
  if (used != count) {
    return Bool_condition{pin, true};
  }
  hhds::Pin_class base;
  bool            invert = true;
  if (op == Ntype_op::Mux) {
    const auto f = const_truth(inputs[1]), t = const_truth(inputs[2]);
    if (!f || !t || *f == *t) {
      return Bool_condition{pin, true};
    }
    base   = inputs[0];
    invert = !*t;
  } else {
    for (size_t i = 0; i < 2; ++i) {
      auto constant = inputs[i];
      if (constant.is_invalid() || !constant.is_const()) {
        continue;
      }
      const auto& value = gu::const_of(constant);
      const auto  other = inputs[1 - i];
      if (value.has_unknowns() || other.is_invalid()) {
        continue;
      }
      if ((op == Ntype_op::EQ && value.is_known_zero())
          || (op == Ntype_op::Xor && value.is_just_i64() && value.to_just_i64() == 1 && boolean(other))) {
        base = other;
        break;
      }
    }
  }
  if (base.is_invalid()) {
    return Bool_condition{pin, true};
  }
  auto condition = lookup(base);
  if (condition && invert) {
    condition->true_when_base = !condition->true_when_base;
  }
  return condition;
}
}  // namespace livehd::muxctx
