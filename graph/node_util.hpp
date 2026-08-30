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

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"

namespace livehd::graph_util {

// The exact container hhds inp_edges()/out_edges() return — an
// absl::InlinedVector (heap-free for the common low-degree pin). Aliased via
// decltype so it tracks hhds's choice (inline size, element type) and can never
// drift out of sync with the accessor it must bind to.
using Edge_vec = decltype(std::declval<hhds::Node_class>().inp_edges());

// Reserved sub-module name for the runtime range-select guard `a#[lo..=hi]`
// emits (see upass_tolg lower_range_assert). It is a recognized PRIMITIVE: a
// Sub instance whose subnode names this module is NOT a real sub-graph — cgen
// lowers it to an inline SystemVerilog immediate assertion (`assert(cond)`)
// rather than a module instantiation, so no `lgassert` module body is ever
// needed and LEC (which only compares data outputs) is unaffected.
inline constexpr std::string_view lgassert_module_name = "lgassert";

// Reserved sub-module name for a materialized user property (pass/formal, task
// 2f-formal): an `fproperty` Sub instance carries a 1-bit `cond` plus, packed in
// its instance-name attr, "<kind>\x1f<loc>\x1f<msg>" (kind = assert |
// assert_always | assume | assume_nocheck). pass.formal proves/defers it; cgen emits a runtime
// check for the ones it could not prove (skipping any marked `proven`). Like
// lgassert it is a recognized PRIMITIVE, not a real sub-graph, so LEC (which
// compares data outputs) is unaffected.
inline constexpr std::string_view fproperty_module_name = "fproperty";

// Name of the phase register pass.single_edge synthesizes when it slots a design
// into P > 1 sub-steps (2f-latch M8). Chosen to survive canon_flop_name (no '$'
// decoration, no "___ssa_", no '.'), and NEVER derived from a nid, so both sides
// of a miter spell it identically and it lands in shared_inputs instead of the
// per-side fallback.
inline constexpr std::string_view single_edge_phase_name = "single_edge_phase";

// True for a flop STATE KEY that names the M8 phase register. The BMC engine
// deliberately discards flop `initial` values under phase=after_reset (a free
// initial state is the sound over-approximation), but a FREE phase would let the
// solver choose the wrong sub-step parity, so the period-boundary guard would
// check mid-period and refute an equivalent design. The phase divider is
// TOOL-SYNTHESIZED with a known concrete start, so it keeps its init — which is
// also what makes every slot predicate const-fold at unroll step j.
[[nodiscard]] inline bool is_single_edge_phase_key(std::string_view key) {
  return key.find(single_edge_phase_name) != std::string_view::npos;
}

// pass/formal obligation-kind codes stored in the proven / runtime_check attrs.
inline constexpr uint32_t kFormalOnehot       = 1;
inline constexpr uint32_t kFormalAssert       = 2;
inline constexpr uint32_t kFormalAssume       = 3;
inline constexpr uint32_t kFormalAssertAlways = 4;
// An input assume discharged by the HIERARCHY: proven at every occurrence this
// design has, under the parents' actual bindings. That is a proof about the
// design, not about the child def alone, so cgen elides the runtime check but
// pass/lec must NOT seed a hypothesis from it when the child is compared on its
// own (see pass/lec/encode.cpp).
inline constexpr uint32_t kFormalAssumeHier   = 5;

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
// serialized payload attached as `livehd::attrs::pin_const_value`. HHDS owns
// the graph-local reverse index and append cursor, so the same Dlop value
// resolves to the same pin without a parallel LiveHD registry.

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
// values use HHDS Graph::intern_constant to reuse an existing pin if the same
// serialized Dlop is already materialised; otherwise append a fresh pid
// (>= 32) and attach the serialized payload.
[[nodiscard]] hhds::Pin_class create_const(hhds::Graph& g, const Dlop& value);

// Decodes the Dlop a const pin represents. Handles both new CONST_NODE-pin
// form (pid encoding for small ints, `pin_const_value` attribute otherwise)
// and the legacy `Ntype_op::Nconst` regular-node form (with the per-node
// `const_value` attribute) that the lgraph wrapper still produces.
[[nodiscard]] Dlop        hydrate_const(const hhds::Pin_class& pin);
[[nodiscard]] Dlop        hydrate_const(const hhds::Node_class& node);
[[nodiscard]] inline Dlop hydrate_const(const hhds::Occurrence_pin& pin) { return hydrate_const(pin.base_pin()); }
[[nodiscard]] inline Dlop hydrate_const(const hhds::Occurrence_node& node) { return hydrate_const(node.base_node()); }

// LITERAL PAYLOAD WIDTH of a constant: the bits a graph pin has to carry to
// hold it, which is NOT Dlop::get_bits().
//
// get_bits() is a SIGNED carrier width, so every NON-NEGATIVE value reports one
// leading zero beyond its payload (`1` -> 2, `255` -> 9, `0ub?` -> 2). Graph
// width hints are literal unsigned payloads, so that headroom must not widen a
// Mux/Hotmux arm, an enclosing Concat lane, or a lossless-carrier requirement.
// An UNKNOWN plane does not change this: `0ub?` occupies exactly its declared
// payload bits. A negative (or non-numeric) value keeps the full carrier --
// its top bit is the sign, not headroom.
//
// ONE definition on purpose: upass/tolg stamps a merge with it, pass/cprop
// enforces carriers with it, and inou/cgen/cgen_sim rejects a narrow mux with
// it. Three private copies drifted -- cgen_sim demanded the carrier for an
// unknown literal that tolg had already stamped at the payload, and the
// resulting `mux-width-loss` fatal fired on a graph that was correct.
[[nodiscard]] inline int32_t literal_payload_bits(const Dlop& value) {
  const auto carrier = static_cast<int32_t>(value.get_bits());
  if (value.is_numeric() && !value.is_negative()) {
    return std::max<int32_t>(1, carrier - 1);
  }
  return std::max<int32_t>(1, carrier);
}

// HHDS stores the user-supplied type in NodeEntry::type with the low bit
// reserved for HHDS's own `is_loop_last` semantics. The Ntype_op encoding
// is designed so that bit 0 of the underlying value already encodes
// is_loop_last (see cell.hpp), so the stored HHDS type round-trips
// directly into an Ntype_op without any shift. Returns Ntype_op::Invalid
// (== 0) for nodes that were never typed.
//
// Gotcha: `Graph::set_subnode` RE-STAMPS the raw type to HHDS's
// own 2/3 loop-last-hint encoding (graph.cpp), silently overwriting the
// Ntype_op::Sub a caller stored — and 2 collides with Ntype_op::Sum. The
// authoritative Sub discriminator is therefore the subnode LINK, not the
// stored type. Every Sub on every path (yosys + Pyrope tolg) goes through
// set_subnode, so this check is complete.
[[nodiscard]] inline Ntype_op type_op_of(const hhds::Node_class& node) {
  if (node.get_subnode_gid() != hhds::Gid_invalid) {
    return Ntype_op::Sub;
  }
  return static_cast<Ntype_op>(static_cast<uint16_t>(node.get_type()));
}

[[nodiscard]] inline Ntype_op type_op_of(const hhds::Occurrence_node& node) { return type_op_of(node.base_node()); }

[[nodiscard]] inline bool is_type_flop(const hhds::Node_class& node) {
  auto op = type_op_of(node);
  return op == Ntype_op::Flop || op == Ntype_op::Fflop;
}
[[nodiscard]] inline bool is_type_flop(const hhds::Occurrence_node& node) { return is_type_flop(node.base_node()); }

[[nodiscard]] inline bool is_type_register(const hhds::Node_class& node) {
  auto op = type_op_of(node);
  return op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Memory || op == Ntype_op::Fflop;
}
[[nodiscard]] inline bool is_type_register(const hhds::Occurrence_node& node) { return is_type_register(node.base_node()); }

[[nodiscard]] inline bool is_type_const(const hhds::Node_class& node) {
  // Constants live on the CONST_NODE singleton. A driver pin attached to
  // CONST_NODE is a constant pin (Graph::create_constant() returns one).
  // get_debug_nid returns the raw Nid for this node handle.
  return node.get_debug_nid() == hhds::Graph::CONST_NODE;
}
[[nodiscard]] inline bool is_type_const(const hhds::Occurrence_node& node) { return is_type_const(node.base_node()); }

[[nodiscard]] inline bool is_type_sub(const hhds::Node_class& node) { return type_op_of(node) == Ntype_op::Sub; }
[[nodiscard]] inline bool is_type_sub(const hhds::Occurrence_node& node) { return type_op_of(node) == Ntype_op::Sub; }

// Per-pin bit width. `livehd::attrs::bits` holds the value (0 == unspecified).
// For graph-IO pins, the declared bits live on `GraphIO::get_bits(name)`
// instead — caller decides which path applies.
[[nodiscard]] inline int32_t bits_of(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return 0;
  }
  auto a = pin.attr(livehd::attrs::bits);
  return a.has() ? static_cast<int32_t>(a.get()) : int32_t{0};
}

