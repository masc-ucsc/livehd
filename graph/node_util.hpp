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
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// HHDS stores the user-supplied type in NodeEntry::type with the low bit
// reserved for HHDS's own `is_loop_last` semantics. The Ntype_op encoding
// is designed so that bit 0 of the underlying value already encodes
// is_loop_last (see cell.hpp), so the stored HHDS type round-trips
// directly into an Ntype_op without any shift. Returns Ntype_op::Invalid
// (== 0) for nodes that were never typed.
[[nodiscard]] inline Ntype_op type_op_of(const hhds::Node_class& node) {
  return static_cast<Ntype_op>(static_cast<uint16_t>(node.get_type()));
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

// ---------------------------------------------------------------------------
// Mutation helpers (used by migrated bitwidth / cprop / yosys passes).
// ---------------------------------------------------------------------------
//
// These mirror the small set of mutators each migrated pass needs without
// taking a //lgraph BUILD dep. They write the same HHDS attribute tags the
// Lgraph wrapper mirrors to, so a node mutated through these helpers looks
// the same to subsequent migrated readers as if it had been mutated through
// the Lgraph wrapper.

// Per-pin bit width. Bits_t == int32_t (graph_sizing.hpp). Storing 0 leaves
// the attribute untouched logically (callers should use clear_bits instead).
inline void set_bits(hhds::Pin_class& pin, Bits_t b) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::bits).set(b);
}

inline void clear_bits(hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::bits).del();
}

// Per-pin sign hint. 1 = unsigned, absent = signed. Mirrors LiveHD's
// `Node_pin::set_sign` / `set_unsign`.
inline void set_unsign(hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_unsigned).set(1);
}

inline void set_sign(hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_unsigned).del();
}

// Per-node color taint.
inline void set_color(hhds::Node_class& node, int32_t c) { node.attr(livehd::attrs::color).set(c); }
inline void clear_color(hhds::Node_class& node) { node.attr(livehd::attrs::color).del(); }

// User-assigned pin name. Empty string clears the attr.
inline void set_pin_name(hhds::Pin_class& pin, std::string_view name) {
  if (pin.is_invalid()) {
    return;
  }
  if (name.empty()) {
    pin.attr(livehd::attrs::pin_name).del();
  } else {
    pin.attr(livehd::attrs::pin_name).set(std::string{name});
  }
}

// ---------------------------------------------------------------------------
// Sink-pin lookup by LiveHD-style name ("a", "din", "0addr", ...).
// ---------------------------------------------------------------------------
// HHDS does not carry the LiveHD sink-name convention. We translate the name
// to a port_id via Ntype, then walk inp_edges() to find a sink pin with the
// matching port_id. This emulates LiveHD's "invalid pin on missing pin"
// behaviour (HHDS asserts when get_sink_pin is called on an unmaterialized
// pin). For Sub nodes the name path goes through HHDS's get_sink_pin
// directly because the sub-graph's GraphIO carries the names.

[[nodiscard]] inline hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.get_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid < 0) {
    return {};
  }
  auto target = static_cast<hhds::Port_id>(pid);
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == target) {
      return e.sink;
    }
  }
  return {};
}

[[nodiscard]] inline bool is_sink_connected(const hhds::Node_class& node, std::string_view name) {
  return !find_sink_pin(node, name).is_invalid();
}

// Returns the (single) driver pin feeding a named sink. If the sink is
// unconnected, returns an invalid pin. Multiple drivers (multi-driver sinks)
// require inp_drivers_of instead.
[[nodiscard]] inline hhds::Pin_class get_driver_of_sink_name(const hhds::Node_class& node, std::string_view name) {
  auto sink = find_sink_pin(node, name);
  if (sink.is_invalid()) {
    return {};
  }
  auto edges = sink.inp_edges();
  if (edges.empty()) {
    return {};
  }
  return edges.front().driver;
}

// Cell-type mutation. The Ntype_op encoding values already bake in the
// is_loop_last low bit (see cell.hpp), so storing the raw value into HHDS
// round-trips correctly with type_op_of(). Do NOT shift.
inline void set_type_op(hhds::Node_class& node, Ntype_op op) {
  node.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(op)));
}

// Set this node to a constant node carrying the given serialized Const.
// Migrated callers serialize with `Dlop::serialize()` before calling this
// (we don't pull Dlop into node_util.hpp so it stays a leaf header).
inline void set_type_const_serialized(hhds::Node_class& node, std::string_view serialized) {
  set_type_op(node, Ntype_op::Nconst);
  if (serialized.empty()) {
    node.attr(livehd::attrs::const_value).del();
  } else {
    node.attr(livehd::attrs::const_value).set(std::string{serialized});
  }
}

// Create a new node of the given Ntype_op in `graph`. Returns the node;
// callers may then create driver / sink pins on it as needed.
[[nodiscard]] inline hhds::Node_class create_typed_node(hhds::Graph& graph, Ntype_op op) {
  auto node = graph.create_node();
  set_type_op(node, op);
  return node;
}

// Create a new Nconst node in `graph` carrying the given serialized Const
// value, and return its port-0 driver pin (matches the LiveHD wrapper
// behaviour: `Lgraph::create_node_const(C).setup_driver_pin()`).
[[nodiscard]] inline hhds::Pin_class create_const_node_serialized(hhds::Graph& graph, std::string_view serialized) {
  auto node = create_typed_node(graph, Ntype_op::Nconst);
  if (!serialized.empty()) {
    node.attr(livehd::attrs::const_value).set(std::string{serialized});
  }
  return node.create_driver_pin(0);
}

// All drivers feeding a named sink port. Used by passes (memory, bit_or)
// that allow multiple drivers on the same sink port_id.
[[nodiscard]] inline std::vector<hhds::Pin_class> inp_drivers_of(const hhds::Node_class& node, std::string_view name) {
  std::vector<hhds::Pin_class> result;
  if (node.is_invalid()) {
    return result;
  }
  auto op = type_op_of(node);
  hhds::Port_id target;
  if (op == Ntype_op::Sub) {
    auto pin = node.get_sink_pin(name);
    if (pin.is_invalid()) {
      return result;
    }
    target = pin.get_port_id();
  } else {
    auto pid = Ntype::get_sink_pid(op, name);
    if (pid < 0) {
      return result;
    }
    target = static_cast<hhds::Port_id>(pid);
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == target) {
      result.push_back(e.driver);
    }
  }
  return result;
}

}  // namespace livehd::graph_util
