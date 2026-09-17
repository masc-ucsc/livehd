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
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "absl/container/inlined_vector.h"


#include "attrs.hpp"
#include "cell.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"

namespace livehd::graph_util {

// A snapshot of a DRIVER's fan-out edges. Matches the element type and inline
// capacity hhds's out_edges() view yields, so `Edge_vec v(r.begin(), r.end())`
// never reallocates for the common low-degree driver.
//
// There is no in-edge twin any more: the in-edge shape is a pin walk,
// `for (auto sink : n.inp_sorted_pins()) for (auto drv : sink.get_driver_pins())`
// (or inp_pins_snapshot() when the walk mutates). Both readers are load-bearing:
// SORTED because port 0 is the node itself and the raw inp_pins() drops it, and
// PLURAL because nearly-but-not-quite every sink has one driver -- a compact
// loop's carry-in has two (pass/legalize's verify_single_driver_sinks sanctions
// exactly that one), and the singular get_driver_pin() would drop the second.
using Edge_vec = absl::InlinedVector<hhds::Edge_class, 4>;

// Disconnect a SINK pin from everything driving it.
//
// This is the replacement for the old `for (auto e : sink.inp_edges())
// e.del_edge();` idiom. It takes a SNAPSHOT first, because del_sink() mutates
// the very storage a driver view walks. The plural reader is deliberate: a
// compact loop's carry-in sink is the one pin pass/legalize sanctions with two
// drivers, and "disconnect this sink" has to mean ALL of them.
//
// Spelling note, because hhds's names read backwards at the call site:
// `Pin_class::del_sink(driver)` is `del_edge(driver, *this)` -- it removes MY
// driver -- while `del_driver()` removes MY out-edges.
inline void drop_drivers(const hhds::Pin_class& sink) {
  auto drivers = sink.get_driver_pins();
  for (const auto& drv : drivers) {
    sink.del_sink(drv);
  }
}

// Disconnect every sink pin of a node from everything driving it.
inline void drop_drivers(const hhds::Node_class& node) {
  for (auto sink : node.inp_pins_snapshot()) {
    drop_drivers(sink);
  }
}

// One operand of a cell: the sink pin it lands on and the driver feeding it.
template <typename PinT>
struct Sink_driver {
  PinT sink;
  PinT driver;