[[nodiscard]] inline int32_t bits_of(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view io_name) {
  if (auto b = bits_of(pin); b != 0) {
    return b;
  }
  return static_cast<int32_t>(gio.get_bits(io_name));
}

// Per-pin sign hint. Default is unsigned; an explicit pin_signed attr marks
// signed pins.
[[nodiscard]] inline bool is_unsign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  return !pin.attr(livehd::attrs::pin_signed).has();
}
[[nodiscard]] inline bool is_unsign(const hhds::Occurrence_pin& pin) { return is_unsign(pin.base_pin()); }

[[nodiscard]] inline int32_t bits_of(const hhds::Occurrence_pin& pin) { return bits_of(pin.base_pin()); }
[[nodiscard]] inline int32_t bits_of(const hhds::Occurrence_pin& pin, const hhds::GraphIO& gio, std::string_view io_name) {
  return bits_of(pin.base_pin(), gio, io_name);
}

// Per-node color taint (set by pass diagnostics).
[[nodiscard]] inline bool has_color(const hhds::Node_class& node) { return node.attr(livehd::attrs::color).has(); }

[[nodiscard]] inline int32_t color_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::color);
  return a.has() ? a.get() : 0;
}

// Pin / node classification by master node id (no attr lookup).
[[nodiscard]] inline bool is_graph_input_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto master = pin.get_master_node();
  return master.get_debug_nid() == hhds::Graph::INPUT_NODE;
}
[[nodiscard]] inline bool is_graph_input_pin(const hhds::Occurrence_pin& pin) { return is_graph_input_pin(pin.base_pin()); }

[[nodiscard]] inline bool is_graph_output_pin(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  auto master = pin.get_master_node();
  return master.get_debug_nid() == hhds::Graph::OUTPUT_NODE;
}
[[nodiscard]] inline bool is_graph_output_pin(const hhds::Occurrence_pin& pin) { return is_graph_output_pin(pin.base_pin()); }

// Per-pin / per-node user-assigned name. Returns empty string when no name
// attribute is present — EXCEPT for graph-IO pins, whose port name lives on
// the graph's IO maps (GraphIO decls), not in the pin_name attr: those resolve
// through hhds Graph::pin_name and never come back empty (asserted). An
// explicit pin_name attr still wins, as a caller-stamped override.
[[nodiscard]] inline std::string_view pin_name_of(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return {};
  }
  auto a = pin.attr(livehd::attrs::pin_name);
  if (a.has()) {
    return std::string_view{a.get()};
  }
  if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
    auto n = pin.get_pin_name();
    I(!n.empty(), "graph-IO pin has no declared port name (GraphIO decls out of sync with graph body)");
    return n;
  }
  return {};
}
[[nodiscard]] inline std::string_view pin_name_of(const hhds::Occurrence_pin& pin) { return pin_name_of(pin.base_pin()); }

[[nodiscard]] inline std::string_view node_name_of(const hhds::Node_class& node) {
  if (node.is_invalid()) {
    return {};
  }
  auto a = node.attr(hhds::attrs::name);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
}
[[nodiscard]] inline std::string_view node_name_of(const hhds::Occurrence_node& node) { return node_name_of(node.base_node()); }

[[nodiscard]] inline bool has_name(const hhds::Node_class& node) { return node.attr(hhds::attrs::name).has(); }

// True for a materialized property MARKER instance: the `fproperty` Sub a user
// `assert`/`assume` becomes, or the `lgassert` Sub a runtime `a#[lo..=hi]` guard
// emits. Both are recognized PRIMITIVES -- no sub-graph body, no Liberty cell --
// so every pass that walks real HARDWARE has to know they are neither. Templated
// over the node kind because the hierarchical walks (pass.opentimer) see
// occurrences while the flat ones see base nodes.
template <typename Node_like>
[[nodiscard]] inline bool is_property_marker(const Node_like& n) {
  if (const auto sio = n.get_subnode_io(); sio) {
    const auto nm = sio->get_name();
    return nm == fproperty_module_name || nm == lgassert_module_name;
  }
  // No subnode binding left -- and then it is not even an `Ntype_op::Sub` any
  // more, since type_op_of derives that from the subnode gid. A marker still
  // identifies itself by the payload it
  // packs into its NAME attr -- "<kind>\x1f<loc>\x1f<msg>" for fproperty,
  // "<loc>\x1f<node>" for lgassert -- which is the same signature
  // Sub_inliner/Flattener use to know they must not prefix that name. \x1f is
  // not a legal identifier character, so a genuine body-less black box (external
  // IP, a Liberty cell) can never collide with it, and such a box must keep
  // reaching whatever refusal its consumer has.
  return node_name_of(n).find('\x1f') != std::string_view::npos;
}

