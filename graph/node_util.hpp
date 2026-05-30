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
#include "const.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// ---------------------------------------------------------------------------
// Constant pins (HHDS Graph::CONST_NODE singleton, scheme-A encoded).
// ---------------------------------------------------------------------------
//
// All LiveHD constants live as pins attached to CONST_NODE (nid 3). The
// first 32 port_ids on CONST_NODE are reserved for small integers in the
// range [-16, 15] (scheme A):
//   pid 0..15  -> values 0..15
//   pid 16..31 -> values -16..-1
// Constants outside that range get fresh port_ids starting at 32, with the
// serialized payload attached as `livehd::attrs::pin_const_value`. A
// per-Graph reverse map (LiveHD-side, in const_pin.cpp) provides write-side
// deduplication so the same Const value resolves to the same pin.

inline constexpr hhds::Port_id Const_small_pid_count = 32;

[[nodiscard]] constexpr bool is_small_const_int(int64_t v) { return v >= -16 && v <= 15; }

[[nodiscard]] constexpr hhds::Port_id encode_small_const(int64_t v) {
  return v >= 0 ? static_cast<hhds::Port_id>(v) : static_cast<hhds::Port_id>(v + 32);
}

[[nodiscard]] constexpr int64_t decode_small_const(hhds::Port_id pid) {
  return pid < 16 ? static_cast<int64_t>(pid) : static_cast<int64_t>(pid) - 32;
}

[[nodiscard]] constexpr bool is_small_const_pid(hhds::Port_id pid) { return pid < Const_small_pid_count; }

// Create-or-find the canonical const pin for `value`. Small ints in [-16, 15]
// are pid-encoded directly on CONST_NODE (idempotent, no payload). Larger
// values consult a per-Graph reverse map and reuse an existing pin if the
// same serialized Const is already materialised; otherwise allocate a fresh
// pid (>= 32) and attach the serialized payload.
[[nodiscard]] hhds::Pin_class create_const(hhds::Graph& g, const Const& value);

// Decodes the Const a const pin represents. Handles both new CONST_NODE-pin
// form (pid encoding for small ints, `pin_const_value` attribute otherwise)
// and the legacy `Ntype_op::Nconst` regular-node form (with the per-node
// `const_value` attribute) that the lgraph wrapper still produces.
[[nodiscard]] Const hydrate_const(const hhds::Pin_class& pin);
[[nodiscard]] Const hydrate_const(const hhds::Node_class& node);

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

// True iff `node` is one of HHDS's singleton built-ins (INPUT_NODE,
// OUTPUT_NODE, CONST_NODE). These have nid < 4 and cannot be deleted; passes
// that walk-and-delete must skip them.
[[nodiscard]] inline bool is_builtin_node(const hhds::Node_class& node) {
  if (node.is_invalid()) {
    return false;
  }
  return node.get_debug_nid() < (static_cast<hhds::Nid>(4) << 2);
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
  auto n    = node_name_of(node);
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
  auto master  = pin.get_master_node();
  // Graph-IO pins carry their declared name on GraphIO; cgen handles those via
  // its IO-walk path before falling through here. For internal pins we generate
  // a synthetic name from the master node + port_id.
  auto base    = default_instance_name(master);
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
inline void set_bits(const hhds::Pin_class& pin, Bits_t b) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::bits).set(b);
}

inline void clear_bits(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::bits).del();
}

// Per-pin sign hint. 1 = unsigned, absent = signed. Mirrors LiveHD's
// `Node_pin::set_sign` / `set_unsign`.
inline void set_unsign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_unsigned).set(1);
}

inline void set_sign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_unsigned).del();
}

// Per-node color taint.
inline void set_color(const hhds::Node_class& node, int32_t c) { node.attr(livehd::attrs::color).set(c); }
inline void clear_color(const hhds::Node_class& node) { node.attr(livehd::attrs::color).del(); }

// User-assigned pin name. Empty string clears the attr.
inline void set_pin_name(const hhds::Pin_class& pin, std::string_view name) {
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
  if (pid == livehd::Port_invalid) {
    return {};
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
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
inline void set_type_op(const hhds::Node_class& node, Ntype_op op) {
  node.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(op)));
}

// Set this node to a constant node carrying the given serialized Const.
// Migrated callers serialize with `Dlop::serialize()` before calling this
// (we don't pull Dlop into node_util.hpp so it stays a leaf header).
inline void set_type_const_serialized(const hhds::Node_class& node, std::string_view serialized) {
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

// Create-if-missing sink pin lookup by LiveHD-style name. For Sub nodes the
// name path goes through HHDS's create_sink_pin(name) directly; for other ops
// we translate name→port_id via Ntype and call create_sink_pin(port_id).
[[nodiscard]] inline hhds::Pin_class setup_sink_by_name(const hhds::Node_class& node, std::string_view name) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.create_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == livehd::Port_invalid) {
    return {};
  }
  return node.create_sink_pin(pid);
}

// Create a new node of `op` in `graph` and stamp port-0 driver bits.
[[nodiscard]] inline hhds::Node_class create_typed_node(hhds::Graph& graph, Ntype_op op, Bits_t bits) {
  auto node = graph.create_node();
  set_type_op(node, op);
  if (bits != 0) {
    auto dpin = node.create_driver_pin(0);
    dpin.attr(livehd::attrs::bits).set(bits);
  }
  return node;
}

// Create a new node of `op` in the same graph as `origin`. Mirrors LiveHD's
// `Node::create(op)` (the new node belongs to the same Lgraph the origin is on).
[[nodiscard]] inline hhds::Node_class create_node_on(const hhds::Node_class& origin, Ntype_op op) {
  return create_typed_node(*origin.get_graph(), op);
}

// Per-pin offset (used by Get_mask / Set_mask / Sext positional ops).
inline void set_pin_offset(const hhds::Pin_class& pin, Bits_t off) {
  if (pin.is_invalid()) {
    return;
  }
  if (off == 0) {
    pin.attr(livehd::attrs::pin_offset).del();
  } else {
    pin.attr(livehd::attrs::pin_offset).set(off);
  }
}

// Per-node source filename / source line. Mirrors LiveHD `Node::set_source`
// and `Node::set_loc1`.
inline void set_source(const hhds::Node_class& node, std::string_view fname) {
  if (fname.empty()) {
    node.attr(livehd::attrs::source).del();
  } else {
    node.attr(livehd::attrs::source).set(std::string{fname});
  }
}

inline void set_loc1(const hhds::Node_class& node, uint64_t line) {
  node.attr(livehd::attrs::loc).set(livehd::attrs::loc_t::value_type{line, 0});
}

// All drivers feeding a named sink port. Used by passes (memory, bit_or)
// that allow multiple drivers on the same sink port_id.
[[nodiscard]] inline std::vector<hhds::Pin_class> inp_drivers_of(const hhds::Node_class& node, std::string_view name) {
  std::vector<hhds::Pin_class> result;
  if (node.is_invalid()) {
    return result;
  }
  auto          op = type_op_of(node);
  hhds::Port_id target;
  if (op == Ntype_op::Sub) {
    auto pin = node.get_sink_pin(name);
    if (pin.is_invalid()) {
      return result;
    }
    target = pin.get_port_id();
  } else {
    auto pid = Ntype::get_sink_pid(op, name);
    if (pid == livehd::Port_invalid) {
      return result;
    }
    target = pid;
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == target) {
      result.push_back(e.driver);
    }
  }
  return result;
}

}  // namespace livehd::graph_util
