// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cprop_muxctx.hpp"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cprop_value.hpp"

namespace livehd::muxctx {
namespace gu = graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;

std::optional<Equality> equality(const Pin& pin) {
  if (pin.is_invalid() || pin.is_const() || gu::type_op_of(pin.get_master_node()) != Ntype_op::EQ) {
    return {};
  }
  std::array<Pin, 2> operands;
  size_t             count = 0;
  for (auto sink : pin.get_master_node().inp_sorted_pins()) {
    if (count == 2) {
      return {};
    }
    operands[count++] = sink.get_driver_pin();
  }
  if (count != 2 || operands[0].is_invalid() || operands[1].is_invalid()) {
    return {};
  }
  if (operands[0].is_const()) {
    std::swap(operands[0], operands[1]);
  }
  if (operands[0].is_const() || !operands[1].is_const()) {
    return {};
  }
  const auto& c = gu::const_of(operands[1]);
  if (!c.is_numeric() || c.has_unknowns() || !c.is_just_i64()) {
    return {};
  }
  return Equality{operands[0], c.to_just_i64()};
}

bool exclusive(const Node& node) {
  if (gu::has_runtime_check(node)) {
    return false;
  }
  if (gu::proven_of(node) == gu::kFormalOnehot) {
    return true;
  }
  Pin                          selector;
  absl::flat_hash_set<int64_t> constants;
  size_t                       count = 0;
  // Validate dense pairs before calling hotmux_inputs (which asserts density).
  std::vector<Pin>             pins;
  for (auto sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() != count++ || sink.get_driver_pin().is_invalid()) {
      return false;
    }
    pins.push_back(sink.get_driver_pin());
  }
  for (size_t i = 0; i + 1 < pins.size(); i += 2) {
    const auto d = equality(pins[i]);
    if (!d || (!selector.is_invalid() && selector != d->value) || !constants.insert(d->constant).second) {
      return false;
    }
    selector = d->value;
  }
  return !selector.is_invalid();
}

std::optional<bool> Path_facts::compare(Pin value, int64_t constant) const {
  for (size_t i = 0; i < used; ++i) {
    const auto& fact = facts[i];
    if (fact.value == value) {
      if (fact.equal) {
        return fact.constant == constant;
      }
      if (fact.constant == constant) {
        return false;
      }
    }
  }
  return {};
}

void Path_facts::add(Pin value, int64_t constant, bool equal) {
  if (auto known = compare(value, constant)) {
    possible &= *known == equal;
  } else if (used < facts.size()) {
    facts[used++] = {value, constant, equal};
  }
}

std::optional<bool> Path_facts::truth(Pin pin, const Decode& decode) const {
  if (auto c = const_truth(pin)) {
    return c;
  }
  const auto condition = decode(pin);
  if (!condition) {
    return {};
  }
  auto base  = condition->base;
  auto truth = const_truth(base);
  if (!truth) {
    if (auto zero = compare(base, 0)) {
      truth = !*zero;
    } else if (auto eq = equality(base)) {
      truth = compare(eq->value, eq->constant);
    }
  }
  return truth ? std::optional<bool>{*truth == condition->true_when_base} : std::nullopt;
}

void Path_facts::assume(Pin pin, bool truth, const Decode& decode) {
  if (auto known = this->truth(pin, decode)) {
    possible &= *known == truth;
    return;
  }
  const auto condition = decode(pin);
  if (!condition) {
    return;
  }
  const bool base_truth = truth == condition->true_when_base;
  if (auto eq = equality(condition->base)) {
    add(eq->value, eq->constant, base_truth);
  } else {
    add(condition->base, 0, !base_truth);
  }
}