// Serialized const value (for Ntype_op::Nconst nodes). Empty if absent.
[[nodiscard]] inline std::string_view const_value_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::const_value);
  return a.has() ? std::string_view{a.get()} : std::string_view{};
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
[[nodiscard]] inline bool is_const_pin(const hhds::Occurrence_pin& pin) { return is_const_pin(pin.base_pin()); }

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

[[nodiscard]] inline std::string debug_name(const hhds::Occurrence_node& node) { return debug_name(node.base_node()); }

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
  // Graph-IO pins never reach this fallback: pin_name_of resolves their declared
  // port name above. For internal pins we generate a synthetic name from the
  // master node + port_id.
  auto base    = default_instance_name(master);
  auto port_id = pin.get_port_id();
  if (port_id == 0) {
    return base;
  }
  return base + "_" + std::to_string(static_cast<uint32_t>(port_id));
}

[[nodiscard]] inline std::string wire_name(const hhds::Occurrence_pin& pin) { return wire_name(pin.base_pin()); }

[[nodiscard]] inline hhds::Pin_class get_driver_of_sink_name(const hhds::Node_class& node, std::string_view name);
[[nodiscard]] inline int32_t         concat_total_width(const hhds::Node_class& node);

// ---------------------------------------------------------------------------
// Debug-only attribute self-checks (-c dbg). Tier 1: a constant driver pin must
// be consistent with the bits/sign attributes stamped on it.
// ---------------------------------------------------------------------------
//
// The per-pin `bits`/`pin_signed` attributes are an optimisation/codegen hint,
// but if a front-end translation leaves a value on a pin whose *explicitly
// declared* width/sign cannot hold it -- e.g. a $mux/concat const imported
// signed so 4'hA reads as -6, or a value with a set bit above the declared
// unsigned width -- the hint is a lie and downstream emit/LEC silently diverges.
// `I(...)` is compiled out under NDEBUG (-c opt); the body is additionally
// guarded so hydrate_const is not even built in a release compile.
//
// IMPORTANT: gated on bits>0 (an *explicit* declared width). Const pins with no
// bits attr default to "unsigned" only by attribute absence, so an ungated sign
// check would false-fire on every legitimately-signed negative literal. A pin
// the front-end deliberately sized carries a real contract worth checking.
inline void debug_check_pin_hint([[maybe_unused]] const hhds::Pin_class& dpin) {
#ifndef NDEBUG
  if (dpin.is_invalid()) {
    return;
  }
  const int32_t nbits = bits_of(dpin);
  if (nbits <= 0) {
    return;  // no explicit declared width -> nothing to violate
  }
  const bool is_uns = is_unsign(dpin);
  if (is_const_pin(dpin)) {
    const auto c = hydrate_const(dpin);
    if (c.has_unknowns() || c.is_nil()) {
      return;  // x/z/nil bits: no concrete value to range-check
    }

    // (1) An unsigned pin must never carry a negative value (its upper bits
    // were not cleared -- a missing get_mask / zero-extend in translation).
    I(!is_uns || !c.is_negative(),
      std::format("unsigned const pin '{}' (declared {} bits) holds a negative value -- missing get_mask/zext?",
                  wire_name(dpin),
                  nbits)
          .c_str());

    // (2) The value must fit the declared literal container. An unsigned b-bit
    // hint represents [0,2^b-1]; a signed one represents
    // [-2^(b-1),2^(b-1)-1].
    const int need = is_uns ? c.get_last_bit_set() + 1 : static_cast<int>(c.get_bits());
    I(need <= nbits,
      std::format("const pin '{}' needs {} bits but is declared {} {}signed bits", wire_name(dpin), need, nbits, is_uns ? "un" : "")
          .c_str());
    return;
  }

  const auto op = type_op_of(dpin.get_master_node());
  if (op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::EQ || op == Ntype_op::Ror || op == Ntype_op::Clock_cell) {
    I(is_uns && nbits == 1,
      std::format("pin '{}' from {} must carry the structural u1 hint, got {} {}signed bits",
                  wire_name(dpin),
                  Ntype::get_name(op),
                  nbits,
                  is_uns ? "un" : "")
          .c_str());
  } else if (op == Ntype_op::Concat) {
    // >= , not ==: an OVER-stamped concat pin is legal and every consumer
    // already models it (encode.cpp widens W to sum(w) and treats the bits
    // above as known zero; abc_map fills `max(out_bits,total)`). Only an
    // UNDER-stamped pin is the miscompile -- it drops the top lanes.
    const int total = concat_total_width(dpin.get_master_node());
    I(total <= 0 || (is_uns && nbits >= total),
      std::format("concat pin '{}' must be an unsigned hint of at least {} bits, got {} {}signed bits",
                  wire_name(dpin),
                  total,
                  nbits,
                  is_uns ? "un" : "")
          .c_str());
  } else if (op == Ntype_op::Get_mask) {
    auto mask = get_driver_of_sink_name(dpin.get_master_node(), "mask");
    if (!mask.is_invalid() && is_const_pin(mask)) {
      const auto mv = hydrate_const(mask);
      if (!mv.is_negative() && !mv.has_unknowns()) {
        const auto capacity = static_cast<int32_t>(mv.popcount());
        I(capacity == 0 || nbits <= capacity,
          std::format("Get_mask pin '{}' selects {} bits but is hinted at {}", wire_name(dpin), capacity, nbits).c_str());
      }
    }
  }
#endif
}

// Compatibility spelling while callers migrate to the general hint checker.
inline void debug_check_const_pin(const hhds::Pin_class& dpin) { debug_check_pin_hint(dpin); }

// Debug-only (-c dbg) invariant: tolg / upass generation must leave every
// value-producing cell with a resolved width. A driver pin still at bits==0 is
// an unbounded cell the front-end failed to size -- a latent bug, since
// cgen/LEC would silently infer a width. Exemptions: consts carry their width
// in their value; multi-driver Sub/Memory/Flop carry it on their typed output
// pins. `where` labels the producing stage in the assertion message. Compiled
// out under NDEBUG (-c opt).
inline void debug_assert_cells_sized([[maybe_unused]] hhds::Graph& g, [[maybe_unused]] std::string_view where) {
#ifndef NDEBUG
  for (auto node : g.body().nodes(hhds::Node_order::forward)) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Invalid || op == Ntype_op::Nconst || Ntype::has_multiple_driver_pins(op)) {
      continue;
    }
    auto dpin = node.create_driver_pin(0);
    if (dpin.is_invalid()) {
      continue;
    }
    debug_check_pin_hint(dpin);
    if (is_const_pin(dpin)) {
      continue;
    }
    if (bits_of(dpin) == 0) {
      std::string inputs;
      for (const auto& edge : node.inp_edges()) {
        if (!inputs.empty()) {
          inputs += ", ";
        }
        inputs += std::format("p{}:{}b", edge.sink.get_port_id(), bits_of(edge.driver));
        if (is_const_pin(edge.driver)) {
          const auto value  = hydrate_const(edge.driver);
          inputs           += std::format(":const({}b,{})", value.get_bits(), value.is_negative() ? "neg" : "nonneg");
        } else if (is_graph_input_pin(edge.driver)) {
          inputs += ":$" + std::string(edge.driver.get_pin_name());
        } else {
          inputs += ":" + std::string(Ntype::get_name(type_op_of(edge.driver.get_master_node())));
        }
      }
      I(false,
        std::format("{} left cell '{}' ({}) in def '{}' at bits==0 -- unbounded width (front-end did not size it); inputs [{}]",
                    where,
                    debug_name(node),
                    Ntype::get_name(op),
                    std::string_view{g.get_name()},
                    inputs)
            .c_str());
    }
  }
