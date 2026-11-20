// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "attrs.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace livehd::graph_util {
// Structural correspondence of placed replicas. Names nominate candidates;
// the complete ordered input wiring, master and initialization must agree.
// Consumers establishing semantic equality must additionally prove state
// initialization and the transition function in their technology/model.
inline bool is_placed_replica(hhds::Node_class node) {
  return node.get_name().starts_with("__wl_copy_") && node.attr(attrs::aggregate_origin).has()
         && !node.attr(attrs::user_cell).has();
}
inline std::map<uint64_t, hhds::Node_class> placed_replica_sources(hhds::Graph& graph) {
  std::map<uint64_t, hhds::Node_class> sources;
  if (!graph.has_attr(attrs::aggregate_origin)) {
    return sources;
  }
  std::map<std::string, std::vector<hhds::Node_class>> by_origin;
  std::vector<hhds::Node_class>                        copies;
  for (auto node : graph.body().nodes()) {
    auto origin = node.attr(attrs::aggregate_origin);
    if (!origin.has()) {
      continue;
    }
    if (is_placed_replica(node)) {
      copies.push_back(node);
    } else {
      by_origin[origin.get()].push_back(node);
    }
  }
  for (auto copy : copies) {
    auto io = copy.get_subnode_io();
    if (!io || io->get_output_pin_decls().size() != 1) {
      continue;
    }
    for (auto original : by_origin[copy.attr(attrs::aggregate_origin).get()]) {
      auto a = copy.attr(attrs::simulation_init), b = original.attr(attrs::simulation_init);
      if (original.get_subnode_io() != io || a.has() != b.has() || (a.has() && a.get() != b.get())) {
        continue;
      }
      bool equal = true;
      for (const auto& p : io->get_input_pin_decls()) {
        const auto x = original.get_sink_pin(p.name), y = copy.get_sink_pin(p.name);
        if (x.is_invalid() || y.is_invalid() || x.get_driver_pins().size() != 1 || y.get_driver_pins().size() != 1
            || x.get_driver_pin() != y.get_driver_pin()) {
          equal = false;
          break;
        }
      }
      if (equal) {
        sources.emplace(copy.get_debug_nid(), original);
        break;
      }
    }
  }
  return sources;
}
// Native flops after behavioral readback lose placement attributes. A name
// only nominates a pair. A directly asserted reset and equal, concrete reset
// value establish the base case. Match every transition operand, allowing
// the one self-feedback mux produced by lowering a clock enable.
inline std::map<uint64_t, hhds::Node_class> reset_replica_sources(hhds::Graph& graph) {
  using Node        = hhds::Node_class;
  using Pin         = hhds::Pin_class;
  // The common case has no placement copies. Scan only stored names before
  // allocating an index of registers or visiting their pins.
  const auto marked = [&](const auto& tag) {
    if (!graph.has_attr(tag)) {
      return false;
    }
    for (const auto& [key, name] : graph.attr_store(tag)) {
      if (name.starts_with("__wl_copy_")) {
        return true;
      }
    }
    return false;
  };
  if (!marked(hhds::attrs::name) && !marked(attrs::pin_name)) {
    return {};
  }
  std::map<std::string, Node>               originals;
  std::vector<std::pair<std::string, Node>> copies;
  std::set<std::string>                     ambiguous;
  for (auto n : graph.body().nodes()) {
    if (type_op_of(n) != Ntype_op::Flop) {
      continue;
    }
    auto q    = n.get_driver_pin(0);
    auto name = std::string(pin_name_of(q));
    if (name.empty()) {
      name = node_name_of(n);
    }
    constexpr std::string_view prefix = "__wl_copy_";
    if (name.starts_with(prefix)) {
      copies.emplace_back(name.substr(prefix.size()), n);
    } else if (!originals.emplace(name, n).second) {
      ambiguous.insert(name);
    }
  }
  std::map<uint64_t, Node> result;
  for (auto& [name, copy] : copies) {
    const auto at = originals.find(name);
    if (at == originals.end() || ambiguous.contains(name)) {
      continue;
    }
    auto original = at->second;
    auto rst = get_driver_of_sink_name(original, "reset_pin"), clk = get_driver_of_sink_name(original, "clock_pin");
    auto init = get_driver_of_sink_name(original, "initial");
    if (rst.is_invalid() || rst.is_const() || !rst.get_master_node().is_input_node() || clk.is_invalid() || clk.is_const()
        || !clk.get_master_node().is_input_node() || !init.is_const() || const_of(init).has_unknowns()) {
      continue;
    }
    auto qa = original.get_driver_pin(0), qb = copy.get_driver_pin(0);
    if (bits_of(qa) <= 0 || bits_of(qb) != bits_of(qa) || is_unsign(qa) != is_unsign(qb)) {
      continue;
    }
    const auto equal = [&](Pin x, Pin y) {
      if (x == y) {
        return true;
      }
      if (bits_of(x) != bits_of(y) || is_unsign(x) != is_unsign(y)) {
        return false;
      }
      if (x.is_const() && y.is_const()) {
        return const_of(x).is_known_eq(const_of(y));
      }
      return x == qa && y == qb;
    };
    const auto corresponding = [&](Node x, Node y, bool allow_mux) {
      const auto xs = x.inp_sorted_pins(), ys = y.inp_sorted_pins();
      if (xs.size() != ys.size()) {
        return false;
      }
      for (auto sink : xs) {
        auto other = y.get_sink_pin(sink.get_port_id());
        if (other.is_invalid() || sink.get_driver_pins().size() != 1 || other.get_driver_pins().size() != 1) {
          return false;
        }
        auto a = sink.get_driver_pin(), b = other.get_driver_pin();
        if (equal(a, b)) {
          continue;
        }
        if (!allow_mux || a.is_const() || b.is_const() || bits_of(a) != bits_of(b) || is_unsign(a) != is_unsign(b)) {
          return false;
        }
        auto mx = a.get_master_node(), my = b.get_master_node();
        if (type_op_of(mx) != Ntype_op::Mux || type_op_of(my) != Ntype_op::Mux || a.get_port_id() != b.get_port_id()) {
          return false;
        }
        auto pins = mx.inp_sorted_pins();
        if (pins.size() != my.inp_sorted_pins().size()) {
          return false;
        }
        for (auto pin : pins) {
          auto right = my.get_sink_pin(pin.get_port_id());
          if (right.is_invalid() || pin.get_driver_pins().size() != 1 || right.get_driver_pins().size() != 1
              || !equal(pin.get_driver_pin(), right.get_driver_pin())) {
            return false;
          }
        }
      }
      return true;
    };
    if (corresponding(original, copy, true)) {
      result.emplace(copy.get_debug_nid(), original);
    }
  }
  return result;
}
}  // namespace livehd::graph_util
