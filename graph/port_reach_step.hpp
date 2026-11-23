// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <limits>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "node_util.hpp"

namespace livehd::port_reach {

// Index connected instance sinks once. Compact-loop carry inputs can have both
// a seed and a recurrence edge; both are dependencies of the input port.
class Instance_inputs {
public:
  explicit Instance_inputs(const hhds::Node_class& instance) {
    for (const auto& sink : instance.inp_sorted_pins()) {
      sinks_.emplace(sink.get_port_id(), sink);
    }
  }

  template <class Visit>
  void drivers(uint32_t port, const Visit& visit) const {
    if (auto it = sinks_.find(port); it != sinks_.end()) {
      for (const auto& driver : it->second.get_driver_pins()) {
        visit(driver);
      }
    }
  }

private:
  absl::flat_hash_map<uint32_t, hhds::Pin_class> sinks_;
};

// Transfer [lo, hi) through wiring that preserves a known bit range. The
// callers supply the clipping rule and the conservative fallback for other
// operators, memories and instance boundaries.
template <class Push, class Whole>
bool step_bit_range(const hhds::Node_class& node, int lo, int hi, const Push& push, const Whole& whole) {
  namespace gu      = graph_util;
  const auto op     = gu::type_op_of(node);
  const auto driver = [&](std::string_view name) { return gu::get_driver_of_sink_name(node, name); };
  switch (op) {
    case Ntype_op::Get_mask: {
      const auto window = gu::bit_range(node);
      if (!window) {
        return false;
      }
      const auto [first, end] = *window;
      if (lo < end - first) {
        push(driver("a"), first + lo, first + std::min(hi, end - first));
      }
      return true;
    }
    case Ntype_op::Set_mask: {
      const auto window = gu::bit_range(node);
      if (!window) {
        return false;
      }
      const auto [first, end] = *window;
      const auto low = std::max(lo, first), high = std::min(hi, end);
      if (low < high) {
        push(driver("value"), low - first, high - first);
      }
      if (lo < first || hi > end) {
        const auto base = driver("a");
        if (lo < first) {
          push(base, lo, std::min(hi, first));
        }
        if (hi > end) {
          push(base, std::max(lo, end), hi);
        }
      }
      return true;
    }
    case Ntype_op::Concat: {
      const auto lanes = gu::concat_lanes(node);
      for (const auto& lane : lanes) {
        const auto low = std::max(lo, lane.offset), high = std::min(hi, lane.offset + lane.width);
        if (low < high) {
          push(lane.value, low - lane.offset, high - lane.offset);
        }
      }
      return !lanes.empty();
    }
    case Ntype_op::SHL:
    case Ntype_op::SRA: {
      const auto amount = driver("b");
      if (!amount.is_const()) {
        return false;
      }
      const auto& value = gu::const_of(amount);
      if (!value.is_just_i64() || value.has_unknowns()) {
        return false;
      }
      const auto shift = value.to_just_i64();
      if (shift < 0 || shift > (1 << 24) || (op == Ntype_op::SRA && shift > std::numeric_limits<int>::max() - hi)) {
        return false;
      }
      const auto offset = static_cast<int>(shift);
      if (op == Ntype_op::SRA) {
        push(driver("a"), lo + offset, hi + offset);
      } else if (hi > offset) {
        push(driver("a"), std::max(lo - offset, 0), hi - offset);
      }
      return true;
    }
    case Ntype_op::And:
    case Ntype_op::Or :
    case Ntype_op::Xor:
    case Ntype_op::Not:
    case Ntype_op::Mux:
      for (const auto& sink : node.inp_sorted_pins()) {
        for (const auto& input : sink.get_driver_pins()) {
          if (op == Ntype_op::Mux && sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Mux, "s")) {
            whole(input);
          } else {
            push(input, lo, hi);
          }
        }
      }
      return true;
    default: return false;
  }
}

}  // namespace livehd::port_reach