#endif
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

// Assert the pin role required by attribute `Tag` (livehd::attrs::attr_kind) at a
// pin-setter call site. The per-pin attr key folds the driver/sink bit (graph.hpp
// Pin_class::attr masks Pid 0x2), so a same-port driver and sink SHARE one slot;
// stamping the wrong role silently aliases the other's value (e.g. a mux's 66-bit
// output width leaking onto its 2-bit select sink). Classifying each attribute
// (attrs.hpp) lets the setter catch that:
//   driver_pin -> the signal source; assert is_driver  (bits, signed, …)
//   sink       -> assert is_sink
//   edge/any   -> no driver/sink restriction (shared wire name, IO offsets, …)
//   node       -> static error: a node attribute must not go through a pin setter.
// Call only on a VALID pin (the setters early-return on invalid first).
template <typename Tag>
inline void assert_pin_attr_role([[maybe_unused]] const hhds::Pin_class& pin) {
  static_assert(livehd::attrs::attr_kind<Tag> != livehd::attrs::Attr_kind::node,
                "node attribute set through a pin setter — use the node overload");
  if constexpr (livehd::attrs::attr_kind<Tag> == livehd::attrs::Attr_kind::driver_pin) {
    I(pin.is_driver(), "this attribute is a DRIVER-pin property; got a sink pin (write it on the driver / read the driver)");
  } else if constexpr (livehd::attrs::attr_kind<Tag> == livehd::attrs::Attr_kind::sink) {
    I(pin.is_sink(), "this attribute is a SINK-pin property; got a driver pin");
  }
}

// Per-pin bit width (plain int32). Storing 0 leaves the attribute untouched
// logically. `bits` is a DRIVER-pin property; a sink's width is its driver's,
// read via `bits_of(driver_of(sink))` (see assert_pin_attr_role above).
inline void set_bits(const hhds::Pin_class& pin, int32_t b) {
  if (pin.is_invalid()) {
    return;
  }
  assert_pin_attr_role<livehd::attrs::bits_t>(pin);
  pin.attr(livehd::attrs::bits).set(b);
}

// Per-pin sign hint. Presence means signed; absence means unsigned. Like `bits`,
// `signed` is a DRIVER-pin property (a sink's sign is its driver's), enforced via
// assert_pin_attr_role<pin_signed_t>.
inline void set_unsign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  assert_pin_attr_role<livehd::attrs::pin_signed_t>(pin);
  pin.attr(livehd::attrs::pin_signed).del();
}

inline void set_sign(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  assert_pin_attr_role<livehd::attrs::pin_signed_t>(pin);
  pin.attr(livehd::attrs::pin_signed).set(livehd::attrs::pin_signed_t::value_type{});
}

// Set one complete realization hint. `bits` is always the literal physical
// container width: unsigned b bits represent [0,2^b-1], signed b bits represent
// [-2^(b-1),2^(b-1)-1]. Prefer these over separate set_bits/set_* calls when a
// producer knows both facts.
inline void set_ubits(const hhds::Pin_class& pin, int32_t bits) {
  set_bits(pin, bits);
  set_unsign(pin);
}

inline void set_sbits(const hhds::Pin_class& pin, int32_t bits) {
  set_bits(pin, bits);
  set_sign(pin);
}

[[nodiscard]] inline int32_t real_width(const hhds::Pin_class& pin) { return bits_of(pin); }
[[nodiscard]] inline int32_t real_width(const hhds::Occurrence_pin& pin) { return bits_of(pin); }
[[nodiscard]] inline int32_t real_width(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view io_name) {
  return bits_of(pin, gio, io_name);
}
[[nodiscard]] inline int32_t real_width(const hhds::Occurrence_pin& pin, const hhds::GraphIO& gio, std::string_view io_name) {
  return bits_of(pin, gio, io_name);
}

// Per-node color taint.
inline void set_color(const hhds::Node_class& node, int32_t c) { node.attr(livehd::attrs::color).set(c); }

// Clear the flat per-node color (the old Node::del_color).
inline void del_color(const hhds::Node_class& node) {
  if (node.attr(livehd::attrs::color).has()) {
    node.attr(livehd::attrs::color).del();
  }
}

// Per-hierarchical-instance color (pass/color hier_color mode). Only valid in
// hier traversal context (the attr resolves through the node's hier_pos).
[[nodiscard]] inline bool    has_hier_color(const hhds::Node_class& node) { return node.attr(livehd::attrs::hier_color).has(); }
[[nodiscard]] inline int32_t hier_color_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::hier_color);
  return a.has() ? a.get() : 0;
}
inline void set_hier_color(const hhds::Node_class& node, int32_t c) { node.attr(livehd::attrs::hier_color).set(c); }
inline void del_hier_color(const hhds::Node_class& node) {
  if (node.attr(livehd::attrs::hier_color).has()) {
    node.attr(livehd::attrs::hier_color).del();
  }
}
[[nodiscard]] inline bool has_hier_color(const hhds::Occurrence_node& node) { return node.attr(livehd::attrs::hier_color).has(); }
[[nodiscard]] inline int32_t hier_color_of(const hhds::Occurrence_node& node) {
  auto a = node.attr(livehd::attrs::hier_color);
  return a.has() ? a.get() : 0;
}
inline void set_hier_color(const hhds::Occurrence_node& node, int32_t c) { node.attr(livehd::attrs::hier_color).set(c); }
inline void del_hier_color(const hhds::Occurrence_node& node) {
  if (node.attr(livehd::attrs::hier_color).has()) {
    node.attr(livehd::attrs::hier_color).del();
  }
}

