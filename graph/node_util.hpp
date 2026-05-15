//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LiveHD-flavored conveniences on top of hhds::Node_class / hhds::Pin_class.
//
// The lgraph/ wrapper used to provide these as methods on Node / Node_pin.
// Migrated passes (cgen, bitwidth, cprop, yosys) need the same conveniences
// directly on the HHDS types without taking a //lgraph BUILD dep.
//
// Pattern: free functions named `*_of` rather than HHDS-side methods, so the
// HHDS library stays free of LiveHD-specific encodings (Ntype_op bit-shift,
// the "%dot.name" wire-naming scheme, etc.).

#include <cstdint>
#include <string>
#include <string_view>

#include "attrs.hpp"
#include "cell.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// HHDS stores the user-supplied type in NodeEntry::type with the low bit
// reserved for HHDS's own `is_loop_last` semantics. LiveHD's mirror
// (`Lgraph::mirror_set_type_hhds`) shifts Ntype_op left by 1 before storing,
// so callers must shift right by 1 to decode. Returns Ntype_op::Invalid for
// nodes that were never typed (default-zero -> Invalid happens to be slot 0
// after the shift, but graph-IO pseudo-nodes also live at low Nids).
[[nodiscard]] inline Ntype_op type_op_of(const hhds::Node_class& node) {
  return static_cast<Ntype_op>(static_cast<uint16_t>(node.get_type()) >> 1);
}

[[nodiscard]] inline bool is_type_of(const hhds::Node_class& node, Ntype_op op) { return type_op_of(node) == op; }

[[nodiscard]] inline bool is_type_flop(const hhds::Node_class& node) {
  auto op = type_op_of(node);
  return op == Ntype_op::Flop || op == Ntype_op::Fflop;
}

[[nodiscard]] inline bool is_type_register(const hhds::Node_class& node) {
  auto op = type_op_of(node);
  return op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Memory || op == Ntype_op::Fflop;
}

[[nodiscard]] inline bool is_type_const(const hhds::Node_class& node) {
  // Constants live on the CONST_NODE singleton. A driver pin attached to
  // CONST_NODE is a constant pin (Graph::create_constant() returns one).
  // get_debug_nid returns the raw Nid for this node handle.
  return node.get_debug_nid() == hhds::Graph::CONST_NODE;
}

[[nodiscard]] inline bool is_type_sub(const hhds::Node_class& node) { return type_op_of(node) == Ntype_op::Sub; }

// Per-pin bit width. `livehd::attrs::bits` holds the value (0 == unspecified).
// For graph-IO pins, the declared bits live on `GraphIO::get_bits(name)`
// instead — caller decides which path applies.
[[nodiscard]] inline Bits_t bits_of(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return 0;
  }
  auto a = pin.attr(livehd::attrs::bits);
  return a.has() ? static_cast<Bits_t>(a.get()) : Bits_t{0};
}

[[nodiscard]] inline Bits_t bits_of(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view io_name) {
  if (auto b = bits_of(pin); b != 0) {
    return b;
  }
  return static_cast<Bits_t>(gio.get_bits(io_name));
}

// Per-pin sign hint. 1 = unsigned, absent = signed (mirrors LiveHD's `is_unsign`).
[[nodiscard]] inline bool is_unsign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto a = pin.attr(livehd::attrs::pin_unsigned);
  return a.has() && a.get() != 0;
}

// Per-node color taint (set by pass diagnostics).
[[nodiscard]] inline bool has_color(const hhds::Node_class& node) { return node.attr(livehd::attrs::color).has(); }

[[nodiscard]] inline int32_t color_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::color);
  return a.has() ? a.get() : 0;
}

// Per-pin / per-node user-assigned name. Returns empty string when no name
// attribute is present.
[[nodiscard]] inline std::string_view pin_name_of(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return {};
  }
  auto a = pin.attr(livehd::attrs::pin_name);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
}

[[nodiscard]] inline std::string_view node_name_of(const hhds::Node_class& node) {
  if (node.is_invalid()) {
    return {};
  }
  auto a = node.attr(hhds::attrs::name);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
}

[[nodiscard]] inline bool has_name(const hhds::Node_class& node) { return node.attr(hhds::attrs::name).has(); }

// Sub-graph reference id (for Ntype_op::Sub nodes).
[[nodiscard]] inline uint32_t subid_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::subid);
  return a.has() ? a.get() : uint32_t{0};
}

// Serialized const value (for Ntype_op::Nconst nodes). Empty if absent.
[[nodiscard]] inline std::string_view const_value_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::const_value);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
}

// Pin / node classification by master node id (no attr lookup).
[[nodiscard]] inline bool is_graph_input_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto master = pin.get_master_node();
  return master.get_debug_nid() == hhds::Graph::INPUT_NODE;
}

[[nodiscard]] inline bool is_graph_output_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto master = pin.get_master_node();
  return master.get_debug_nid() == hhds::Graph::OUTPUT_NODE;
}

[[nodiscard]] inline bool is_const_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto master = pin.get_master_node();
  if (master.get_debug_nid() == hhds::Graph::CONST_NODE) {
    return true;
  }
  // LiveHD's Lgraph wrapper materialises constants as Ntype_op::Nconst
  // regular nodes (with the value attached via livehd::attrs::const_value),
  // distinct from HHDS's CONST_NODE singleton. Both are valid constant
  // sources for cgen.
  return type_op_of(master) == Ntype_op::Nconst;
}

// LiveHD's `default_instance_name`: a deterministic name derived from
// `<type>_<nid>` if the node has no user-assigned name, otherwise the
// user-assigned name. Used by cgen to label module instances and memories.
[[nodiscard]] inline std::string default_instance_name(const hhds::Node_class& node) {
  auto n = node_name_of(node);
  if (!n.empty()) {
    return std::string{n};
  }
  return std::string{Ntype::get_name(type_op_of(node))} + "_" + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
}

// LiveHD's `debug_name` (cell+nid+name) — used in error messages.
[[nodiscard]] inline std::string debug_name(const hhds::Node_class& node) {
  auto n = node_name_of(node);
  auto base = std::string{Ntype::get_name(type_op_of(node))} + "_" + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
  if (!n.empty()) {
    base.append(":").append(n);
  }
  return base;
}

// Wire-name generation: prefer the user-assigned pin name; otherwise fall
// back to the node's debug name + port id. The cgen output uses this to
// name driver pins as Verilog wires.
[[nodiscard]] inline std::string wire_name(const hhds::Pin_class& pin) {
  auto pn = pin_name_of(pin);
  if (!pn.empty()) {
    return std::string{pn};
  }
  if (pin.is_invalid()) {
    return {};
  }
  auto master = pin.get_master_node();
  // Graph-IO pins carry their declared name on GraphIO; cgen handles those via
  // its IO-walk path before falling through here. For internal pins we generate
  // a synthetic name from the master node + port_id.
  auto base = default_instance_name(master);
  auto port_id = pin.get_port_id();
  if (port_id == 0) {
    return base;
  }
  return base + "_" + std::to_string(static_cast<uint32_t>(port_id));
}

}  // namespace livehd::graph_util