size_t prune(hhds::Graph& graph, const Decode& decode, const Boolean& boolean) {
  struct Candidate {
    Node             node;
    std::vector<Pin> pins;
    bool             hot;
    bool             owned = false;
  };
  std::vector<Candidate>                         nodes;
  absl::flat_hash_map<hhds::Class_index, size_t> ids;
  for (auto node : graph.body().nodes()) {
    const auto op = gu::type_op_of(node);
    if ((op != Ntype_op::Mux && op != Ntype_op::Hotmux) || gu::has_color(node) || gu::has_runtime_check(node)
        || !node.has_out_edges() || (op == Ntype_op::Hotmux && !exclusive(node))) {
      continue;
    }
    Candidate c{node, {}, op == Ntype_op::Hotmux};
    bool      valid = true;
    for (auto sink : node.inp_sorted_pins()) {
      const auto pin = sink.get_driver_pin();
      if (sink.get_port_id() != c.pins.size() || pin.is_invalid() || (pin.is_const() && gu::const_of(pin).has_unknowns())) {
        valid = false;
        break;
      }
      // State holds belong to enableopt; never move the direct Q arm.
      const auto source = gu::type_op_of(pin.get_master_node());
      if (source == Ntype_op::Latch || source == Ntype_op::Flop) {
        valid = false;
        break;
      }
      c.pins.push_back(pin);
    }
    if (!valid || (!c.hot && c.pins.size() != 3) || (c.hot && c.pins.size() < 2)) {
      continue;
    }
    ids.emplace(node.get_class_index(), nodes.size());
    nodes.push_back(std::move(c));
  }
  for (auto& c : nodes) {
    if (gu::has_name(c.node) || !gu::pin_name_of(c.node.get_driver_pin(0)).empty()) {
      continue;
    }
    auto edges = c.node.out_edges();
    auto it    = edges.begin();
    if (it == edges.end()) {
      continue;
    }
    const auto sink = (*it).sink;
    if (++it != edges.end()) {
      continue;
    }
    auto parent = ids.find(sink.get_master_node().get_class_index());
    if (parent != ids.end()) {
      const auto& p   = nodes[parent->second];
      const auto  pid = sink.get_port_id();
      c.owned         = p.hot ? pid % 2 == 1 || pid + 1 == p.pins.size() : pid != 0;
    }
  }
  struct Visit {
    size_t     id;
    Path_facts facts;
    Pin        consumer;
  };
  std::vector<Visit> pending;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].owned) {
      pending.push_back({i, {}, {}});
    }
  }
  absl::flat_hash_set<size_t> visited;
  const auto                  zero    = gu::create_const(graph, *Dlop::create_integer(0));
  const auto                  one     = gu::create_const(graph, *Dlop::create_integer(1));
  size_t                      changed = 0;
  const auto                  replace = [&](Pin sink, Pin value) {
    const auto old = sink.get_driver_pin();
    if (old != value) {
      cprop_value::forget(sink.get_master_node().get_driver_pin(0));
      old.del_sink(sink);
      value.connect_sink(sink);
      ++changed;
    }
  };
  while (!pending.empty()) {
    auto visit = std::move(pending.back());
    pending.pop_back();
    if (!visit.facts.reachable() || !visited.insert(visit.id).second) {
      continue;
    }
    auto&      c       = nodes[visit.id];
    const auto descend = [&](Pin sink, Pin value, const Path_facts& facts) {
      if (!facts.reachable()) {
        return;
      }
      if (boolean(value)) {
        if (auto truth = facts.truth(value, decode)) {
          replace(sink, *truth ? one : zero);
          return;
        }
      }
      auto child = ids.find(value.get_master_node().get_class_index());
      if (child != ids.end() && nodes[child->second].owned) {
        pending.push_back({child->second, facts, sink});
      }
    };
    if (!c.hot) {
      if (auto truth = visit.facts.truth(c.pins[0], decode); truth && !visit.consumer.is_invalid()) {
        auto value = c.pins[*truth ? 2 : 1];
        replace(visit.consumer, value);
        descend(visit.consumer, value, visit.facts);
        continue;
      }
      for (size_t i = 1; i < 3; ++i) {
        auto facts = visit.facts;
        facts.assume(c.pins[0], i == 2, decode);
        descend(c.node.get_sink_pin(i), c.pins[i], facts);
      }
    } else {
      auto fallback_facts = visit.facts;
      for (size_t i = 0; i + 1 < c.pins.size(); i += 2) {
        if (auto truth = visit.facts.truth(c.pins[i], decode); truth && !*truth) {
          // Removing an activation preserves global exclusivity; asserting one
          // only under a path would invalidate the global proof outside it.
          gu::set_proven(c.node, gu::kFormalOnehot);
          replace(c.node.get_sink_pin(i), zero);
        }
        auto facts = visit.facts;
        facts.assume(c.pins[i], true, decode);
        descend(c.node.get_sink_pin(i + 1), c.pins[i + 1], facts);
        fallback_facts.assume(c.pins[i], false, decode);
      }
      if (c.pins.size() % 2) {
        descend(c.node.get_sink_pin(c.pins.size() - 1), c.pins.back(), fallback_facts);
      }
    }
  }
  return changed;
}
}  // namespace livehd::muxctx