// Per-node / per-pin structural-correspondence id (pass/semdiff). 0 is a real,
// greppable value meaning "no counterpart" — distinct from the attribute being
// absent. semdiff stamps every node/driver-pin (0 or a shared id) so the diff
// is greppable end to end (`tool grep match=0`).
inline void set_match(const hhds::Node_class& node, uint32_t id) { node.attr(livehd::attrs::match).set(id); }
inline void set_match(const hhds::Pin_class& pin, uint32_t id) {
  if (!pin.is_invalid()) {
    assert_pin_attr_role<livehd::attrs::match_t>(pin);  // pin matches are stamped on driver pins
    pin.attr(livehd::attrs::match).set(id);
  }
}
[[nodiscard]] inline bool     has_match(const hhds::Node_class& node) { return node.attr(livehd::attrs::match).has(); }
[[nodiscard]] inline uint32_t match_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::match);
  return a.has() ? a.get() : 0;
}
[[nodiscard]] inline bool has_match(const hhds::Pin_class& pin) {
  return !pin.is_invalid() && pin.attr(livehd::attrs::match).has();
}
[[nodiscard]] inline uint32_t match_of(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return 0;
  }
  auto a = pin.attr(livehd::attrs::match);
  return a.has() ? a.get() : 0;
}

// Per-node formal-verification status (pass/formal, task 2f-formal). 0 == absent;
// pass.formal sets a small non-zero enum (see Formal_status in pass/formal).
inline void                   set_proven(const hhds::Node_class& node, uint32_t v) { node.attr(livehd::attrs::proven).set(v); }
// Every consumer tests PRESENCE (has_proven / `if (auto a = ...; a.has())`), so
// retracting a proof must DELETE the attribute; set(0) would still read as proven.
inline void                   clear_proven(const hhds::Node_class& node) { node.attr(livehd::attrs::proven).del(); }
[[nodiscard]] inline bool     has_proven(const hhds::Node_class& node) { return node.attr(livehd::attrs::proven).has(); }
[[nodiscard]] inline uint32_t proven_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::proven);
  return a.has() ? a.get() : 0;
}
inline void set_runtime_check(const hhds::Node_class& node, uint32_t v) { node.attr(livehd::attrs::runtime_check).set(v); }
[[nodiscard]] inline bool has_runtime_check(const hhds::Node_class& node) { return node.attr(livehd::attrs::runtime_check).has(); }
[[nodiscard]] inline uint32_t runtime_check_of(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::runtime_check);
  return a.has() ? a.get() : 0;
}

// Unified color reader for pass/color + pass/partition: prefer the per-instance
// hier color when present (hier mode), else fall back to the flat color. A
// return of 0 (NO_COLOR) means uncolored.
[[nodiscard]] inline int32_t node_color_of(const hhds::Node_class& node) {
  if (node.is_hier() && node.attr(livehd::attrs::hier_color).has()) {
    return node.attr(livehd::attrs::hier_color).get();
  }
  return color_of(node);
}
[[nodiscard]] inline bool has_node_color(const hhds::Node_class& node) {
  return (node.is_hier() && node.attr(livehd::attrs::hier_color).has()) || has_color(node);
}
[[nodiscard]] inline int32_t node_color_of(const hhds::Occurrence_node& node) {
  return node.attr(livehd::attrs::hier_color).has() ? node.attr(livehd::attrs::hier_color).get() : color_of(node.base_node());
}
[[nodiscard]] inline bool has_node_color(const hhds::Occurrence_node& node) {
  return node.attr(livehd::attrs::hier_color).has() || has_color(node.base_node());
}

// User-assigned pin name. Empty string clears the attr.
inline void set_pin_name(const hhds::Pin_class& pin, std::string_view name) {
  if (pin.is_invalid()) {
    return;
  }
  assert_pin_attr_role<livehd::attrs::pin_name_t>(pin);  // edge (wire name): no driver/sink restriction
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

[[nodiscard]] inline hhds::Occurrence_pin find_sink_pin(const hhds::Occurrence_node& node, std::string_view name) {
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

// Driver of the input edge with the LOWEST SINK port id — the VALUE operand of
// a single-value wrapper cell (Not / Get_mask / Sext / passthrough Or), skipping
// the mask / amount operands that sit on higher sink pids. The comparison must
// be sink-pid against sink-pid: the once-common inline version compared the
// candidate's sink pid against the current pick's DRIVER pid (a Sub output pid,
// a const node's pid — unrelated numbering), so on a two-operand Get_mask it
// could follow the MASK CONSTANT instead of the value and silently stop a
// clock-cone walk one hop early.
[[nodiscard]] inline hhds::Pin_class first_value_driver(const hhds::Node_class& node) {
  hhds::Pin_class a;
  uint32_t        best = 0;
  for (const auto& e : node.inp_edges()) {
    const auto sp = static_cast<uint32_t>(e.sink.get_port_id());
    if (a.is_invalid() || sp < best) {
      a    = e.driver;
      best = sp;
    }
  }
  return a;
}

[[nodiscard]] inline hhds::Occurrence_pin first_value_driver(const hhds::Occurrence_node& node) {
  hhds::Occurrence_pin a;
  uint32_t             best = 0;
  for (const auto& e : node.inp_edges()) {
    const auto sp = static_cast<uint32_t>(e.sink.get_port_id());
    if (a.is_invalid() || sp < best) {
      a    = e.driver;
      best = sp;
    }
  }
  return a;
}

// Returns the (single) driver pin feeding a named sink. If the sink is
// unconnected, returns an invalid pin. The multi-driver sinks (those where
// Ntype::is_sink_single_driver is false -- the 's'-suffixed "as"/"bs" pins)
// require inp_drivers_of instead.
[[nodiscard]] inline hhds::Pin_class get_driver_of_sink_name(const hhds::Node_class& node, std::string_view name) {
  auto sink = find_sink_pin(node, name);
  if (sink.is_invalid()) {
    return {};
  }
  // get_driver_pins() is the direct sink-fan-in accessor (no Edge_class vector
  // materialized) — a sink's fan-in is small, usually a single driver.
  auto drivers = sink.get_driver_pins();
  if (drivers.empty()) {
    return {};
  }
  // Single-driver contract: a multi-driver sink (the "as"/"bs" pins -- Sum as/bs,
  // Or as, SHL bs, ...) must read every driver via inp_drivers_of — silently
  // taking the first would drop fan-in. Assert callers honor it.
  I(drivers.size() == 1, "get_driver_of_sink_name on a multi-driver sink; use inp_drivers_of");
  return drivers.front();
}

[[nodiscard]] inline hhds::Occurrence_pin get_driver_of_sink_name(const hhds::Occurrence_node& node, std::string_view name) {
  auto sink = find_sink_pin(node, name);
  if (sink.is_invalid()) {
    return {};
  }
  auto drivers = sink.get_driver_pins();
  if (drivers.empty()) {
    return {};
  }
  I(drivers.size() == 1, "get_driver_of_sink_name on a multi-driver sink; use inp_drivers_of");
  return drivers.front();
}

// Cell-type mutation. The Ntype_op encoding values already bake in the
// is_loop_last low bit (see cell.hpp), so storing the raw value into HHDS
// round-trips correctly with type_op_of(). Do NOT shift.
inline void set_type_op(const hhds::Node_class& node, Ntype_op op) {
  node.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(op)));
}

