// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <limits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "cprop_profile.hpp"
#include "cprop_wiring.hpp"
#include "node_util.hpp"

namespace livehd::cprop_value {
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
using Window = std::pair<int, int>;
inline constexpr Window unknown_window{-1, -1};

// Queries never traverse a producer cone. Ordinary cprop sees literal and
// explicit cell contracts only. Bitwidth supplies its settled semantic ranges
// while applying the range-dependent subset of the shared rewrite machinery.
struct Facts {
  absl::flat_hash_map<hhds::Class_index, size_t>                                   write_costs;
  bool                                                                             track_wiring = true;
  Cprop_wiring                                                                     wiring;
  absl::flat_hash_map<hhds::Class_index, std::pair<Cprop_wiring::Reference, bool>> conditions;
  absl::flat_hash_map<hhds::Class_index, uint64_t>                                 generations;
  uint64_t                                                                         next_generation   = 0;
  size_t                                                                           inspection_budget = 0;
  std::function<int(const Pin&)>                                                   range;
  std::function<void(const Pin&)>                                                  erase_range;
  std::function<void(const Pin&, int)>                                             publish_range;
  absl::flat_hash_map<hhds::Class_index, int>                                      capacities;
  absl::flat_hash_map<hhds::Class_index, Window>                                   support;
};
inline thread_local Facts* active = nullptr;
class Scope {
  Facts* previous = active;

public:
  explicit Scope(Facts& facts) { active = &facts; }
  ~Scope() { active = previous; }
};
inline void forget(const Pin& pin) {
  if (active) {
    if (!graph_util::is_builtin_node(pin.get_master_node())) {
      active->generations.insert_or_assign(pin.get_master_node().get_class_index(), ++active->next_generation);
    }
    active->wiring.invalidate(pin);
    active->write_costs.erase(pin.get_class_index());
    active->conditions.erase(pin.get_class_index());
    active->capacities.erase(pin.get_class_index());
    active->support.erase(pin.get_class_index());
    if (active->erase_range) {
      active->erase_range(pin);
    }
  }
}
inline void forwarded(const Node& node, const Pin& pin) {
  if (active) {
    active->wiring.forward(node.get_driver_pin(0), pin);
  }
}
inline void retire(const Node& node) {
  for (auto output : node.out_sorted_pins()) {
    if (active) {
      active->wiring.retire(output);
    }
    forget(output);
  }
  node.del_node();
}
template <typename... Args>
Node make_node(Args&&... args) {
  auto node = graph_util::create_typed_node(std::forward<Args>(args)...);
  if (active) {
    active->wiring.fresh(node.create_driver_pin(0));
    active->generations.insert_or_assign(node.get_class_index(), ++active->next_generation);
  }
  forget(node.create_driver_pin(0));  // an allocator-recycled pin is a NEW fact
  return node;
}
template <typename... Args>
Node make_get_mask(Args&&... args) {
  auto node = graph_util::create_get_mask(std::forward<Args>(args)...);
  if (active) {
    active->wiring.fresh(node.create_driver_pin(0));
    active->generations.insert_or_assign(node.get_class_index(), ++active->next_generation);
  }
  forget(node.create_driver_pin(0));
  return node;
}
inline int unsigned_width(const Pin& pin) {
  cprop_profile::Timer timer(cprop_profile::range);
  namespace gu = graph_util;
  if (pin.is_invalid()) {
    return -1;
  }
  if (pin.is_const()) {
    const auto& value = gu::const_of(pin);
    if (value.is_negative() || !value.is_numeric()) {
      return -1;
    }
    if (value.has_unknowns()) {
      const int width = value.get_payload_bits();
      return width >= 0 && value.sra_op(Dlop::create_integer(width))->is_known_zero() ? width : -1;
    }
    return value.get_last_bit_set() + 1;
  }
  if (active && active->range) {
    const int width = active->range(pin);
    if (width >= 0) {
      return width;
    }
  }
  if (gu::is_graph_input_pin(pin) || gu::is_graph_output_pin(pin)) {
    return -1;
  }
  auto       node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  if (op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::Ror || op == Ntype_op::Rxor) {
    return 1;
  }
  if (op == Ntype_op::Popcount) {
    return std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(gu::reduction_count(node)))));
  }
  if (op == Ntype_op::Get_mask) {
    auto mask = gu::get_driver_of_sink_name(node, "mask");
    if (mask.is_const() && !gu::const_of(mask).is_negative() && !gu::const_of(mask).has_unknowns()) {
      return gu::const_of(mask).popcount();
    }
  }
  if (active) {
    auto it = active->capacities.find(pin.get_class_index());
    if (it != active->capacities.end()) {
      return it->second;
    }
  }
  return -1;
}
inline bool is_bool01(const Pin& pin) {
  const int width = unsigned_width(pin);
  return width >= 0 && width <= 1;
}
inline Window footprint(const Pin& pin) {
  if (!pin.is_invalid() && pin.is_const()) {
    const auto& value = graph_util::const_of(pin);
    if (value.is_numeric() && !value.is_negative() && !value.has_unknowns()) {
      const int first = value.get_first_bit_set(), last = value.get_last_bit_set();
      return first < 0 ? Window{0, 0} : Window{first, last + 1};
    }
  }

  if (active) {
    auto it = active->support.find(pin.get_class_index());
    if (it != active->support.end()) {
      return it->second;
    }
  }
  // A literal left shift has low zero bits even when its signed tail is
  // unbounded. This fixed-shape fact breaks packed false cycles without BW.
  if (!pin.is_invalid() && !pin.is_const() && graph_util::type_op_of(pin.get_master_node()) == Ntype_op::SHL) {
    auto amount = graph_util::get_driver_of_sink_name(pin.get_master_node(), "b");
    if (amount.is_const() && graph_util::const_of(amount).is_just_i64() && !graph_util::const_of(amount).has_unknowns()) {
      const auto k = graph_util::const_of(amount).to_just_i64();
      if (k >= 0 && k <= (1 << 28)) {
        const int width = unsigned_width(graph_util::get_driver_of_sink_name(pin.get_master_node(), "a"));
        return {static_cast<int>(k),
                width >= 0 && width < (1 << 29) ? static_cast<int>(k) + width : std::numeric_limits<int>::max()};
      }
    }
  }
  const int width = unsigned_width(pin);
  return width < 0 ? unknown_window : Window{0, width};
}
inline void remember(const Node& node) {
  namespace gu = graph_util;
  if (!active || node.is_invalid()) {
    return;
  }
  const auto op = gu::type_op_of(node);
  if (!Ntype::is_comb(op)) {
    return;
  }
  const auto output = node.get_driver_pin(0);
  if (output.is_invalid()) {
    return;
  }
  if (op == Ntype_op::Set_mask || op == Ntype_op::Concat
      || (active->track_wiring && (op == Ntype_op::Get_mask || op == Ntype_op::SHL || op == Ntype_op::SRA))) {
    active->wiring.remember(output);
  }
  if (active->track_wiring && op == Ntype_op::Or && !active->wiring.has_layout(output)) {
    std::vector<Cprop_wiring::Support> inputs;
    for (auto sink : node.inp_sorted_pins()) {
      auto pin     = sink.get_driver_pin();
      auto support = footprint(pin);
      inputs.push_back({pin, support.first, support.second});
    }
    active->wiring.remember_or(output, inputs);
  }
  if (op == Ntype_op::Concat) {
    active->capacities.insert_or_assign(output.get_class_index(), gu::concat_total_width(node));
  }
  if (op == Ntype_op::Set_mask) {
    const auto base       = gu::get_driver_of_sink_name(node, "a");
    const auto mask       = gu::get_driver_of_sink_name(node, "mask");
    const auto window     = mask.is_const() ? gu::mask_window_of(gu::const_of(mask)) : std::nullopt;
    const int  base_width = unsigned_width(base);
    size_t     cost       = 1;
    if (auto prior = active->write_costs.find(base.get_class_index()); prior != active->write_costs.end()) {
      auto edges = base.out_edges();
      auto it    = edges.begin();
      if (it != edges.end() && ++it == edges.end()) {
        cost += prior->second;
      }
    }
    active->write_costs.insert_or_assign(output.get_class_index(), cost);
    if (window && base_width >= 0) {
      active->capacities.insert_or_assign(output.get_class_index(), std::max(base_width, window->second));
    }
  }
  const int width   = unsigned_width(output);
  Window    support = width < 0 ? unknown_window : Window{0, width};
  if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    const auto amount = gu::get_driver_of_sink_name(node, "b");
    if (amount.is_const() && gu::const_of(amount).is_just_i64() && !gu::const_of(amount).has_unknowns()) {
      const auto k      = gu::const_of(amount).to_just_i64();
      const auto source = footprint(gu::get_driver_of_sink_name(node, "a"));
      if (k >= 0 && k <= (1 << 28) && source.first >= 0) {
        if (source.first == source.second) {
          support = {0, 0};
        } else if (op == Ntype_op::SHL && source.second < (1 << 29)) {
          support = {source.first + k, source.second + k};
        } else if (op == Ntype_op::SRA) {
          support = {std::max<int64_t>(0, source.first - k), std::max<int64_t>(0, source.second - k)};
        }
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    const auto mask = gu::get_driver_of_sink_name(node, "mask");
    if (mask.is_const()) {
      auto       window = gu::mask_window_of(gu::const_of(mask));
      const auto source = footprint(gu::get_driver_of_sink_name(node, "a"));
      if (window && source.first >= 0) {
        const int lo = std::max(source.first, window->first), hi = std::min(source.second, window->second);
        support = hi <= lo ? Window{0, 0} : Window{lo - window->first, hi - window->first};
      }
    }
  }
  active->support.insert_or_assign(output.get_class_index(), support);
  if (active->publish_range && width >= 0) {
    active->publish_range(output, width);
  }
}
}  // namespace livehd::cprop_value
