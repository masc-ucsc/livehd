//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Definitions for `lean_common.hpp`.  Moved verbatim out of pass_lean.cpp's
// anonymous namespace: these needed external linkage once a direction's emitter
// became its own translation unit.

#include "lean_common.hpp"

namespace livehd::lean_ir {

uint32_t node_id(const Node& node) { return static_cast<uint32_t>(node.get_debug_nid()); }

Node pin_node(const Node_pin& pin) { return pin.get_master_node(); }

Ntype_op node_op(const Node& node) { return livehd::graph_util::type_op_of(node); }

bool node_is_flop(const Node& node) { return livehd::graph_util::is_type_flop(node); }

bool node_is_memory(const Node& node) { return node_op(node) == Ntype_op::Memory; }

bool pin_is_input(const Node_pin& pin) { return livehd::graph_util::is_graph_input_pin(pin); }

bool pin_is_const(const Node_pin& pin) { return livehd::graph_util::is_const_pin(pin); }

livehd::graph_util::Edge_vec inp_edges_ordered(const Node& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    const auto ap = a.sink.get_port_id();
    const auto bp = b.sink.get_port_id();
    if (ap != bp) {
      return ap < bp;
    }
    return a.driver.get_class_index().value < b.driver.get_class_index().value;
  });
  return edges;
}

std::string sink_pin_name(const Edge& edge) {
  const auto sink_node = pin_node(edge.sink);
  return std::string(Ntype::get_sink_name(node_op(sink_node), edge.sink.get_port_id()));
}

uint32_t raw_pin_width(const Node_pin& pin) { return static_cast<uint32_t>(livehd::graph_util::bits_of(pin)); }

uint32_t raw_node_width(const Node& node) { return raw_pin_width(node.create_driver_pin(0)); }

Dlop pin_const_value(const Node_pin& pin) { return livehd::graph_util::hydrate_const(pin); }

Dlop node_const_value(const Node& node) { return livehd::graph_util::hydrate_const(node); }

bool node_output_is_signed(const Node& node) {
  auto n    = node;
  auto dpin = n.create_driver_pin(0);
  return !dpin.is_invalid() && !livehd::graph_util::is_unsign(dpin);
}

[[noreturn]] void fatal(const LeanCtx& /*ctx*/, const std::string& msg) { throw Emit_error("[ERROR] pass.lean: " + msg); }

Node_pin resolve_resize_chain(const Node_pin& start) {
  Node_pin cur = start;
  for (int guard = 0; guard < 32; ++guard) {
    if (cur.is_invalid() || pin_is_input(cur) || pin_is_const(cur)) {
      return cur;
    }
    auto n  = pin_node(cur);
    auto op = node_op(n);
    auto es = inp_edges_ordered(n);
    if (op == Ntype_op::Or && es.size() == 1) {
      cur = es[0].driver;  // arity-1 Or is the emitter's resize
      continue;
    }
    if (op == Ntype_op::Get_mask && es.size() == 2) {
      // Transparent only when the mask is a constant all-ones: get_mask(a,-1) is zext(a).
      const auto& mask = es[1].driver;
      if (pin_is_const(mask)) {
        auto v = pin_const_value(mask);
        if (v.is_just_i64() && v.to_just_i64() == -1) {
          cur = es[0].driver;
          continue;
        }
      }
    }
    return cur;
  }
  return cur;
}


std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin) {
  // pin_name_of resolves a graph-input pin's declared port name directly (via
  // the graph's IO maps); no need to identity-match against get_input_pin.
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_field.contains(pname)) {
    return pname;
  }
  return {};
}

}  // namespace livehd::lean_ir