// Set this node to a constant node carrying the given serialized Dlop.
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
[[nodiscard]] inline hhds::Node_class create_typed_node(hhds::Graph& graph, Ntype_op op, int32_t bits) {
  auto node = graph.create_node();
  set_type_op(node, op);
  if (bits != 0) {
    auto dpin = node.create_driver_pin(0);
    dpin.attr(livehd::attrs::bits).set(bits);
  }
  return node;
}

// Per-pin offset (used by Get_mask / Set_mask / Sext positional ops, and IO-port
// wire offsets on either end). any_pin: no driver/sink restriction.
inline void set_pin_offset(const hhds::Pin_class& pin, int32_t off) {
  if (pin.is_invalid()) {
    return;
  }
  assert_pin_attr_role<livehd::attrs::pin_offset_t>(pin);
  if (off == 0) {
    pin.attr(livehd::attrs::pin_offset).del();
  } else {
    pin.attr(livehd::attrs::pin_offset).set(off);
  }
}

// Per-node source provenance is hhds::attrs::srcid, minted through the
// graph's Source_locator; the old set_source/set_loc1 string+line
// helpers are gone.

// All drivers feeding a named sink port. Used by passes (memory, bit_or)
// that allow multiple drivers on the same sink port_id.
[[nodiscard]] inline std::vector<hhds::Pin_class> inp_drivers_of(const hhds::Node_class& node, std::string_view name) {
  std::vector<hhds::Pin_class> result;
  if (node.is_invalid()) {
    return result;
  }
  // Go straight to the named sink's fan-in (get_driver_pins) instead of scanning
  // every input edge of the node and filtering by port_id — a sink's fan-in is
  // materialized directly, with no Edge_class vector for the other ports.
  auto sink = find_sink_pin(node, name);
  if (sink.is_invalid()) {
    return result;
  }
  auto drivers = sink.get_driver_pins();
  result.assign(drivers.begin(), drivers.end());
  return result;
}

// ---------------------------------------------------------------------------
// Design-size estimation (memory-admission size gate).
// ---------------------------------------------------------------------------
//
// A flattened design of more than a few million nodes is where synthesis (ABC)
// and formal (LEC) runs start exhausting host memory. These helpers give a
// DETERMINISTIC, host-independent node count so a pass can warn (color) or refuse
// with a named override flag (abc/lec) BEFORE it materializes anything large --
// unlike an RSS/footprint sample, which only catches the blow-up mid-flight.

// "Very large flattened design" threshold, in nodes. Default ~1M: below this,
// whole-design ABC/LEC is routinely fine; well above it, a flat run risks
// hundreds of GB (a flat XSCore run reached 221 GB). Overridable via the env var
// LIVEHD_LARGE_DESIGN_NODES (0 disables the gate; also lets a test exercise the
// refusal on a small design). Read once per pass, not per node.
inline constexpr uint64_t large_design_nodes_default = 1'000'000;

[[nodiscard]] inline uint64_t large_design_node_threshold() {
  if (const char* env = std::getenv("LIVEHD_LARGE_DESIGN_NODES"); env != nullptr && *env != '\0') {
    char*                    end = nullptr;
    const unsigned long long v   = std::strtoull(env, &end, 10);
    if (end != env && *end == '\0') {
      // 0 disables the gate; the count can never exceed UINT64_MAX.
      return v == 0 ? UINT64_MAX : static_cast<uint64_t>(v);
    }
  }
  return large_design_nodes_default;
}

// Node count of ONE def body (class context: this body's own nodes, no descent
// into sub-instances). O(nodes-in-body) time, O(1) space -- a plain lazy walk,
// never a materialized copy.
[[nodiscard]] inline uint64_t body_node_count(const hhds::Graph* g) {
  if (g == nullptr) {
    return 0;
  }
  uint64_t n = 0;
  for ([[maybe_unused]] auto node : const_cast<hhds::Graph*>(g)->body().nodes()) {
    ++n;
  }
  return n;
}

// Flattened node count of `top`'s instance hierarchy: sum over the structure tree
// of each def body's own node count (each unique def counted once, then
// multiplied by how many times it is instantiated). `resolve(gid)` maps a
// subnode's target gid to its Graph* (or nullptr if not in the library; such
// instances are skipped -- the count is then a lower bound).
//
// Cost: grouped_hierarchy().instances() walks the STRUCTURE TREE only (proportional to the instance
// count, not the flat node count), and each unique def body is counted once via
// body_node_count. So this is O(unique-nodes + instances), O(unique-defs) space
// -- it never materializes the flattened walk (which is exactly the O(flat-nodes)
// allocation this gate exists to keep the caller from doing).
template <typename Resolve>
[[nodiscard]] uint64_t flat_node_count(hhds::Graph* top, Resolve&& resolve) {
  if (top == nullptr) {
    return 0;
  }
  std::unordered_map<hhds::Gid, uint64_t> per_def;  // memoize: a def may recur
  auto                                    count_def = [&](hhds::Gid gid, hhds::Graph* g) -> uint64_t {
    auto it = per_def.find(gid);
    if (it != per_def.end()) {
      return it->second;
    }
    uint64_t c = body_node_count(g);
    per_def.emplace(gid, c);
    return c;
  };

  uint64_t total = count_def(top->get_gid(), top);  // the top is instantiated once
  for (auto inst : top->grouped_hierarchy().instances()) {
    hhds::Gid gid = inst.get_target_gid();
    if (hhds::Graph* g = resolve(gid); g != nullptr) {
      total += count_def(gid, g);
    }
  }
  return total;
}