  [[nodiscard]] hhds::Port_id get_port_id() const { return sink.get_port_id(); }
};

// The operand list of a node, ascending by sink port id.
//
// WHY THIS EXISTS AND NOT A BARE inp_sorted_pins() CALL. The walkers below --
// and every templated cone walker in graph/ -- are instantiated for BOTH
// hhds::Node_class (flat) and hhds::Occurrence_node (hierarchical). BOTH handle
// families answer inp_sorted_pins() now, but they do not yield the same pin
// TYPE and they do not have the same driver multiplicity, so this overload pair
// is the one place that difference is spelled out: a shared walker reads
// `sink.get_port_id()` / `sink.driver` -- one entry per operand EDGE -- and does
// not care which family it was stamped for.
//
// SORTED, never the raw inp_pins(): hhds stores port 0 as the NODE ITSELF, so
// the raw pin list omits it, and port 0 is exactly where a banked cell's first
// operand lives (see append_sink_operand). inp_sorted_pins() yields the
// node-as-pin first, then the list ascending by sink pid -- the order this
// helper promises.
//
// The FLAT overload still gets the whole point of the pin redesign: the
// operands come off the node's OWN pin list, so a driver pin with a 12k fanout
// is skipped by a direction-bit test instead of having 12k out-edges decoded
// and discarded. The small vector it fills is per-CELL (a Mux has 3 operands,
// an And 2) and is what makes the two handle families share one body.
//
// A pin yielded by inp_sorted_pins() is CONNECTED, so the flat overload's
// `driver` is valid. The HIER overload uses the PLURAL get_driver_pins() per
// sink instead: one cross-boundary sink can resolve to SEVERAL drivers (a
// compact loop's carry-in becomes the previous occurrence's output plus, with
// an activation input, the inactive-carry bypass), and dropping the extra ones
// would silently shorten the operand list the edge reader used to hand back.
[[nodiscard]] inline absl::InlinedVector<Sink_driver<hhds::Pin_class>, 4> inp_sink_drivers(const hhds::Node_class& node) {
  absl::InlinedVector<Sink_driver<hhds::Pin_class>, 4> out;
  if (node.is_invalid()) {
    return out;
  }
  // PLURAL in BOTH overloads: the contract above is one entry per operand EDGE.
  // A compact loop's carry-in is the one sink pass/legalize sanctions with two
  // drivers, and the singular get_driver_pin() returns drivers.front() behind an
  // assert -DNDEBUG erases -- so the flat list would silently come back SHORT,
  // and every caller that pairs it POSITIONALLY against the occurrence-side list
  // would then pair the wrong items.
  for (auto sink : node.inp_sorted_pins()) {
    for (auto driver : sink.get_driver_pins()) {
      out.push_back({sink, driver});
    }
  }
  return out;
}

[[nodiscard]] inline absl::InlinedVector<Sink_driver<hhds::Occurrence_pin>, 4> inp_sink_drivers(
    const hhds::Occurrence_node& node) {
  absl::InlinedVector<Sink_driver<hhds::Occurrence_pin>, 4> out;
  if (node.is_invalid()) {
    return out;
  }
  for (auto sink : node.inp_sorted_pins()) {
    for (auto driver : sink.get_driver_pins()) {
      out.push_back({sink, driver});
    }
  }
  return out;
}

// hhds guarantees inp_sorted_pins() is SINK-PORT ASCENDING, but says nothing
// about the order of SEVERAL DRIVERS OF ONE SINK PIN (a Sum's `as` fed by three
// nodes, an Or's `a`) -- that is edge storage order. A consumer that needs a
// deterministic emission or hash order there imposes `less` inside each
// sink-pin RUN and leaves the port order hhds already fixed alone.
//
// O(n) when every pin has a single driver, which is nearly every pin.
template <typename Edges, typename Less>
inline void sort_drivers_within_pin(Edges& edges, Less less) {
  auto it = edges.begin();
  while (it != edges.end()) {
    const auto pid     = it->sink.get_port_id();
    auto       run_end = it;
    while (run_end != edges.end() && run_end->sink.get_port_id() == pid) {
      ++run_end;
    }
    if (std::distance(it, run_end) > 1) {
      std::sort(it, run_end, less);
    }
    it = run_end;
  }
}

// Hotmux has contiguous (one-bit control, value) pairs at p(2*i), p(2*i+1).
// An optional trailing even pin is the default value when every control is
// zero. Without a default the zero-control result is zero. Multiple active
// controls violate the cell's one-hot-or-zero obligation.
template <typename Pin>
struct Hotmux_inputs {
  std::vector<std::pair<Pin, Pin>> arms;
  Pin                              fallback;
};

template <typename Node>
[[nodiscard]] inline auto hotmux_inputs(const Node& node) {
  // The operand list is sink-port ascending by contract (hhds graph.hpp), so
  // the pairs come out in pin order. inp_sink_drivers serves BOTH the flat and
  // the hierarchical handle -- see its note -- which is why this stays a
  // template over `Node`.
  auto               ins = inp_sink_drivers(node);
  using Pin              = std::decay_t<decltype(ins[0].driver)>;
  Hotmux_inputs<Pin> result;
  Pin                pending;
  size_t             pid = 0;
  for (const auto& in : ins) {
    I(in.get_port_id() == pid);
    if (pid % 2 == 0) {
      pending = in.driver;
    } else {
      result.arms.emplace_back(pending, in.driver);
    }
    ++pid;
  }
  if (pid % 2) {
    result.fallback = pending;
  }
  return result;
}

// Exclusive upper bound for control pids, computed once per consumer sweep.
template <typename Node>
[[nodiscard]] inline size_t hotmux_control_end(const Node& node) {
  size_t end = 0;
  for (const auto& in : inp_sink_drivers(node)) {
    const auto pid = in.get_port_id();
    if (pid % 2) {
      end = std::max(end, static_cast<size_t>(pid) + 1);
    }
  }
  return end;
}

[[nodiscard]] inline bool is_hotmux_control(hhds::Port_id pid, size_t control_end) { return pid % 2 == 0 && pid < control_end; }

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
// Constant pins (HHDS Graph::CONST_NODE singleton + constant pool).
// ---------------------------------------------------------------------------
//
// Every LiveHD constant is a driver pin on CONST_NODE whose Dlop is held BY
// VALUE in hhds's constant pool (`Graph::create_constant`, one slot per pin):
// `pin.is_const()`, `pin.const_value()`, `pin.is_known_false()` and
// `pin.is_known_true()` are hhds members that answer from a PinEntry load and
// a pool index -- no attr lookup, no unserialize, no allocation. Only PINS
// carry values; a node has none (port 0 is the node, and it is never a
// constant), so probing a node does not compile.

// Create-or-find the const pin for `value`. Nothing is canonicalized here:
// hlop already makes every spelling the graph needs the ONLY spelling --
//  * a Boolean IS the hardware u1 (`true` = 1, `false` = 0, an unknown bool =
//    `0ub?`), never a signed all-ones payload;
//  * Dlop::hash keys on the minimal (sign-extension-trimmed) representation,
//    the same equivalence same_repr() uses, so a wide-operand fold and a
//    literal dedup to ONE pin without a rebuild.
// Invalid / Nil are not values: hhds refuses them (std::invalid_argument) --
// a producer that computed "no value" must keep its node, not mint a 0.
[[nodiscard]] inline hhds::Pin_class create_const(hhds::Graph& g, const Dlop& value) { return g.create_constant(value); }

[[noreturn]] inline void not_a_constant(const hhds::Pin_class& pin) {
  std::fprintf(stderr,
               "livehd: const_of: pin %llu (%s) is not a constant\n",
               static_cast<unsigned long long>(pin.get_debug_pid()),
               pin.is_invalid() ? "invalid" : "driven by a cell");
  std::abort();
}

// The ONE full-value accessor: the constant a pin carries, by reference into
// the graph's constant pool. The reference survives create_const on the same
// graph (the pool never relocates) and dies with the body -- never hold it
// across load/copy/clear of that graph. A NON-constant pin is a BUG at the
// call site, never a 0: hard failure in every build mode. Where a pin may
// legitimately be non-constant, probe `pin.is_const()` / `pin.const_value()`.
[[nodiscard]] inline const Dlop& const_of(const hhds::Pin_class& pin) {
  const Dlop* v = pin.const_value();
  if (v == nullptr) [[unlikely]] {
    not_a_constant(pin);
  }
  return *v;
}
[[nodiscard]] inline const Dlop& const_of(const hhds::Occurrence_pin& pin) { return const_of(pin.base_pin()); }
// Nodes never carry a value: probing one is a compile error, not a silent 0.
const Dlop&                      const_of(const hhds::Node_class&)      = delete;
const Dlop&                      const_of(const hhds::Occurrence_node&) = delete;


// hhds `NodeEntry::type` is 16 bits whose bit 0 is hhds's own per-node
// `is_loop_break` cut flag. LiveHD stores `(op << 1) | loop_last` (see
// set_type_op) and reads the op back with one shift. hhds's `set_subnode`
// rewrites ONLY bit 0 (per instance: set iff the child body holds state), so
// the op survives it and a Sub reads as Sub with no link check. Returns
// Ntype_op::Invalid (== 0) for nodes that were never typed.
[[nodiscard]] inline Ntype_op type_op_of(const hhds::Node_class& node) {
  return static_cast<Ntype_op>(static_cast<uint16_t>(node.get_type()) >> 1);
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

// Node-IDENTITY probe: is this the CONST_NODE singleton (the master of every
// constant pin)? It says nothing about a value -- values live on PINS
// (pin.is_const() / pin.const_value() / const_of(pin)); a node has none.
[[nodiscard]] inline bool is_type_const(const hhds::Node_class& node) { return node.get_debug_nid() == hhds::Graph::CONST_NODE; }
[[nodiscard]] inline bool is_type_const(const hhds::Occurrence_node& node) { return is_type_const(node.base_node()); }

// Connect a folded constant to one consumer sink.
//
// HISTORY / why this is now a one-liner: hhds edges are unique per
// (driver, sink) PAIR, and a commutative cell used to pile all of its operands
// onto ONE sink pin -- so redirecting a second copy of the same constant onto a
// Sum's `as` was silently DEDUPED (`2 + 2` became `2`) and this helper had to
// fold the collision by hand (`2 + 2` -> the constant 4, repeating). Under ONE
// DRIVER PER SINK PIN each operand owns its own pid, two equal constants sit on
// two different pins, and the multiset is representable directly. There is
// nothing left to fold: a collision on one pin would mean that pin already had
// two drivers.
//
// The signature is kept (graph, value, sink) because callers pass the graph
// anyway and a future rule may need it again. Connecting while the sink still
// carries the driver being replaced is the usual transient -- the caller's
// del_node drops the old edge immediately after -- and is legal DURING a
// mutation; the one-driver-per-sink-pin check is a legalize pass, not an
// invariant that holds mid-rewrite.
inline void connect_folded_const([[maybe_unused]] hhds::Graph& graph, hhds::Pin_class value, const hhds::Pin_class& sink) {
  value.connect_sink(sink);
}

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
  // No subnode binding left. A marker still identifies itself by the payload
  // it packs into its NAME attr -- "<kind>\x1f<loc>\x1f<msg>" for fproperty,
  // "<loc>\x1f<node>" for lgassert -- which is the same signature
  // Sub_inliner/Flattener use to know they must not prefix that name. \x1f is
  // not a legal identifier character, so a genuine body-less black box (external
  // IP, a Liberty cell) can never collide with it, and such a box must keep
  // reaching whatever refusal its consumer has.
  return node_name_of(n).find('\x1f') != std::string_view::npos;
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
// guarded so none of it is built in a release compile.
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
  if (dpin.is_const()) {
    const auto& c = const_of(dpin);
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
    const int need = is_uns ? c.get_last_bit_set() + 1 : static_cast<int>(c.get_signed_bits());
    I(need <= nbits,
      std::format("const pin '{}' needs {} bits but is declared {} {}signed bits", wire_name(dpin), need, nbits, is_uns ? "un" : "")
          .c_str());
    return;
  }

  const auto op = type_op_of(dpin.get_master_node());
  if (op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::EQ || op == Ntype_op::Ror || op == Ntype_op::Rxor
      || op == Ntype_op::Clock_cell) {
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
    if (mask.is_const()) {
      const auto& mv = const_of(mask);
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
    if (op == Ntype_op::Invalid || Ntype::has_multiple_driver_pins(op)) {
      continue;
    }
    auto dpin = node.create_driver_pin(0);
    if (dpin.is_invalid()) {
      continue;
    }
    debug_check_pin_hint(dpin);
    if (dpin.is_const()) {
      continue;
    }
    if (bits_of(dpin) == 0) {
      std::string inputs;
      for (const auto& edge : inp_sink_drivers(node)) {
        if (!inputs.empty()) {
          inputs += ", ";
        }
        inputs += std::format("p{}:{}b", edge.sink.get_port_id(), bits_of(edge.driver));
        if (edge.driver.is_const()) {
          const auto& value  = const_of(edge.driver);
          inputs            += std::format(":const({}b,{})", value.get_signed_bits(), value.is_negative() ? "neg" : "nonneg");
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
//   edge/any   -> no driver/sink restriction (shared wire name, IO offsets, …)
//   node       -> static error: a node attribute must not go through a pin setter.
// There is no `sink` role: attributes are forbidden on sink pins, and
// Attr_kind has no enumerator for one, so "stamp this on a sink" is not a
// classification anybody can write (see attrs.hpp).
// Call only on a VALID pin (the setters early-return on invalid first).
//
// ALWAYS ON, deliberately -- this was an `I()` until 2026-09-17, and `I()` is
// erased by -DNDEBUG, which `.bazelrc` sets for every `-c opt` build. So the ONE
// guard against a structural aliasing bug was absent from precisely the builds
// that ship and that every bench runs. That is the same "silent in opt" shape
// that let a vacuous single-driver verifier and a dropped carry seed survive in
// this tree, so it is a hard check now: the cost is one predictable branch on an
// already-loaded word, and the failure it catches is a wrong VALUE on an
// unrelated pin, which no later pass can detect.
template <typename Tag>
inline void assert_pin_attr_role([[maybe_unused]] const hhds::Pin_class& pin) {
  static_assert(livehd::attrs::attr_kind<Tag> != livehd::attrs::Attr_kind::node,
                "node attribute set through a pin setter — use the node overload");
  if constexpr (livehd::attrs::attr_kind<Tag> == livehd::attrs::Attr_kind::driver_pin) {
    if (!pin.is_driver()) [[unlikely]] {
      std::fprintf(stderr,
                   "livehd: FATAL: a DRIVER-pin attribute was written on a SINK pin.\n"
                   "  The per-pin attribute key folds the driver/sink bit, so a same-port driver and\n"
                   "  sink SHARE one slot: this write would silently overwrite the driver's value\n"
                   "  (e.g. a mux's 66-bit output width landing on its 2-bit select sink).\n"
                   "  Write it on the driver pin, and read it via bits_of(driver_of(sink)).\n");
      std::abort();
    }
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
  pin.attr(livehd::attrs::pin_signed).set();
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

// Per-node formal-verification status. 0 == absent;
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
// to a port_id via Ntype (or, for a Sub, via the sub-graph's GraphIO decls) and
// ask hhds for that pin with try_get_sink_pin, which answers with an invalid
// pin instead of asserting the way get_sink_pin does.
//
// A pin with NO DRIVER reads as a miss too. That is the LiveHD contract these
// callers have always had -- the lookup used to be an inp_edges() walk, so a
// pin left dangling by a del_edge() (cprop does this constantly) was invisible
// -- and `is_sink_connected` below is exactly that question. The difference
// from a pure existence test is one InlinedVector of the pin's own fan-in,
// against the whole node's fan-in the walk used to materialize.

[[nodiscard]] inline hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  auto          op  = type_op_of(node);
  hhds::Port_id pid = hhds::Port_invalid;
  if (op == Ntype_op::Sub) {
    auto sub_io = node.get_subnode_io();
    if (!sub_io || !sub_io->has_input(name)) {
      return {};
    }
    pid = sub_io->get_input_port_id(name);
  } else {
    pid = Ntype::get_sink_pid(op, name);
  }
  if (pid == hhds::Port_invalid) {
    return {};
  }
  if (Ntype::is_banked_sink_op(op)) {
    // A banked name denotes a BANK of consecutive operand slots, not one pin
    // (Ntype's ONE DRIVER PER SINK PIN block). Answer with its LOWEST DRIVEN
    // slot, so "is this bank used at all" and the single-operand case both
    // read the way they always did. A caller that needs EVERY operand of the
    // bank must use bank_sinks / inp_drivers_of.
    const auto bank = Ntype::sink_bank(op, pid);
    // inp_sorted_pins(), NOT the raw inp_pins(): port 0 is the node ITSELF in
    // hhds and is absent from the pin linked list, so a raw pin scan misses the
    // bank's first slot entirely (see append_sink_operand). inp_sorted_pins
    // yields the node-as-pin first, then the list, ascending by sink pid.
    for (auto sink : node.inp_sorted_pins()) {
      if (Ntype::sink_bank(op, sink.get_port_id()) == bank) {
        return sink;
      }
    }
    return {};
  }
  auto pin = node.try_get_sink_pin(pid);
  if (pin.is_invalid() || !pin.has_driver()) {
    return {};
  }
  return pin;
}

// EVERY driven sink pin of a banked cell's operand bank, in ascending pid
// order. Empty for a non-banked op or an unused bank.
[[nodiscard]] inline absl::InlinedVector<hhds::Pin_class, 4> bank_sinks(const hhds::Node_class& node, std::string_view name) {
  absl::InlinedVector<hhds::Pin_class, 4> out;
  if (node.is_invalid()) {
    return out;
  }
  const auto op = type_op_of(node);
  if (!Ntype::is_banked_sink_op(op)) {
    return out;
  }
  const auto pid = Ntype::get_sink_pid(op, name);
  if (pid == hhds::Port_invalid) {
    return out;
  }
  const auto bank = Ntype::sink_bank(op, pid);
  // inp_sorted_pins(), NOT the raw inp_pins(): port 0 is the node itself and
  // never appears in the pin linked list (see append_sink_operand);
  // inp_sorted_pins yields it first, then the list ascending by sink pid.
  for (auto sink : node.inp_sorted_pins()) {
    if (Ntype::sink_bank(op, sink.get_port_id()) == bank) {
      out.push_back(sink);
    }
  }
  return out;
}

// Hier twin, on the same SORTED pin walk as the flat overload:
// Occurrence_node::inp_sorted_pins() yields the node-as-pin (port 0, where a
// banked cell's first operand lives and which the raw inp_pins() omits) first,
// then the pin list ascending by sink pid.
//
// The `get_driver_pins()` probe is what keeps the no-driver-reads-as-a-miss
// contract documented above: a hier sink pin exists as soon as the LOCAL edge
// does, but its cross-boundary resolution can come back empty (a loop domain
// index, a carry self-edge replaced by the virtual chain), and the edge reader
// this replaced reported that as a miss. It is PLURAL -- the only question
// asked of it is "empty?", and one resolved sink can carry several drivers.
[[nodiscard]] inline hhds::Occurrence_pin find_sink_pin(const hhds::Occurrence_node& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    // Same invalid-on-miss contract as the Node_class overload above: hhds
    // get_sink_pin asserts on a declared-but-unconnected input.
    auto sub_io = node.get_subnode_io();
    if (!sub_io || !sub_io->has_input(name)) {
      return {};
    }
    const auto sub_pid = sub_io->get_input_port_id(name);
    for (auto sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() == sub_pid && !sink.get_driver_pins().empty()) {
        return sink;
      }
    }
    return {};
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == hhds::Port_invalid) {
    return {};
  }
  for (auto sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() == pid && !sink.get_driver_pins().empty()) {
      return sink;
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
  for (auto sink : node.inp_sorted_pins()) {  // read-only walk, ascending pid
    const auto sp = static_cast<uint32_t>(sink.get_port_id());
    // Compare INSIDE the driver loop so the first driver of the lowest pid still
    // wins, which is what the edge walk returned; the hier twin below does the
    // same, and the two overloads must not disagree on which reader they trust.
    for (auto drv : sink.get_driver_pins()) {
      if (a.is_invalid() || sp < best) {
        a    = drv;
        best = sp;
      }
    }
  }
  return a;
}

// Hier twin, on Occurrence_node's own inp_sorted_pins(). SORTED is what makes
// the lowest-sink-pid pick correct at all: hhds stores port 0 as the node
// itself, so the raw inp_pins() omits the LOWEST pid there is, and a Get_mask
// whose value operand sits there would be answered with its mask constant.
// The PLURAL get_driver_pins() keeps every resolved driver of a cross-boundary
// sink in the walk, exactly as the edge reader it replaced did; drivers of ONE
// pin share `sp`, so `sp < best` is false for the later ones and the first
// driver of the lowest pin still wins. See inp_sink_drivers for the shim a
// SHARED (templated) walker uses instead of either overload.
[[nodiscard]] inline hhds::Occurrence_pin first_value_driver(const hhds::Occurrence_node& node) {
  hhds::Occurrence_pin a;
  uint32_t             best = 0;
  for (auto sink : node.inp_sorted_pins()) {
    const auto sp = static_cast<uint32_t>(sink.get_port_id());
    for (auto driver : sink.get_driver_pins()) {
      if (a.is_invalid() || sp < best) {
        a    = driver;
        best = sp;
      }
    }
  }
  return a;
}

// Returns the (single) driver pin feeding a named sink. If the sink is
// unconnected, returns an invalid pin. The multi-driver sinks (those where
// Ntype::is_sink_single_driver is false -- the 's'-suffixed "as"/"bs" pins)
// require inp_drivers_of instead.
[[nodiscard]] inline hhds::Pin_class get_driver_of_sink_name(const hhds::Node_class& node, std::string_view name) {
  if (Ntype::is_banked_sink_op(type_op_of(node))) {
    // A banked name is a whole operand bank; taking its first slot would
    // silently drop the rest. Answer only when the bank holds ONE operand.
    auto slots = bank_sinks(node, name);
    if (slots.empty()) {
      return {};
    }
    I(slots.size() == 1, "get_driver_of_sink_name on a multi-operand bank; use inp_drivers_of");
    auto drivers = slots.front().get_driver_pins();
    return drivers.empty() ? hhds::Pin_class{} : drivers.front();
  }
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

// Reduction semantics use the explicit count operand, never a pin-width hint.
template <class Node>
[[nodiscard]] inline int reduction_count(const Node& node) {
  const auto count = get_driver_of_sink_name(node, "b");
  if (count.is_invalid() || !count.is_const()) {
    throw std::invalid_argument("Rxor/Popcount requires a constant bit count");
  }
  const auto& value = const_of(count);
  if (value.has_unknowns() || !value.is_just_i64() || value.to_just_i64() < 0
      || value.to_just_i64() > std::numeric_limits<int>::max()) {
    throw std::invalid_argument("Rxor/Popcount requires a non-negative bit count");
  }
  return static_cast<int>(value.to_just_i64());
}

// Cell-type mutation. hhds owns bit 0 of `NodeEntry::type` (its per-node
// `is_loop_break` cut flag), so the op is stored SHIFTED LEFT by one with that
// bit seeded from Ntype::is_loop_last; type_op_of() shifts it back. Keep the
// two in step. Call this BEFORE `set_subnode` on a Sub: set_subnode rewrites
// only bit 0 (per instance, set iff the child body holds state) and this write
// would otherwise clobber that decision back to the conservative 1.
inline void set_type_op(const hhds::Node_class& node, Ntype_op op) {
  const uint16_t cut = Ntype::is_loop_last(op) ? 1u : 0u;  // hhds bit 0 = is_loop_break
  node.set_type(static_cast<hhds::Type>((static_cast<uint16_t>(op) << 1) | cut));
}

// Create a new node of the given Ntype_op in `graph`. Returns the node;
// callers may then create driver / sink pins on it as needed.
[[nodiscard]] inline hhds::Node_class create_typed_node(hhds::Graph& graph, Ntype_op op) {
  auto node = graph.create_node();
  set_type_op(node, op);
  return node;
}

// APPEND one operand slot to a BANKED cell's operand bank (Ntype's ONE DRIVER
// PER SINK PIN block). `bank` is the bank's first pid -- 0 for "as", 1 for
// "bs" -- and the stride between consecutive slots of one bank is the cell's
// bank count, so the "as" slots of a Sum are 0, 2, 4, ... and its "bs" slots
// are 1, 3, 5, ...
//
// Returns the LOWEST slot of that bank that is still free. "Free" means UNDRIVEN
// (hhds::Node_class::inp_sorted_pins yields only sink pins that already carry an
// edge), so the usual build shape
//
//   setup_sink_by_name(sum, "as").connect_driver(x);   // -> pid 0
//   setup_sink_by_name(sum, "as").connect_driver(y);   // -> pid 2
//
// appends, while a setup that is never connected is reused by the next append
// instead of leaving a disconnected pin behind. That matters: a disconnected
// sink pin is legal only DURING a mutation, and the one-driver-per-sink-pin
// legalize check rejects one that survives it.
[[nodiscard]] inline hhds::Pin_class append_sink_operand(const hhds::Node_class& node, Ntype_op op, hhds::Port_id bank) {
  const auto stride = static_cast<hhds::Port_id>(Ntype::sink_bank_count(op));
  I(stride != 0, "append_sink_operand on a cell with no operand banks");
  I(bank < stride, "append_sink_operand: bank pid outside this cell's bank count");
  // Walk inp_sorted_pins(), not the raw hhds::Node_class::inp_pins().
  //
  // hhds stores PORT 0 as the NODE ITSELF (create_sink_pin(0) hands back a
  // Pin_class over the node's own Nid), so port 0 is not in the node's pin
  // linked list and get_sink_pins -- the backing of inp_pins() -- can never
  // report it. Scanning pins therefore never saw the FIRST operand of a bank,
  // every append answered `bank` again, and every operand of every commutative
  // cell piled back onto one pin. (Silently: hhds then DEDUPES a repeated
  // (driver, sink) pair, so `(x == 0) ^ 1` with the EQ folded to 1 became
  // `Xor(1)` = 1 instead of `1 ^ 1` = 0 -- caught by inou/prp's folded_rotate.)
  // inp_sorted_pins() walks the node-as-pin entry and the pin list both.
  //
  // The lowest FREE slot, so a setup that was never connected is reused by the
  // next append instead of leaving a disconnected pin behind: an undriven pin
  // contributes no edge, so it does not move `next`.
  hhds::Port_id next = bank;
  for (auto sink : node.inp_sorted_pins()) {
    const auto pid = sink.get_port_id();
    if (Ntype::sink_bank(op, pid) != bank) {
      continue;
    }
    if (pid >= next) {
      next = static_cast<hhds::Port_id>(pid + stride);
    }
  }
  return node.create_sink_pin(next);
}

// Create-if-missing sink pin by RAW pid, bank-aware.
//
// Use this instead of `node.create_sink_pin(pid)` whenever the pid is a
// LITERAL the caller chose (building a fresh cell), because on a BANKED op
// (graph/cell.hpp's ONE DRIVER PER SINK PIN block) a literal names a BANK and
// each operand needs its own slot:
//
//   setup_sink_pid(andn, 0).connect_driver(a);   // -> pid 0
//   setup_sink_pid(andn, 0).connect_driver(b);   // -> pid 1
//
// Do NOT use it to COPY a pid from another graph (`create_sink_pin(
// e.sink.get_port_id())`): a copy must reproduce the source pid exactly, and
// appending would renumber the operands.
[[nodiscard]] inline hhds::Pin_class setup_sink_pid(const hhds::Node_class& node, hhds::Port_id pid) {
  const auto op = type_op_of(node);
  if (Ntype::is_banked_sink_op(op)) {
    return append_sink_operand(node, op, Ntype::sink_bank(op, pid));
  }
  return node.create_sink_pin(pid);
}

// Create-if-missing sink pin lookup by LiveHD-style name. For Sub nodes the
// name path goes through HHDS's create_sink_pin(name) directly; for other ops
// we translate name→port_id via Ntype and call create_sink_pin(port_id).
//
// For a BANKED op ("as"/"bs" on Sum/LT/GT/Mult/And/Or/Xor/Ror/EQ) the name
// denotes a BANK rather than a single pin, so this APPENDS a fresh operand slot
// (see append_sink_operand). Every existing `setup_sink_by_name(n, "as")
// .connect_driver(d)` call site therefore builds the same cell it always did,
// with each operand now on its own pin.
[[nodiscard]] inline hhds::Pin_class setup_sink_by_name(const hhds::Node_class& node, std::string_view name) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.create_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == hhds::Port_invalid) {
    return {};
  }
  if (Ntype::is_banked_sink_op(op)) {
    return append_sink_operand(node, op, Ntype::sink_bank(op, pid));
  }
  return node.create_sink_pin(pid);
}

// ---------------------------------------------------------------------------
// Get_mask / Set_mask mask pin: the contiguous-window contract (graph/cell.hpp)
// ---------------------------------------------------------------------------
// The mask constant is either the window [lo, hi) or the literal -1 ("the whole
// value"). These are the ONLY spellings the IR accepts, so every consumer reads
// a window instead of scanning for runs.

// The window CONSTANT for bits [lo, hi). `lo == 0 && hi <= 0` is rejected: a
// mask that selects nothing is not a cell, it is a constant 0 the producer
// should have folded.
[[nodiscard]] inline Dlop mask_window_const(int lo, int hi) {
  if (lo < 0 || hi <= lo) {
    throw std::invalid_argument("Get_mask/Set_mask window must be a non-empty [lo, hi) with lo >= 0");
  }
  return *Dlop::get_mask_value(hi - 1, lo);
}

// "The whole value": Get_mask(a, -1) is to-unsigned, Set_mask(a, -1, v) is v.
[[nodiscard]] inline Dlop mask_whole_const() { return *Dlop::create_integer(-1); }
[[nodiscard]] inline bool is_whole_value_mask(const Dlop& mask) {
  return mask.is_integer() && !mask.has_unknowns() && mask.is_just_i64() && mask.to_just_i64() == -1;
}

// True for either legal spelling. A producer minting a mask pin passes through
// here; a consumer normally asks mask_window() instead.
[[nodiscard]] inline bool is_legal_mask(const Dlop& mask) {
  if (!mask.is_integer() || mask.has_unknowns()) {
    return false;
  }
  if (is_whole_value_mask(mask)) {
    return true;
  }
  if (mask.is_negative()) {
    return false;  // a carve-out other than -1 is not part of the IR
  }
  const auto [lo, hi] = mask.get_mask_range();  // {-1,-1} == noncontiguous
  return lo >= 0 && hi > lo;
}

// The window as an OPTIONAL: empty for the -1 whole-value spelling (which has
// no window) and for anything the contract forbids. This is the form for a
// caller whose answer to "not a bit-field slice" is to DECLINE -- an analysis
// that only ever refuses an optimization stays correct whatever it is handed,
// so it needs no assert of its own and no hand-rolled contiguity test.
[[nodiscard]] inline std::optional<std::pair<int, int>> mask_window_of(const Dlop& mask) {
  if (!mask.is_integer() || mask.has_unknowns() || mask.is_negative()) {
    return std::nullopt;
  }
  const auto [lo, hi] = mask.get_mask_range();  // {-1,-1} == noncontiguous
  if (lo < 0 || hi <= lo) {
    return std::nullopt;
  }
  return std::pair<int, int>{lo, hi};
}

[[noreturn]] inline void not_a_mask_window(const Dlop& mask) {
  std::fprintf(stderr,
               "livehd: Get_mask/Set_mask mask constant '%s' is neither a contiguous window nor the -1 whole-value "
               "spelling (graph/cell.hpp)\n",
               std::string(mask.to_pyrope()).c_str());
  std::abort();
}

// The half-open [lo, hi) window of a mask constant. FAILS CLOSED on the -1
// spelling (a caller that can handle "the whole value" must test for it first)
// and on anything the contract forbids -- unconditionally, NOT through I():
// silently treating a sparse mask as its bounding window would select bits the
// cell never asked for, and that is a miscompile a release build must not make
// quietly. A caller that simply declines uses mask_window_of instead.
[[nodiscard]] inline std::pair<int, int> mask_window(const Dlop& mask) {
  const auto window = mask_window_of(mask);
  if (!window) {
    not_a_mask_window(mask);
  }
  return *window;
}

// Create a new node of `op` in `graph` and stamp port-0 driver bits.
[[nodiscard]] inline hhds::Node_class create_typed_node(hhds::Graph& graph, Ntype_op op, int32_t bits) {
  auto node = graph.create_node();
  set_type_op(node, op);
  if (bits != 0) {
    auto dpin = node.create_driver_pin(0);
    set_bits(dpin, bits);  // guarded setter, not a raw attr write
  }
  return node;
}

// Validate before creating a node or changing any edges. These checks are not
// debug assertions: consumers may only interpret a mask as one window or -1.
inline void require_mask(const hhds::Pin_class& mask) {
  if (!mask.is_const() || !is_legal_mask(const_of(mask))) {
    throw std::invalid_argument("Get_mask/Set_mask requires a constant contiguous window or -1");
  }
}

inline void connect_mask_operands(const hhds::Node_class& node, const hhds::Pin_class& value, const hhds::Pin_class& mask,
                                  const hhds::Pin_class& replacement = {}) {
  require_mask(mask);
  const auto op = type_op_of(node);
  if ((op != Ntype_op::Get_mask && op != Ntype_op::Set_mask) || value.is_invalid()
      || (op == Ntype_op::Set_mask && replacement.is_invalid())) {
    throw std::invalid_argument("invalid Get_mask/Set_mask operands");
  }
  setup_sink_by_name(node, "a").connect_driver(value);
  setup_sink_by_name(node, "mask").connect_driver(mask);
  if (op == Ntype_op::Set_mask) {
    setup_sink_by_name(node, "value").connect_driver(replacement);
  }
}

[[nodiscard]] inline hhds::Node_class create_get_mask(hhds::Graph& graph, const hhds::Pin_class& value,
                                                      const hhds::Pin_class& mask) {
  require_mask(mask);
  if (value.is_invalid()) {
    throw std::invalid_argument("invalid Get_mask operand");
  }
  auto node = create_typed_node(graph, Ntype_op::Get_mask);
  connect_mask_operands(node, value, mask);
  return node;
}

[[nodiscard]] inline hhds::Node_class create_get_mask(hhds::Graph& graph, const hhds::Pin_class& value, int lo, int hi) {
  return create_get_mask(graph, value, create_const(graph, mask_window_const(lo, hi)));
}

[[nodiscard]] inline hhds::Node_class create_set_mask(hhds::Graph& graph, const hhds::Pin_class& value, const hhds::Pin_class& mask,
                                                      const hhds::Pin_class& replacement) {
  require_mask(mask);
  if (value.is_invalid() || replacement.is_invalid()) {
    throw std::invalid_argument("invalid Set_mask operands");
  }
  auto node = create_typed_node(graph, Ntype_op::Set_mask);
  connect_mask_operands(node, value, mask, replacement);
  return node;
}

[[nodiscard]] inline hhds::Node_class create_set_mask(hhds::Graph& graph, const hhds::Pin_class& value, int lo, int hi,
                                                      const hhds::Pin_class& replacement) {
  return create_set_mask(graph, value, create_const(graph, mask_window_const(lo, hi)), replacement);
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

// All drivers feeding a named sink. For a BANKED op this is the whole operand
// BANK, in ascending pid order -- one driver per slot (Ntype's ONE DRIVER PER
// SINK PIN block). For every other op it is that single pin's fan-in, which is
// also exactly one driver.
[[nodiscard]] inline std::vector<hhds::Pin_class> inp_drivers_of(const hhds::Node_class& node, std::string_view name) {
  std::vector<hhds::Pin_class> result;
  if (node.is_invalid()) {
    return result;
  }
  if (Ntype::is_banked_sink_op(type_op_of(node))) {
    for (const auto& sink : bank_sinks(node, name)) {
      auto drivers = sink.get_driver_pins();
      result.insert(result.end(), drivers.begin(), drivers.end());
    }
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

// A Memory has one clock sink per port: `clock_pin` is a fixed base offset, so
// port i's clock sits at raw pid = i*Memory_port_stride + offset (graph/cell.cpp).
// Visit EVERY port's clock driver in HHDS edge order — a consumer that keeps ONE
// answer per array (e.g. the sim's array-wide write guard) has to look at all of
// them before it can trust the first; looking only at the first silently loses
// clock domains on multi-clock arrays. Tests the raw pid directly instead of
// get_sink_name: that helper builds a std::string per edge (absl::StrCat above
// the stride) on what is a hot per-node path.
template <typename Fn>
inline void for_each_memory_clock_driver(const hhds::Node_class& node, Fn&& fn) {
  I(type_op_of(node) == Ntype_op::Memory, "for_each_memory_clock_driver decodes Memory port blocks; got a non-Memory node");
  const auto clock_off = Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin");
  for (auto sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() % Ntype::Memory_port_stride != clock_off) {
      continue;
    }
    // PLURAL: the contract above is EVERY port's clock driver. Visiting only
    // drivers.front() is exactly the "silently loses clock domains" failure it
    // warns about.
    for (auto drv : sink.get_driver_pins()) {
      fn(drv);
    }
  }
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
  for (auto sink : node.inp_sorted_pins()) {  // read-only walk
    for (auto drv : sink.get_driver_pins()) {  // PLURAL: max over drivers == max over the old edges
      const auto b = bits_of(drv);
      if (b > 0 && static_cast<uint64_t>(b) > w) {
        w = static_cast<uint64_t>(b);
      }
    }
  }
  return w;
}

// Port width across a blackbox boundary: every distinct driver pin, plus every
// distinct sink pin that a non-constant drives. The `seen` dedupe inside `once`
// was load-bearing when this walked EDGES -- an edge walk yields one entry per
// EDGE, so a driver with three readers was counted three times. Both halves now
// walk SORTED PINS, which yield each pin exactly once, so `once` is kept for the
// accumulate half of its job and its dedupe is a belt. Constant-driven
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
  // out_sorted_pins yields each DRIVER PIN once, which is the dedupe this
  // needed out_edges() plus `seen` to do -- `once` stays only because the sink
  // half below still uses it.
  for (auto drv : node.out_sorted_pins()) {
    once(static_cast<uint32_t>(drv.get_port_id()), drv);
  }
  seen.clear();  // driver and sink port ids live in separate spaces
  for (auto sink : node.inp_sorted_pins()) {
    // The const skip is PER DRIVER, as it was per EDGE: a sink whose first
    // driver is constant and whose second is not still contributes its width.
    for (auto drv : sink.get_driver_pins()) {
      if (drv.is_const()) {
        continue;
      }
      once(static_cast<uint32_t>(sink.get_port_id()), drv);  // a sink's width is its driver's
    }
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
  // Gather by pid first: the lane table is INDEXED by pid, a malformed cell may
  // leave holes, and `max_pid` is not known until the walk ends. The SORTED
  // reader is also what makes port 0 appear at all -- lane 0's value driver is
  // stored as the node itself, and the raw inp_pins() list omits it.
  //
  // `emplace` keeps the FIRST driver per pid deliberately: a Concat lane is one
  // operand, so a second driver on one pid is a malformed cell, not a lane.
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> by_pid;
  hhds::Port_id                                       max_pid = 0;
  for (auto sink : node.inp_sorted_pins()) {
    const auto pid = sink.get_port_id();
    for (auto drv : sink.get_driver_pins()) {
      by_pid.emplace(pid, drv);
    }
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
    if (vit == by_pid.end() || wit == by_pid.end() || !wit->second.is_const()) {
      return {};
    }
    const auto& wv = const_of(wit->second);
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
    case Ntype_op::Set_mask: return 0;

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
    case Ntype_op::Rxor    : return atleast1(reduction_count(node)) * 3;
    case Ntype_op::Popcount: return atleast1(reduction_count(node)) * 7;

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