// Like flat_node_count, but does NOT descend into a sub whose resolved def the
// caller marks opaque (`is_opaque(child) == true`) -- that instance is a leaf,
// contributing only its own Sub node. This mirrors what a hierarchical LEC encode
// materializes: already-proven children are black-boxed (lec.collapse), so a
// whole-design count would over-refuse a design that is actually checked in small
// opacity-bounded pieces. Direct-child recursion (not hier_range, which cannot
// prune a subtree), memoized per def gid -- the global opacity set makes each
// def's pruned count well defined. O(reachable-non-opaque nodes), O(defs) space.
template <typename Resolve, typename IsOpaque>
[[nodiscard]] uint64_t flat_node_count_pruned(hhds::Graph* top, Resolve&& resolve, IsOpaque&& is_opaque) {
  if (top == nullptr) {
    return 0;
  }
  std::unordered_map<hhds::Gid, uint64_t> memo;
  auto                                    count = [&](auto&& self, hhds::Graph* g) -> uint64_t {
    if (auto it = memo.find(g->get_gid()); it != memo.end()) {
      return it->second;  // 0 while in progress = cycle guard (hier is a DAG; a back-edge undercounts, never loops)
    }
    memo.emplace(g->get_gid(), 0);
    uint64_t total = 0;
    for (auto node : g->body().nodes()) {
      ++total;  // every node counts, Sub nodes included
      if (type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      hhds::Graph* child = resolve(node.get_subnode_gid());
      if (child != nullptr && !is_opaque(child)) {
        total += self(self, child);
      }
    }
    memo[g->get_gid()] = total;
    return total;
  };
  return count(count, top);
}

// ---------------------------------------------------------------------------
// Gate-equivalent (GE) weight.
// ---------------------------------------------------------------------------
//
// A node is not a unit of work. A 1-bit And and a 512-bit And are one LGraph
// node each, and 1 versus 512 AIG nodes once ABC bit-blasts them -- so a
// 200k-node region of wide datapath sails through any node-count gate and still
// exhausts the host. Anything sizing a region against a memory budget therefore
// weighs nodes in GATE EQUIVALENTS: one GE is roughly one bit-blasted gate
// (the `min=`/`max=` window of pass.color, todo/livehd/2c-color-size.html R1).
//
// The model:
//   * bitwise / mux / shift / Sext / Get_mask / Set_mask / Sum, and the flops
//                            -> driver width: one gate per output bit
//   * Mult                   -> width^2: an array multiplier is quadratic
//   * comparators and reduces (LT/GT/EQ/Ror) -> widest OPERAND, not the driver.
//     They answer in one bit but pay per input bit; sizing a 64-bit compare at
//     1 GE is exactly the undercount this metric exists to prevent.
//   * blackboxes ABC never opens (Div, Sub, a native Memory)
//                            -> sum of PORT widths. The boundary is all ABC
//                               sees of them, and it is what the region carries.
//   * constants and graph IO -> 0. Neither maps to a gate.
//
// UNKNOWN WIDTH (bits == 0: pass.color running before pass.bitwidth, or a
// driver nobody reads) degrades to 1 -- the same degradation Color_synth::is_cut
// takes. Note the asymmetry: for `is_cut` an unknown width only ever grows a
// region ("larger, never wrong"), while here it UNDERSTATES one. A GE window is
// a best-effort fence; the sampled-RSS admission inside pass.abc stays the
// backstop that actually holds.
namespace ge_detail {

// Width of the node's primary output. Driver pin 0 ("node-as-pin") is the
// single-output shape that dominates the graph; create_driver_pin(0) is pure
// handle arithmetic (no allocation, no mutation) and, unlike an out-edge scan,
// still reports the width of a driver that nobody reads.
[[nodiscard]] inline uint64_t out_width(const hhds::Node_class& node) {
  const auto b = bits_of(node.create_driver_pin(0));
  return b <= 0 ? 0 : static_cast<uint64_t>(b);
}

// Width of the widest driver feeding any of `node`'s sinks -- the operand width
// of a comparator/reduce, whose own output is a single bit.
[[nodiscard]] inline uint64_t widest_operand(const hhds::Node_class& node) {
  uint64_t w = 0;
  for (const auto& e : node.inp_edges()) {
    const auto b = bits_of(e.driver);
    if (b > 0 && static_cast<uint64_t>(b) > w) {
      w = static_cast<uint64_t>(b);
    }
  }
  return w;
}

// Port width across a blackbox boundary: every distinct driver pin, plus every
// distinct sink pin that a non-constant drives. Deduping by port id is
// load-bearing -- out_edges()/inp_edges() yield one entry per EDGE, so a driver
// with three readers would otherwise be counted three times. Constant-driven
// sinks are skipped: on a Memory those are the comptime `bits`/`size`/`rdport`
// configuration pins rather than ports, and a tied input costs no boundary gate
// anywhere (ABC folds it).
[[nodiscard]] inline uint64_t port_bits_sum(const hhds::Node_class& node) {
  std::vector<uint32_t> seen;
  uint64_t              sum  = 0;
  auto                  once = [&](uint32_t pid, const hhds::Pin_class& width_pin) {
    if (std::find(seen.begin(), seen.end(), pid) != seen.end()) {
      return;
    }
    seen.emplace_back(pid);
    if (const auto b = bits_of(width_pin); b > 0) {
      sum += static_cast<uint64_t>(b);
    }
  };
  for (const auto& e : node.out_edges()) {
    once(static_cast<uint32_t>(e.driver.get_port_id()), e.driver);
  }
  seen.clear();  // driver and sink port ids live in separate spaces
  for (const auto& e : node.inp_edges()) {
    if (is_const_pin(e.driver)) {
      continue;
    }
    once(static_cast<uint32_t>(e.sink.get_port_id()), e.driver);  // a sink's width is its driver's
  }
  return sum;
}

// Declared port width of a sub-instance. Read off the child's GraphIO decls, not
// its edges: the decl carries `bits` even when the body was never materialized
// and when the parent left a port unconnected.
[[nodiscard]] inline uint64_t sub_port_bits(const hhds::Node_class& node) {
  auto io = node.get_subnode_io();
  if (!io) {
    return 0;
  }
  uint64_t sum = 0;
  for (const auto& d : io->get_input_pin_decls()) {
    sum += d.bits;
  }
  for (const auto& d : io->get_output_pin_decls()) {
    sum += d.bits;
  }
  return sum;
}

}  // namespace ge_detail

// ── Ntype_op::Concat decoding ────────────────────────────────────────────────
//
// A Concat's sinks are INTERLEAVED (value, declared-width) pairs, MSB-FIRST:
// lane i occupies sink pids 2i (the value driver) and 2i+1 (an Nconst holding
// that lane's width in bits). Every consumer must read the lane table through
// this ONE decoder rather than walking inp_edges itself -- the whole reason the
// cell exists is that lane widths are NOT recoverable from the drivers, and a
// consumer that re-derives them from `bits_of` or from the value's significant
// bits silently shifts every lane above the one it got wrong.
struct Concat_lane {
  hhds::Pin_class value;
  int32_t         width{0};   // DECLARED window width, always > 0
  int32_t         offset{0};  // LSB position of this lane in the result
};

// Lanes MSB-first (index 0 is the most significant, matching Verilog `{a,b,c}`
// and hlop's `concat_op`), each with its resolved LSB offset. Returns an empty
// vector for a malformed cell (missing/odd pin, non-const or non-positive
// width) -- callers must treat empty as "cannot lower" and fail closed, never
// as "zero lanes".
[[nodiscard]] inline std::vector<Concat_lane> concat_lanes(const hhds::Node_class& node) {
  std::vector<Concat_lane> lanes;
  if (node.is_invalid() || type_op_of(node) != Ntype_op::Concat) {
    return lanes;
  }
  // Gather by pid first: inp_edges order is unspecified, and here the pid IS
  // the lane index.
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> by_pid;
  hhds::Port_id                                       max_pid = 0;
  for (const auto& ie : node.inp_edges()) {
    const auto pid = ie.sink.get_port_id();
    by_pid.emplace(pid, ie.driver);
    max_pid = std::max(max_pid, pid);
  }
  if (by_pid.empty() || (max_pid % 2) == 0) {
    return {};  // a trailing value with no width operand
  }
  const size_t n_lanes = static_cast<size_t>(max_pid + 1) / 2;
  lanes.reserve(n_lanes);
  for (size_t i = 0; i < n_lanes; ++i) {
    auto vit = by_pid.find(static_cast<hhds::Port_id>(2 * i));
    auto wit = by_pid.find(static_cast<hhds::Port_id>(2 * i + 1));
    if (vit == by_pid.end() || wit == by_pid.end() || !is_const_pin(wit->second)) {
      return {};
    }
    const auto wv = hydrate_const(wit->second);
    if (!wv.is_just_i64()) {
      return {};
    }
    const auto w = wv.to_just_i64();
    if (w <= 0 || w > std::numeric_limits<int32_t>::max()) {
      return {};
    }
    lanes.push_back(Concat_lane{vit->second, static_cast<int32_t>(w), 0});
  }
  // MSB-first: lane 0 sits above every lane after it, so offsets accumulate
  // from the tail.
  int32_t off = 0;
  for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
    it->offset  = off;
    off        += it->width;
  }
  return lanes;
}

// Sum of the lane widths, i.e. the literal unsigned width of the result.
// 0 when the cell is malformed (an EMPTY table, never "zero lanes").
//
// concat_lanes() already resolved every offset MSB-first, so the top lane's
// window ends exactly at the total -- no second walk. Callers that already hold
// the decoded table MUST use this overload: the node overload pays another full
// inp_edges walk plus a hash map and a vector allocation.
[[nodiscard]] inline int32_t concat_total_width(const std::vector<Concat_lane>& lanes) {
  return lanes.empty() ? int32_t{0} : lanes.front().offset + lanes.front().width;
}

// ── The Concat lane contract ────────────────────────────────────────────────
//
// A Concat's output width is ALWAYS sum(declared lane widths). The interleaved
// const sinks carry the INTENDED bit spacing, and no consumer may re-derive the
// width from the driver pin's stamped `bits` -- `concat(a,8,b,4)` is 12 bits
// even after the optimizer proves `a` fits in 3.
//
// That is the whole point of the const sinks: LGraph passes are free to narrow
// the REAL width of a variable, while the concat keeps the spacing the source
// asked for. So a lane driver may occupy FEWER bits than its window, and is
// SIGN-EXTENDED up to it (LiveHD values are signed-canonical, so extension is
// always sign-aware: a 1-bit signed -1 widens to 0b111 in a 3-bit window,
// while an unsigned driver widens with 0s).
// A driver occupying MORE bits than its window is an INTERNAL COMPILE ERROR --
// nothing may produce it, and truncating it silently shifts every lane above.
//
// Logical field width a driver occupies. The realization-hint contract uses
// literal widths for both signed and unsigned pins, exactly like a lane window.
// Returns 0 for an UNSTAMPED pin -- "unknown", which is never a violation.
[[nodiscard]] inline int32_t lane_value_bits(const hhds::Pin_class& pin) {
  const auto b = bits_of(pin);
  if (b <= 0) {
    return 0;  // unstamped: unknown, not a violation
  }
  return b;
}

// True iff this lane's driver fits its declared window (the contract above).
[[nodiscard]] inline bool concat_lane_fits(const Concat_lane& l) { return lane_value_bits(l.value) <= l.width; }

// Empty when every lane fits. Otherwise a diagnostic naming the first offending
// lane, for the caller's own fatal path (`I(msg.empty(), msg.c_str())`, a pass
// `error_at`, …). Over-wide is a compiler bug, so the message names the pin.
[[nodiscard]] inline std::string concat_lane_violation(const std::vector<Concat_lane>& lanes) {
  for (size_t i = 0; i < lanes.size(); ++i) {
    const auto& l = lanes[i];
    if (concat_lane_fits(l)) {
      continue;
    }
    return std::format(
        "internal: Concat lane {} (of {}, MSB-first) driver '{}' occupies {} bits but its declared window is {} -- a lane "
        "driver may only be NARROWER than its window (LGraph may narrow, never widen); truncating it would shift every "
        "lane above it",
        i,
        lanes.size(),
        wire_name(l.value),
        lane_value_bits(l.value),
        l.width);
  }
  return {};
}

[[nodiscard]] inline int32_t concat_total_width(const hhds::Node_class& node) { return concat_total_width(concat_lanes(node)); }

// Gate-equivalent weight of one node. See the model above. Never returns 0 for a
// node that maps to logic -- an unknown width floors at 1, so a region's GE is
// always at least its partitionable node count.
[[nodiscard]] inline uint64_t ge_weight(const hhds::Node_class& node) {
  if (node.is_invalid() || is_builtin_node(node)) {
    return 0;
  }
  const auto atleast1 = [](uint64_t w) -> uint64_t { return w == 0 ? 1 : w; };

  switch (type_op_of(node)) {
    case Ntype_op::Invalid:
    case Ntype_op::IO:
    // A Concat is pure wiring: it renames bit positions and mints no gate. Its
    // out_width is the SUM of its lanes, so the default arm would charge the
    // whole assembled bus as logic and make any packing-heavy region look
    // enormous to the size windows.
    case Ntype_op::Concat:
    case Ntype_op::Get_mask:
    case Ntype_op::Set_mask:
    case Ntype_op::Nconst  : return 0;

    case Ntype_op::Sub: return atleast1(ge_detail::sub_port_bits(node));

    case Ntype_op::Div:
    case Ntype_op::Rem:
    case Ntype_op::Memory: return atleast1(ge_detail::port_bits_sum(node));

    case Ntype_op::Mult: {
      const uint64_t w = ge_detail::out_width(node);
      return w == 0 ? 1 : w * w;
    }

    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ:
    case Ntype_op::Ror: return atleast1(ge_detail::widest_operand(node));

    default: return atleast1(ge_detail::out_width(node));
  }
}

// The GE that a synthesis mapper (pass.abc) will actually bit-blast when this
// node sits inside a region of ITS def. A Sub is a blackbox there -- its logic
// is weighed in its own def's regions, so counting its declared port bits here
// double-counts the child and lifts a zero-logic glue+instance region past any
// size floor (XSCore: thousands of <=2-node regions "weighing" 5k-50k GE from
// ports alone); it floors at 1 so a region is never weightless. Everything
// else keeps ge_weight -- including Memory, which pass.abc DOES decompose when
// asked (memory=true), so its boundary weight is a fair blast estimate.
//
// Use this for region-size windows and their stats; keep ge_weight where the
// boundary itself is the cost being modeled (color_absorb's black-box weigher).
[[nodiscard]] inline uint64_t mappable_ge_weight(const hhds::Node_class& node) {
  if (node.is_invalid() || is_builtin_node(node)) {
    return 0;
  }
  if (type_op_of(node) == Ntype_op::Sub) {
    return 1;
  }
  return ge_weight(node);
}

}  // namespace livehd::graph_util
