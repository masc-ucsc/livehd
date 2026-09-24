//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cprop_profile.hpp"
#include "cprop_value.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "pass_cprop.hpp"
#include "perf_tracing.hpp"

using livehd::cprop_value::make_node;
using livehd::graph_util::create_const;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::type_op_of;

#define TRACE(x)
// #define TRACE(x) x

namespace {

// This node's operands, in ascending sink-port order, as a SNAPSHOT.
//
// SNAPSHOT, not a lazy view: every caller rewrites the graph while holding the
// result (connect_driver, del_sink, set_type_op, del_node), and a lazy pin view
// is a view over the storage those calls mutate. inp_edges() materialized for
// exactly this reason; inp_pins_snapshot() is the pin-shaped equivalent.
//
// The SORT this used to do is GONE, and its absence is the point rather than an
// omission. It existed only to impose a deterministic order on the SEVERAL
// DRIVERS OF ONE SINK PIN -- the one thing hhds does not order. Under ONE
// DRIVER PER SINK PIN (graph/cell.hpp) a commutative cell spends one sink pid
// per operand, so every run it sorted has length one and there is nothing left
// to order. What remains is the ascending sink-port contract, which the pin
// chain already keeps, so the whole pass is now order-free by construction.
//
// cprop never sees the one sanctioned multi-driver sink either: a compact
// loop's carry-in belongs to a Sub, and scalar_node returns before reaching
// here for anything is_computed_comb_op rejects.
Cprop::Inp_pins ordered_inp_edges(const hhds::Node_class& node) { return node.inp_pins_snapshot(); }

bool is_two_arm_mux(const Cprop::Inp_pins& edges) {
  return edges.size() == 3 && edges[0].get_port_id() == 0 && edges[1].get_port_id() == 1 && edges[2].get_port_id() == 2;
}

// Materialize HHDS's forward order once. HHDS owns the topological traversal;
// rebuilding it here used to add another indegree map, heap, and edge walk
// before cprop could inspect a single node. TolG is responsible for producing
// a valid graph, so cprop does not run a second cycle-management algorithm.
std::vector<hhds::Node_class> stable_nodes(hhds::Graph* g) {
  livehd::cprop_profile::Timer  timer(livehd::cprop_profile::order);
  std::vector<hhds::Node_class> nodes;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    nodes.push_back(node);
  }
  return nodes;
}

std::vector<hhds::Node_class> storage_nodes(hhds::Graph* graph) {
  std::vector<hhds::Node_class> result;
  for (auto node : graph->body().nodes()) {
    result.push_back(node);
  }
  return result;
}

using livehd::graph_util::const_of;
using livehd::graph_util::setup_sink_by_name;

// Disconnect EVERY operand of `node`, leaving the node in place for its callers
// to re-wire under a new opcode.
//
// SNAPSHOT: del_sink() is a structural mutation, so a lazy pin view taken here
// would be invalidated by the first disconnect. The snapshot is a plain vector
// of handles and only EDGES are removed (never a pin or the node), so every
// later element stays valid -- which is why this is one loop and not the
// collect-then-delete pair it replaces. One driver per sink pin, so dropping a
// sink's driver IS dropping the one in-edge the old code deleted.
void clear_all_sinks(const hhds::Node_class& node) {
  for (const auto& spin : node.inp_pins_snapshot()) {
    spin.del_sink();
  }
}

// Every consumer SINK of `node`, materialized: the node's DRIVER PINS, then
// each pin's own fan-out. Same sinks, same order as the old node-level
// out_edges() walk, but safe to rewire through -- a driver's fan-out is a lazy
// view over live edge storage, and every caller below reconnects or deletes
// while walking it. A driver's fan-out is genuinely a SET (clock and reset
// reach 100K+ sinks), so there is no single-sink reader to use instead.
std::vector<hhds::Pin_class> consumer_sinks(const hhds::Node_class& node) {
  std::vector<hhds::Pin_class> sinks;
  for (const auto& out_pin : node.out_sorted_pins()) {
    for (const auto& e : out_pin.out_edges()) {
      sinks.push_back(e.sink);
    }
  }
  return sinks;
}

// Only explicit operations and literal values can prove a mask redundant.
bool fits_unsigned_window(const hhds::Pin_class& pin, int width) {
  const int bound = livehd::cprop_value::unsigned_width(pin);
  return width > 0 && bound >= 0 && bound <= width;
}

// "Does `p` (a driver pin) or `n` (a node) have EXACTLY one consumer edge?"
// Stops at the second edge instead of walking the whole fan-out.
template <typename T>
[[nodiscard]] bool has_single_consumer(const T& p) {
  size_t consumers = 0;
  for ([[maybe_unused]] const auto& e : p.out_edges()) {
    if (++consumers > 1) {
      return false;
    }
  }
  return consumers == 1;
}

[[nodiscard]] std::string sink_pin_name(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return {};
  }
  auto master = spin.get_master_node();
  auto op     = type_op_of(master);
  return Ntype::get_sink_name(op, spin.get_port_id());
}

[[nodiscard]] hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid);

struct Bool_condition {
  hhds::Pin_class base;
  bool            true_when_base = true;
};

[[nodiscard]] bool same_pin(const hhds::Pin_class& a, const hhds::Pin_class& b) {
  return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
}

[[nodiscard]] std::optional<bool> const_truth(const hhds::Pin_class& p) {
  if (p.is_invalid() || !p.is_const()) {
    return std::nullopt;
  }
  const auto& c = const_of(p);
  if (c.has_unknowns()) {
    return std::nullopt;
  }
  return !c.is_known_zero();
}

// Truth identity and polarity are computed from canonical producers once.
// References follow forwarding and retain their generation across ID reuse.
[[nodiscard]] std::optional<Bool_condition> lookup_bool_condition(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return std::nullopt;
  }
  const auto& conditions = livehd::cprop_value::active->conditions;
  if (auto it = conditions.find(p.get_class_index()); it != conditions.end()) {
    if (!it->second.first) {
      return Bool_condition{p, true};
    }
    const auto source = livehd::Cprop_wiring::resolve_reference(it->second.first);
    return source.is_invalid() ? std::nullopt
                               : std::optional<Bool_condition>{
                                     {source, it->second.second}
    };
  }
  return Bool_condition{p, true};
}

// Reduce the boolean materializations emitted by tolg and by a slang round
// trip: `s ? 1 : 0`, its inverse, and `x == 0` chains. This is deliberately a
// structural decoder, not a general boolean-equivalence proof.
[[nodiscard]] std::optional<Bool_condition> compute_bool_condition(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return std::nullopt;
  }
  if (p.is_const() || is_graph_input_pin(p)) {
    return Bool_condition{p, true};
  }
  auto n = p.get_master_node();
  if (type_op_of(n) == Ntype_op::Mux && is_two_arm_mux(ordered_inp_edges(n))) {
    auto sel  = drv_at(n, 0);
    auto arm0 = drv_at(n, 1);
    auto arm1 = drv_at(n, 2);
    auto v0   = const_truth(arm0);
    auto v1   = const_truth(arm1);
    if (!sel.is_invalid() && v0.has_value() && v1.has_value() && *v0 != *v1) {
      auto result = lookup_bool_condition(sel);
      if (result.has_value() && !*v1) {  // arm1 false, arm0 true => !selector
        result->true_when_base = !result->true_when_base;
      }
      return result;
    }
  } else if (type_op_of(n) == Ntype_op::EQ) {
    // The lowering spells truth tests as `(x == 0) == 0`; peel each equality
    // against known zero and carry its inversion bit. EQ's operands occupy
    // CONSECUTIVE sink pins of one bank, so walk the pins rather than drv_at().
    hhds::Pin_class value;
    int             zeros  = 0;
    int             values = 0;
    for (auto isnk : n.inp_sorted_pins()) {
      auto idrv  = isnk.get_driver_pin();
      auto truth = const_truth(idrv);
      if (truth.has_value() && !*truth) {
        ++zeros;
      } else {
        value = idrv;
        ++values;
      }
    }
    if (zeros == 1 && values == 1) {
      auto result = lookup_bool_condition(value);
      if (result.has_value()) {
        result->true_when_base = !result->true_when_base;
      }
      return result;
    }
  }
  return Bool_condition{p, true};
}

[[nodiscard]] std::optional<Bool_condition> decode_bool_condition(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return std::nullopt;
  }
  auto& facts = *livehd::cprop_value::active;
  if (facts.conditions.contains(p.get_class_index())) {
    return lookup_bool_condition(p);
  }
  auto condition = compute_bool_condition(p);
  if (condition) {
    facts.conditions.emplace(p.get_class_index(),
                             std::pair{condition->base == p && condition->true_when_base ? livehd::Cprop_wiring::Reference{}
                                                                                         : facts.wiring.reference(condition->base),
                                       condition->true_when_base});
  }
  return condition;
}

// ---- packed-wire slice fold helpers ---------------------------------------
// A firtool/Chisel bundle-as-UInt writes a wide net as an Or of constant-shifted
// disjoint fields and reads fields back as constant slices. At WORD level the net
// is one node, so write-field-A/read-field-B looks like a combinational cycle even
// though the bit ranges are disjoint. Resolving each constant slice to the one
// operand that drives it removes the false edge (and is a plain win regardless of
// any cycle). See graph/split_selfref.cpp for the cycle-gated, node-CREATING
// rules (Mux/EQ/distribute) this deliberately does NOT duplicate.

[[nodiscard]] hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid) {
  // Cprop only asks for required single-driver ports of a well-formed builtin.
  // Go directly through that sink's fan-in instead of materializing and
  // filtering every input edge of the node for each port lookup.
  auto sink    = n.get_sink_pin(static_cast<hhds::Port_id>(pid));
  auto drivers = sink.get_driver_pins();
  if (drivers.size() == 1) {
    return drivers.front();
  }
  return {};
}

constexpr std::pair<int, int> kFpBail{-1, -1};
// Canonicalizing one flat, disjoint Or/SHL pack scans immediate operands. Chisel-generated state bundles can
// legitimately carry hundreds of lanes (Rob: 520); refusing them leaves pure
// wiring for ABC to bit-blast independently. Keep a generous hard ceiling for
// malformed/adversarial graphs while covering normal generated aggregates.
constexpr int                 kConcatPackFanInLimit = 4096;

// The CONSTANT shift amount of a SHL, or <0 when not a bounded constant.
[[nodiscard]] int const_shl_amount(const hhds::Node_class& m) {
  auto kd = drv_at(m, 1);
  if (kd.is_invalid() || !kd.is_const()) {
    return -1;
  }
  const auto& kc = const_of(kd);
  if (kc.has_unknowns() || kc.is_negative() || !kc.is_just_i64()) {
    return -1;
  }
  auto k = kc.to_just_i64();
  return (k < 0 || k > (1 << 28)) ? -1 : static_cast<int>(k);
}

// The half-open [begin,end) window of a Get_mask's CONSTANT mask, or kFpBail
// for the `-1` to-unsigned spelling (which has no window).
[[nodiscard]] std::pair<int, int> const_mask_range(const hhds::Node_class& m) {
  auto md = drv_at(m, 2);
  if (md.is_invalid() || !md.is_const()) {
    return kFpBail;
  }
  auto window = livehd::graph_util::mask_window_of(const_of(md));
  return window ? *window : kFpBail;
}

// The width n of a low-contiguous mask 2^n-1 (n>=1), or -1 for anything else
// (negative, unknown-bit, zero, or a run not anchored at bit 0).
[[nodiscard]] int low_mask_width(const Dlop& mask) {
  auto window = livehd::graph_util::mask_window_of(mask);
  return (window && window->first == 0) ? window->second : -1;
}

// "Is `op` a COMPUTED combinational cell (as opposed to an IO/state/Sub/const/
// attr boundary)?"
//
// Ntype::is_comb minus two deliberate exclusions. Clock_cell is a clock-network
// operator, never folded or CSE'd as data. Rem is combinational too but every
// site below has its own measured reason to keep it out (see is_bool01 and
// canonicalize_and_masks).
[[nodiscard]] constexpr bool is_computed_comb_op(Ntype_op op) {
  return Ntype::is_comb(op) && op != Ntype_op::Rem && op != Ntype_op::Clock_cell;
}

using livehd::cprop_value::is_bool01;

// Decode a 2-input EQ against a specific constant: returns the non-const
// operand iff the node has exactly two input edges and exactly one of them is
// the constant `against`. EQ's `as` port is multi-driver, so scan edges.
[[nodiscard]] hhds::Pin_class eq_against_const(const hhds::Node_class& n, int64_t against) {
  if (type_op_of(n) != Ntype_op::EQ) {
    return {};
  }
  hhds::Pin_class value;
  int             matches = 0;
  int             total   = 0;
  for (auto isnk : n.inp_sorted_pins()) {
    auto idrv = isnk.get_driver_pin();
    ++total;
    if (total > 2) {
      return {};
    }
    if (idrv.is_const()) {
      const auto& c = const_of(idrv);
      if (c.is_just_i64() && !c.has_unknowns() && c.to_just_i64() == against) {
        ++matches;
        continue;
      }
    }
    value = idrv;
  }
  if (total != 2 || matches != 1 || value.is_invalid()) {
    return {};
  }
  return value;
}

// ---- hand-spelled concat -> one Ntype_op::Concat ---------------------------
//
// Two idioms SPELL a bit concatenation out of general-purpose cells, and both
// are the dominant shape in real designs: a Set_mask chain that writes one
// field at a time over a zero base, and an Or of constant-shifted disjoint
// fields. Measured on minion (prop_slow.md 3.1): 1,943 Set_mask chain heads
// (405 of them pure ascending 1-bit lanes over a zero base, ~30 nodes each --
// a Verilog concat built one node per BIT) and 1,052 Or-of-disjoint-SHL pack
// trees. Both arms are ON -- see kOrPackEnabled at the bottom of this block for
// what the Or arm's flip depended on.
//
// The canonical interval index resolves overwrite order. Finite layouts can
// emit sliced lanes, with extra interval traffic charged to the invocation's
// edge budget. Signed or unknown tails remain Set_mask operations.
// Set_mask truncates a value to its window; Concat requires a value that fits.
// Preserve that truncation explicitly when constructing the Concat lanes.

struct Pack_lane {
  hhds::Pin_class value;
  int             lo{0};
  int             hi{0};  // half-open
};

// The measured worst case is a full-word pack of 47-64 single-bit writes;
// these are headroom, not tuning knobs.
constexpr int kPackMaxLanes = 1024;
constexpr int kPackMaxWidth = 1 << 20;

// Delete `n` and, transitively, every input whose last consumer it was. Same
// job as Cprop::bwd_del_node, re-spelled here because the canonicalizations
// below are free functions and the class members are not reachable from this
// namespace.
void sweep_dead_node(const hhds::Node_class& n) {
  std::deque<hhds::Node_class>           work;
  // ENQUEUED-ONCE, like Cprop::bwd_del_node's `potential_set`. Without it a node
  // that drives the swept one through TWO edges (`x*x`, a value written into two
  // lanes) is queued twice: the first pop deletes it and the second pops a stale
  // handle straight into has_out_edges()/del_node().
  absl::flat_hash_set<hhds::Class_index> queued;
  work.push_back(n);
  queued.insert(n.get_class_index());
  while (!work.empty()) {
    auto cur = work.front();
    work.pop_front();
    if (cur.is_invalid() || Ntype::is_loop_last(type_op_of(cur)) || livehd::graph_util::is_builtin_node(cur)
        || type_op_of(cur) == Ntype_op::Hotmux || cur.has_out_edges()) {
      continue;
    }
    for (const auto& in_pin : cur.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      if (is_graph_input_pin(in_drv) || is_graph_output_pin(in_drv)) {
        continue;
      }
      auto m = in_drv.get_master_node();
      if (!livehd::graph_util::is_builtin_node(m) && queued.insert(m.get_class_index()).second) {
        work.push_back(m);
      }
    }
    livehd::cprop_value::retire(cur);
  }
}

// Run DCE from every dead leaf in a fresh graph snapshot. Individual scalar
// rewrites normally collect their own obsolete fan-in, but region rewrites
// can expose a dead leaf after its forward visit. One explicit final phase
// collects those leaves independently of rewrite order.
void cleanup_dead_nodes(hhds::Graph* g) {
  livehd::cprop_profile::Timer timer(livehd::cprop_profile::dead);
  auto                         order = storage_nodes(g);
  // Deletion owns its complete backward closure; storage order is sufficient.
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    auto candidate = *it;
    if (!candidate.is_invalid() && !candidate.has_out_edges()) {
      sweep_dead_node(candidate);
    }
  }
}

// Sort the collected windows by position, PROVE they are pairwise disjoint, and
// fill every hole (including the one below the lowest window) with a
// constant-zero lane, so the list tiles [0,W) exactly -- which is the only
// layout a Concat can encode. `lanes` is left LSB-first.
//
// Refuses on overlap rather than picking a winner: for a Set_mask chain the
// head-most write would win and for an Or the bits would merge, so the two
// spellings do not even agree on what an overlap MEANS.
[[nodiscard]] bool tile_pack_lanes(hhds::Graph& g, std::vector<Pack_lane>& lanes) {
  if (lanes.empty() || lanes.size() > kPackMaxLanes) {
    return false;
  }
  std::sort(lanes.begin(), lanes.end(), [](const Pack_lane& a, const Pack_lane& b) { return a.lo < b.lo; });

  std::vector<Pack_lane> tiled;
  tiled.reserve(2 * lanes.size() + 1);
  int pos = 0;
  for (const auto& l : lanes) {
    if (l.hi <= l.lo || l.lo < pos) {
      return false;  // empty or overlapping window
    }
    if (l.lo > pos) {
      tiled.push_back(Pack_lane{create_const(g, *Dlop::create_integer(0)), pos, l.lo});
    }
    tiled.push_back(l);
    pos = l.hi;
  }
  if (pos <= 0 || pos > kPackMaxWidth || tiled.size() > kPackMaxLanes) {
    return false;
  }
  lanes = std::move(tiled);
  return true;
}

// Retype the pack head into the Concat its lane table spells. `tiled` arrives
// LSB-first (as tile_pack_lanes leaves it) and the cell is MSB-first, hence the
// reverse indexing.
void emit_concat(hhds::Graph& g, hhds::Node_class& node, const std::vector<Pack_lane>& tiled) {
  clear_all_sinks(node);
  livehd::graph_util::set_type_op(node, Ntype_op::Concat);

  int32_t total = 0;
  for (size_t i = 0; i < tiled.size(); ++i) {
    const auto&   l     = tiled[tiled.size() - 1 - i];
    const int32_t w     = l.hi - l.lo;
    auto          value = l.value;
    if (!fits_unsigned_window(value, w)) {
      auto mask = Dlop::get_mask_value(w);
      if (value.is_const()) {
        value = create_const(g, *const_of(value).and_op(*mask));
      } else {
        auto get = make_node(g, Ntype_op::Get_mask);
        livehd::graph_util::connect_mask_operands(get, value, create_const(g, *mask));
        value = get.create_driver_pin(0);
        livehd::graph_util::set_ubits(value, w);
      }
    }
    livehd::graph_util::setup_sink_pid(node, static_cast<hhds::Port_id>(2 * i)).connect_driver(value);
    livehd::graph_util::setup_sink_pid(node, static_cast<hhds::Port_id>(2 * i + 1))
        .connect_driver(create_const(g, *Dlop::create_integer(w)));
    total += w;
  }

  // The explicit lane table establishes this finite result capacity.
  auto out = node.create_driver_pin(0);
  livehd::graph_util::set_ubits(out, total);
  livehd::cprop_value::remember(node);
}

// A Set_mask chain with constant, disjoint lanes:
// `set_mask(set_mask(base,m0,v0),m1,v1)…`
bool canonicalize_set_mask_pack(hhds::Graph& g, hhds::Node_class& node) {
  const auto output = node.get_driver_pin(0);
  if (output.is_invalid()) {
    return false;
  }
  if (has_single_consumer(output)) {
    const auto sink = (*output.out_edges().begin()).sink;
    if (type_op_of(sink.get_master_node()) == Ntype_op::Set_mask && sink_pin_name(sink) == "a") {
      return false;
    }
  }
  const int width = livehd::cprop_value::unsigned_width(output);
  if (width <= 0) {
    return false;
  }
  auto pieces = livehd::cprop_value::active->wiring.read(output, 0, width);
  if (!pieces || pieces->empty()) {
    return false;
  }
  for (const auto& piece : *pieces) {
    if (piece.source == output) {
      return false;
    }
  }
  using Slice_key = std::tuple<hhds::Class_index, int64_t, int64_t>;
  absl::flat_hash_set<Slice_key> slices;
  for (const auto& piece : *pieces) {
    if (!piece.source.is_const() && (piece.source_lo != 0 || !fits_unsigned_window(piece.source, piece.hi - piece.lo))) {
      slices.emplace(piece.source.get_class_index(), piece.source_lo, piece.hi - piece.lo);
    }
  }
  const size_t new_slices = slices.size();
  const auto&  costs      = livehd::cprop_value::active->write_costs;
  const auto   cost       = costs.find(output.get_class_index());
  if (new_slices >= (cost == costs.end() ? 1 : cost->second)) {
    return false;
  }
  const auto                    old_inputs = node.inp_pins_snapshot();
  std::vector<hhds::Node_class> old_producers;
  for (auto input : old_inputs) {
    old_producers.push_back(input.get_driver_pin().get_master_node());
  }
  std::vector<Pack_lane>                          lanes;
  absl::flat_hash_map<Slice_key, hhds::Pin_class> emitted;
  for (const auto& piece : *pieces) {
    auto      value = piece.source;
    const int bits  = piece.hi - piece.lo;
    if (value.is_const()) {
      value = create_const(g, *const_of(value).get_mask_op_opt(piece.source_lo, piece.source_lo + bits));
    } else if (piece.source_lo != 0 || !fits_unsigned_window(value, bits)) {
      const Slice_key key{value.get_class_index(), piece.source_lo, bits};
      if (auto it = emitted.find(key); it != emitted.end()) {
        value = it->second;
      } else {
        auto get = livehd::cprop_value::make_get_mask(g, value, piece.source_lo, piece.source_lo + bits);
        value    = get.create_driver_pin(0);
        livehd::graph_util::set_ubits(value, bits);
        emitted.emplace(key, value);
      }
    }
    lanes.push_back({value, static_cast<int>(piece.lo), static_cast<int>(piece.hi)});
  }
  emit_concat(g, node, lanes);
  for (auto producer : old_producers) {
    sweep_dead_node(producer);
  }
  return true;
}

// An Or whose operands occupy DISJOINT constant bit windows: the classic
// `(a<<8) | (b<<4) | c` pack tree. An operand whose bit span cannot be pinned
// down refuses the whole rewrite.
//
// An upper bound from the explicit expression; unknown boundaries refuse.
[[nodiscard]] int pack_lane_width(const hhds::Pin_class& p) { return livehd::cprop_value::unsigned_width(p); }

bool canonicalize_or_pack(hhds::Graph& g, hhds::Node_class& node) {
  auto out = node.get_driver_pin(0);
  if (out.is_invalid()) {
    return false;
  }

  std::vector<Pack_lane>        lanes;
  std::vector<hhds::Node_class> shifts;
  int                           fan_in = 0;
  for (auto isnk : node.inp_sorted_pins()) {
    if (++fan_in > kConcatPackFanInLimit) {
      return false;
    }
    if (Ntype::sink_bank(Ntype_op::Or, isnk.get_port_id()) != 0) {
      return false;  // Or has ONE operand bank; anything else is not this shape
    }
    auto value = isnk.get_driver_pin();
    int  shift = 0;
    auto m     = value.get_master_node();
    if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL) {
      const int k = const_shl_amount(m);
      auto      x = drv_at(m, 0);
      if (k < 0 || x.is_invalid()) {
        return false;  // runtime shift amount: no constant window
      }
      value = x;
      shift = k;
      shifts.push_back(m);
    }
    // The window is [shift, shift + w): its LOW end starts at `shift` even when
    // the operand has no low bits of its own, because that is where the concat
    // lane has to start.
    const int w = pack_lane_width(value);
    if (w < 0) {
      return false;  // unbounded / signed operand: no provable window
    }
    if (w == 0) {
      continue;  // provably zero: contributes nothing to an Or
    }
    lanes.push_back(Pack_lane{value, shift, shift + w});
  }
  if (lanes.size() < 2) {
    return false;  // a 1-operand Or is a forward, handled by the scalar sweep
  }
  if (!tile_pack_lanes(g, lanes)) {
    return false;
  }

  emit_concat(g, node, lanes);
  for (auto& s : shifts) {
    sweep_dead_node(s);
  }
  return true;
}

// The Or arm is ON. It was never a correctness doubt -- `(a<<8)|(b<<4)|c ->
// Concat` was LEC-proven on the fixtures (including the u5-instance-output lane
// bug pack_lane_width now exists for) -- it was a CONSUMER gap, and one test
// named it exactly: //inou/prp:prp-sim-packed_bus_bit_ring, whose two modules
// exchange a packed bus each way and are only schedulable because
// inou/cgen/sim_color_plan.cpp proves that the bits of one bus depend on
// disjoint bits of the other and splits the def into slices. That matcher (and
// graph/split_selfref's false-loop breaker) read the Or/SHL/Get_mask SPELLING
// and had no Concat arm, so turning this on used to make `lhd sim` refuse the
// module outright ("fine-color dependency cycle remains after state and
// compact-loop carry cuts"). Both now decode concat_lanes() directly, which is
// what unblocked the flip; if either arm is ever removed, this goes back to
// false rather than growing a cprop-local guard -- inside `deva` the pack is
// plainly acyclic, and the ring exists only in the PARENT, at Sub port level.
//
// The Set_mask arm below never had that consumer and is on by measurement; it
// is also the larger population (1,943 chain heads vs 1,052 Or trees).
constexpr bool kOrPackEnabled = true;

// Consider each pack head once. Private Set_mask base links defer to their
// terminal consumer; shared versions use the persistent layout index.
bool canonicalize_concat_pack(hhds::Graph* g, hhds::Node_class& node) {
  if (node.is_invalid() || !node.has_out_edges()) {
    return false;
  }
  const auto op = type_op_of(node);
  if (op == Ntype_op::Set_mask) {
    return canonicalize_set_mask_pack(*g, node);
  } else if (kOrPackEnabled && op == Ntype_op::Or) {
    return canonicalize_or_pack(*g, node);
  }
  return false;
}

}  // namespace

// Absorb an associative PRIVATE region at its final consumer. Intermediate
// nodes defer flattening, so an N-link chain never copies operand lists of
// lengths 1, 2, ..., N. Shared and colored nodes are opaque boundaries.
void Cprop::collapse_forward_same_op(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  const auto op = type_op_of(node);
  if (has_single_consumer(node) && !livehd::graph_util::has_color(node)) {
    const auto sink = (*node.out_edges().begin()).sink;
    if (type_op_of(sink.get_master_node()) == op) {
      return;
    }
  }
  struct Operand {
    hhds::Pin_class pin;
    unsigned        bank;
  };
  std::vector<Operand> pending;
  for (const auto& sink : inp_edges_ordered) {
    pending.push_back({sink.get_driver_pin(), Ntype::sink_bank(op, sink.get_port_id())});
  }
  std::vector<hhds::Node_class>          absorbed;
  std::vector<Operand>                   leaves;
  absl::flat_hash_set<hhds::Class_index> owned{node.get_class_index()};
  while (!pending.empty()) {
    auto value = pending.back();
    pending.pop_back();
    auto child = value.pin.get_master_node();
    if (!value.pin.is_const() && !is_graph_input_pin(value.pin) && type_op_of(child) == op && has_single_consumer(child)
        && !livehd::graph_util::has_color(child)) {
      if (!owned.insert(child.get_class_index()).second) {
        return;  // cyclic region: leave it intact
      }
      absorbed.push_back(child);
      for (const auto& sink : child.inp_sorted_pins()) {
        pending.push_back(
            {sink.get_driver_pin(), op == Ntype_op::Sum ? value.bank ^ Ntype::sink_bank(op, sink.get_port_id()) : 0u});
      }
    } else {
      leaves.push_back(value);
    }
  }
  // A SHARED `x + c` operand stays a boundary above: copying a shared region
  // into every consumer is the quadratic splice this pass dropped. When this
  // Sum also has a literal, splicing just that one variable and its literals
  // is O(1) and folds them: `(m + 1) + 8` -> `m + 9` keeps the consumer off
  // the shared adder, and `(lo + 7) + 1 - lo` still cancels to 8.
  std::vector<hhds::Node_class> spliced;
  if (op == Ntype_op::Sum && std::ranges::any_of(leaves, [](const Operand& v) { return v.pin.is_const(); })) {
    const size_t nleaves = leaves.size();
    for (size_t i = 0; i < nleaves; ++i) {
      const auto pin = leaves[i].pin;
      if (pin.is_const() || is_graph_input_pin(pin)) {
        continue;
      }
      auto child = pin.get_master_node();
      if (type_op_of(child) != Ntype_op::Sum || livehd::graph_util::has_color(child) || owned.contains(child.get_class_index())) {
        continue;
      }
      Operand              variable{};
      int                  nvariables = 0;
      int                  ninputs    = 0;
      bool                 foldable   = true;
      std::vector<Operand> literals;
      for (const auto& sink : child.inp_sorted_pins()) {
        if (++ninputs > 3) {
          foldable = false;
          break;
        }
        const Operand operand{sink.get_driver_pin(), leaves[i].bank ^ Ntype::sink_bank(op, sink.get_port_id())};
        if (!operand.pin.is_const()) {
          variable = operand;
          ++nvariables;
        } else if (const_of(operand.pin).is_numeric()) {
          literals.push_back(operand);
        } else {
          foldable = false;  // a non-numeric literal does not fold
        }
      }
      if (!foldable || nvariables != 1 || literals.empty()) {
        continue;
      }
      leaves[i] = variable;
      leaves.insert(leaves.end(), literals.begin(), literals.end());
      if (std::ranges::find(spliced, child) == spliced.end()) {
        spliced.push_back(child);
      }
    }
  }
  if (absorbed.empty() && op != Ntype_op::Sum && op != Ntype_op::Xor) {
    return;
  }
  // Preserve multiplicity for arithmetic; cancel opposite Sum signs and XOR
  // parity through a hash lookup instead of scanning the consumer's bank.
  struct Term {
    hhds::Pin_class pin;
    int64_t         count = 0;
  };
  absl::flat_hash_map<hhds::Class_index, size_t> index;
  std::vector<Term>                              terms;
  for (const auto& value : leaves) {
    auto [it, fresh] = index.try_emplace(value.pin.get_class_index(), terms.size());
    if (fresh) {
      terms.push_back({value.pin, 0});
    }
    auto& count = terms[it->second].count;
    if (op == Ntype_op::And || op == Ntype_op::Or) {
      count = 1;
    } else if (op == Ntype_op::Xor) {
      count ^= 1;
    } else {
      count += op == Ntype_op::Sum && value.bank ? -1 : 1;
    }
  }
  clear_all_sinks(node);
  for (const auto& term : terms) {
    const unsigned bank = term.count < 0 ? 1 : 0;
    for (int64_t i = 0; i < std::abs(term.count); ++i) {
      livehd::graph_util::append_sink_operand(node, op, bank).connect_driver(term.pin);
    }
  }
  // Parent before child: each absorbed node has lost its sole consumer.
  for (auto child : absorbed) {
    livehd::cprop_value::retire(child);
  }
  // A shared operand read only through this Sum's own edges is dead now.
  for (auto child : spliced) {
    if (!child.has_out_edges()) {
      bwd_del_node(child);
    }
  }
  if (!node.has_inp_edges()) {
    replace_node(node, *Dlop::create_integer(op == Ntype_op::Mult ? 1 : 0));
    return;
  }
  inp_edges_ordered = ordered_inp_edges(node);
}

void Cprop::collapse_forward_always_pin0(hhds::Node_class& node, Inp_pins& inputs) {
  I(inputs.size() == 1);
  collapse_forward_for_pin(node, inputs.front().get_driver_pin());
}

// Redirect every consumer of node to a value-equivalent pin, then retire
// the node and its dead fan-in. Preserve the compact-loop multi-driver guard.
namespace {
// Do two driver pins carry the same value at the same width? Pin identity is the
// common case, but the constant pool mints a node per CONST pin, so two arms
// spelling the same literal are DIFFERENT pins with equal contents -- and an
// identical-arm select written as two literals is the shape that shows up most.
// same_repr (not is_known_eq) because a collapse rewires consumers straight to
// this pin: the width and the unknown plane have to match, not just the value.
[[nodiscard]] bool same_driver_value(const hhds::Pin_class& a, const hhds::Pin_class& b) {
  if (a == b) {
    return true;
  }
  if (!a.is_const() || !b.is_const()) {
    return false;
  }
  return livehd::graph_util::const_of(a).same_repr(livehd::graph_util::const_of(b));
}
}  // namespace

bool Cprop::collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin) {
  // SNAPSHOT ONCE. out_edges() is a lazy VIEW over live edge storage, and the
  // second loop below MUTATES (connect_sink -> add_edge). The old code walked
  // the live view while connecting, on the reasoning that "connect_sink only
  // grows new_dpin/sink storage, so the live walk stays valid" -- which does
  // not hold: an add_edge that spills an entry into overflow push_backs onto
  // Graph::overflow_sets(), and THAT vector reallocating dangles the raw
  // OverflowSet pointer and the borrowed set iterators the in-flight
  // OutEdgeIterator is walking. hhds's debug mutation guard aborts on it.
  // Snapshotting also stops this function walking the fanout twice.
  const auto                   out_view = node.out_edges();
  livehd::graph_util::Edge_vec outs(out_view.begin(), out_view.end());

  for (const auto& out : outs) {
    // Parallel-edge refusal: if new_dpin ALREADY drives this consumer sink,
    // the reconnect needs a second parallel edge -- and hhds overflow-mode
    // sink storage is a set that silently DEDUPS it, so the consumer would
    // lose one operand (Sum(a,a')=2a halves to a; Xor parity flips). And/Or
    // consumers are idempotent, so a dropped duplicate is value-neutral
    // there; refuse everywhere else.
    const auto consumer_op = type_op_of(out.sink.get_master_node());
    if (consumer_op != Ntype_op::And && consumer_op != Ntype_op::Or) {
      for (const auto& d : out.sink.get_driver_pins()) {
        if (d == new_dpin) {
          return false;
        }
      }
    }
  }

  livehd::cprop_value::forwarded(node, new_dpin);
  // Redirect every consumer to new_dpin, then bwd_del_node deletes the node in
  // one shot (del_node bulk-drops its edges — no per-edge find).
  for (const auto& out : outs) {
    new_dpin.connect_sink(out.sink);
  }

  bwd_del_node(node);
  return true;
}

bool Cprop::try_constant_prop(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto& e : inp_edges_ordered) {
    n_inputs++;
    if (!e.get_driver_pin().is_const()) {
      continue;
    }
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant >= 1) {
    replace_part_inputs_const(node, inp_edges_ordered);
    return true;
  }

  return false;
}

void Cprop::try_collapse_forward(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  auto op = type_op_of(node);

  if (inp_edges_ordered.size() == 1) {
    auto       prev_op      = type_op_of(inp_edges_ordered[0].get_driver_pin().get_master_node());
    // A single-input Sum whose lone driver is on the SUBTRACT (b) pin is a
    // negation (0 - x = -x). collapse_forward_always_pin0 forwards the driver
    // UNCHANGED, which would drop the sign (turning -x into +x). Leave the Sum
    // node so cgen renders it as -(x).
    const bool sum_subtract = op == Ntype_op::Sum && sink_pin_name(inp_edges_ordered[0]) == "bs";
    if ((op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
         || op == Ntype_op::Xor)
        && !sum_subtract) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
    if (prev_op == Ntype_op::Get_mask) {
      if (op == Ntype_op::Get_mask) {
        collapse_forward_always_pin0(node, inp_edges_ordered);
        return;
      }
    }
  }

  if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
    // Identical arms make the selector irrelevant, for a Hotmux as much as for a
    // Mux: an overlap that cannot change the result is not worth a decode cone
    // plus its own ABC region. (That shape is exactly an always-open latch
    // enable -- `case(c) 2'd0: e<=1; default: e<=1;` -- and keeping the Hotmux
    // alive to carry a one-hot obligation about it left the latch in the design
    // on the reference side only.) The obligation is dropped with the cell.
    if (inp_edges_ordered.size() <= 1) {
      livehd::cprop_value::retire(node);
      return;
    }
    if (op == Ntype_op::Mux) {
      auto a_pin = inp_edges_ordered[1].get_driver_pin();
      for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
        if (!same_driver_value(a_pin, inp_edges_ordered[i].get_driver_pin())) {
          return;
        }
      }
      collapse_forward_for_pin(node, a_pin);
      return;
    }
    const auto inputs = livehd::graph_util::hotmux_inputs(node);
    if (inputs.arms.empty()) {
      return;
    }
    const auto a_pin = inputs.arms[0].second;
    for (const auto& arm : inputs.arms) {
      if (!same_driver_value(a_pin, arm.second)) {
        return;
      }
    }
    // The all-controls-zero result has to agree too. It is the trailing default
    // when the cell has one, and a literal 0 when it does not -- so a Hotmux
    // with no default port collapses only when the shared arm value IS zero.
    if (inputs.fallback.is_invalid()) {
      if (!a_pin.is_const() || !const_of(a_pin).is_known_zero()) {
        return;
      }
    } else if (!same_driver_value(a_pin, inputs.fallback)) {
      return;
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Mux) {
    auto s_pin = inp_edges_ordered[0].get_driver_pin();
    if (!s_pin.is_const()) {
      return;
    }
    const auto& s_const = const_of(s_pin);
    if (s_const.has_unknowns()) {
      return;
    }

    const bool binary = is_two_arm_mux(inp_edges_ordered);
    if (!s_const.is_numeric() || (!binary && !s_const.is_just_i64())) {
      return;
    }
    // A two-arm mux takes a condition: every nonzero value selects arm 1.
    size_t sel = binary ? !s_const.is_known_zero() : s_const.to_just_i64();

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.get_port_id() == 0) {
        continue;
      }
      if (e.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        a_pin = e.get_driver_pin();
        break;
      }
    }

    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: mux selector:{} for a disconnected pin in mux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Hotmux) {
    const auto      inputs = livehd::graph_util::hotmux_inputs(node);
    hhds::Pin_class selected;
    for (const auto& [control, value] : inputs.arms) {
      if (!control.is_const() || const_of(control).has_unknowns()) {
        return;
      }
      if (!const_of(control).is_known_zero()) {
        if (!selected.is_invalid()) {
          return;  // Preserve overlapping controls for pass.formal/runtime checks.
        }
        selected = value;
      }
    }
    if (selected.is_invalid()) {
      selected = inputs.fallback.is_invalid() ? create_const(*current_graph, *Dlop::create_integer(0)) : inputs.fallback;
    }
    collapse_forward_for_pin(node, selected);
  } else if (op == Ntype_op::EQ) {
    // FIXME: 1- eq(X,0) = not(ror(x))
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    hhds::Pin_class first_const_edge;
    int             nconstants = 0;
    int             npending   = 0;

    // Seed the accumulator with the op's identity (0 for Sum/Or, -1 for And).
    // A default Dlop is Invalid, which used to act as the additive identity but
    // now propagates to nil (hlop's non-numeric guard) — poisoning the fold.
    Dlop result;
    result = Dlop::create_integer(op == Ntype_op::And ? -1 : 0);

    // A non-numeric constant operand (a String) turns every fold below into
    // nil, which is not a value: bail BEFORE any constant edge is deleted.
    for (const auto& i : inp_edges_ordered) {
      if (i.get_driver_pin().is_const() && !const_of(i.get_driver_pin()).is_numeric()) {
        return;
      }
    }
    Cprop::Inp_pins edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!i.get_driver_pin().is_const()) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }

      const auto& c = const_of(i.get_driver_pin());

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (sink_pin_name(i) == "as") {
          result = result.add_op(c);
        } else {
          I(sink_pin_name(i) == "bs");
          result = result.sub_op(c);
        }
      } else if (op == Ntype_op::Or) {
        result = result.or_op(c);
      } else if (op == Ntype_op::Xor) {
        result = result.xor_op(c);
      } else {
        I(op == Ntype_op::And);
        result = result.and_op(c);
      }

      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_sink();
      }
    }

    if (op == Ntype_op::And && result.is_known_zero()) {
      // `x & 0` is 0 for EVERY x: a zero fold is And's ANNIHILATOR, not its
      // identity. Sharing the "zero result => drop the constants and forward
      // the remaining operand" path below (which is right for Sum's 0 and Or's
      // 0) silently deleted the `& 0` and returned x — `(~&sb) & (~|3'b101)`
      // emitted the bare comparison where the answer is a constant 0. A SINGLE
      // constant 0 annihilates just the same (it used to fall through every
      // branch here and survive to codegen). replace_node, not collapse: it
      // width-adjusts the constant per consumer instead of refusing.
      replace_node(node, result);
    } else if (op == Ntype_op::Or && result.is_just_i64() && result.to_just_i64() == -1) {
      // Or's annihilator: x | -1 is the all-ones -1 for EVERY x.
      replace_node(node, result);
    } else if (nconstants > 1) {
      first_const_edge.del_sink();
      if (!result.is_known_zero()) {
        if (op == Ntype_op::Sum && !result.is_positive()) {
          // `result` is the SIGNED fold (add_op for `as`, sub_op for `bs`), so a
          // negative result is already "minus that much". The `bs` sink negates
          // whatever it is given, so it must receive the MAGNITUDE — handing it
          // the negative value negated it a second time and flipped the sign of
          // the whole constant term: `0 - ua - 5` folded to result=-5, went to
          // `bs`, and the node computed `-ua + 5`.
          Dlop zero;
          zero = Dlop::create_integer(0);
          Dlop mag;
          mag = zero.sub_op(result);
          setup_sink_by_name(node, "bs").connect_driver(create_const(*current_graph, mag));
        } else {
          // Or/And/Xor have no subtract sink, so their fold always joins `as`.
          setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, result));
        }
      } else if (npending == 1 && !(op == Ntype_op::Sum && sink_pin_name(edge_it2[0]) == "bs")) {
        // Same guard as the nconstants==0 case below: a lone pending operand on a
        // Sum's subtract (b) pin is `0 - x` = -x, so forwarding x unchanged would
        // drop the sign. Leave the Sum node (it now holds only the b driver).
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (nconstants == 1 && npending >= 1
               && ((op == Ntype_op::And && result.is_just_i64() && result.to_just_i64() == -1)
                   || ((op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Sum) && result.is_known_zero()))) {
      // Identity element: and(x.., -1) == and(x..), or(x.., 0) == or(x..),
      // xor(x.., 0) == xor(x..), sum(x.., +0) == sum(x..). Dropping it matters
      // for codegen too: cgen renders -1 as `1'sh1`, which only sign-extends in
      // an all-signed Verilog expression — in a mixed/unsigned context it reads
      // as +1 and masks everything away. (Sum's lone `+0` used to fall through
      // every branch here — the nconstants>1 fold path needs two constants —
      // so `add(add(0,a),b)` survived all the way into the generated sim C++.)
      first_const_edge.del_sink();
      if (npending == 1 && !(op == Ntype_op::Sum && sink_pin_name(edge_it2[0]) == "bs")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 0 && nconstants == 1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    } else if (npending == 1 && nconstants == 0) {
      if (!(op == Ntype_op::Sum && sink_pin_name(edge_it2[0]) == "bs")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    }
  } else if (op == Ntype_op::Mult) {
    hhds::Pin_class first_const_edge;
    int             nconstants = 0;
    int             npending   = 0;

    Dlop result;
    result = Dlop::create_integer(1);  // multiplicative identity

    // A non-numeric constant operand (a String) turns every fold below into
    // nil, which is not a value: bail BEFORE any constant edge is deleted.
    for (const auto& i : inp_edges_ordered) {
      if (i.get_driver_pin().is_const() && !const_of(i.get_driver_pin()).is_numeric()) {
        return;
      }
    }
    Cprop::Inp_pins edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!i.get_driver_pin().is_const()) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }
      const auto& c = const_of(i.get_driver_pin());
      ++nconstants;
      result = result.mult_op(c);
      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_sink();
      }
    }
    I(nconstants >= 1 && npending >= 1);  // try_constant_prop routes all-const to replace_all

    if (result.is_known_zero()) {
      replace_node(node, result);  // x * 0 == 0: annihilator (width-adjusts per consumer)
    } else if (result.is_just_i64() && result.to_just_i64() == 1) {
      // Multiplicative identity: drop the constant.
      first_const_edge.del_sink();
      if (npending == 1) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 1 && result.is_just_i64() && result.to_just_i64() > 1
               && (result.to_just_i64() & (result.to_just_i64() - 1)) == 0) {
      // Strength reduction: Mult(x, 2^k) -> SHL(x, k). Exact for any sign of x
      // at unlimited precision.
      //
      // The surviving operand must be MOVED to pid 0. Mult's operand bank
      // spends one pid PER operand (graph/cell.hpp), so `x` may sit on pid 1,
      // 2, ... -- and pid 1 is exactly SHL's shift-AMOUNT sink. Leaving it
      // there and wiring the amount produced `x << x`. Rebuild both sinks from
      // scratch instead of assuming any pid survives the retype.
      const int64_t v = result.to_just_i64();
      int           k = 0;
      while ((v >> k) != 1) {
        ++k;
      }
      auto x_pin = edge_it2[0].get_driver_pin();  // the single non-constant operand
      clear_all_sinks(node);
      livehd::graph_util::set_type_op(node, Ntype_op::SHL);
      setup_sink_by_name(node, "a").connect_driver(x_pin);
      setup_sink_by_name(node, "b").connect_driver(create_const(*current_graph, *Dlop::create_integer(k)));
    } else if (nconstants > 1) {
      // Reattach the folded product as the one surviving constant.
      first_const_edge.del_sink();
      setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, result));
    }
  } else if (op == Ntype_op::SRA) {
    auto amt_pin = inp_edges_ordered[1].get_driver_pin();
    if (amt_pin.is_known_false()) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].get_driver_pin());
    }
  } else if (op == Ntype_op::SHL) {
    if (inp_edges_ordered.size() == 2) {
      auto amt_pin = inp_edges_ordered[1].get_driver_pin();
      if (amt_pin.is_known_false()) {
        collapse_forward_for_pin(node, inp_edges_ordered[0].get_driver_pin());
      }
    }
  }
}

void Cprop::replace_all_inputs_const(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::SHL) {
    // SHL b is single-driver (the one-hot multi-shift form was removed).
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto b_pin = livehd::graph_util::get_driver_of_sink_name(node, "b");
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop val = const_of(a_pin);
    Dlop amt = const_of(b_pin);

    Dlop result;
    result = Dlop::create_integer(0);
    result = result.or_op(val.shl_op(amt));  // val << amt
    replace_node(node, result);

  } else if (op == Ntype_op::Ror) {
    Dlop result;
    result = Dlop::create_integer(0);
    for (auto& i : inp_edges_ordered) {
      const auto& c = const_of(i.get_driver_pin());
      result        = result.ror_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
    const auto input    = livehd::graph_util::get_driver_of_sink_name(node, "a");
    const auto selected = const_of(input).get_mask_op_opt(0, livehd::graph_util::reduction_count(node));
    replace_node(node, op == Ntype_op::Rxor ? selected->rxor_op() : selected->popcount_op());
  } else if (op == Ntype_op::Set_mask) {
    auto a_pin     = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");

    if (a_pin.is_invalid()) {
      return;
    }
    Dlop val = const_of(a_pin);

    if (!mask_pin.is_invalid() && !value_pin.is_invalid()) {
      const auto& mask  = const_of(mask_pin);
      const auto& value = const_of(value_pin);
      replace_node(node, livehd::eval_set_mask(val, mask, value));
    } else {
      replace_node(node, val);
    }
  } else if (op == Ntype_op::Sum) {
    Dlop result;
    result = Dlop::create_integer(0);  // additive identity (Invalid no longer folds as 0)
    for (auto& i : inp_edges_ordered) {
      const auto& c = const_of(i.get_driver_pin());
      // EVEN pid = added, ODD = subtracted; one pid per operand (cell.hpp).
      if (Ntype::sink_bank(Ntype_op::Sum, i.get_port_id()) == 0) {
        result = result.add_op(c);
      } else {
        result = result.sub_op(c);
      }
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Dlop result;
    result = Dlop::create_integer(0);  // or identity (Invalid no longer folds as 0)
    for (auto& e : inp_edges_ordered) {
      const auto& c = const_of(e.get_driver_pin());
      result        = result.or_op(c);
    }

    replace_logic_node(node, result);

  } else if (op == Ntype_op::And) {
    Dlop result;
    result = Dlop::create_integer(-1);
    for (auto& i : inp_edges_ordered) {
      const auto& c = const_of(i.get_driver_pin());
      result        = result.and_op(c);
    }

    replace_node(node, result);

  } else if (op == Ntype_op::EQ) {
    // EQ is a single-port, multi-edge "all drivers on port a are equal" cell.
    // When both comparator operands resolve to the SAME driver pin (e.g.
    // const==const), HHDS collapses the duplicate driver->sink edges into one,
    // leaving a single input edge. A 1-input "all-equal" is trivially true, and
    // the loop below already yields that (it starts at index 1), so no >1
    // assertion is warranted (try_constant_prop only calls this with n>=1).
    //
    // Dlop::eq_op is THREE-valued: a known/known bit mismatch decides `false`,
    // all-agree decides `true`, and an otherwise-agreeing pair with unknown bits
    // is UNDECIDABLE (unknown_bool). Collapsing that third case with
    // `!is_known_false()` folded `0ub0? == 0` to a definite 1 — which then made
    // `c != 0` a definite 0 and comptime-DECIDED the enclosing if/match arm (the
    // `match` form went further: two arms both folding to 1 built a non-one-hot
    // Hotmux selector, which the unique-if check then reported as overlapping).
    // Leave the cell alone instead — the same three-valued discipline the
    // LT/GT fold below already follows. A DEFINITE mismatch still folds: it
    // decides the all-equal regardless of unknown bits elsewhere.
    const auto& first   = const_of(inp_edges_ordered[0].get_driver_pin());
    bool        eq      = true;
    bool        any_unk = false;
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      const auto& c = const_of(inp_edges_ordered[i].get_driver_pin());
      auto        r = first.eq_op(c);
      if (r->is_known_false()) {
        eq = false;
        break;
      }
      any_unk = any_unk || r->has_unknowns();
    }
    if (eq && any_unk) {
      return;  // undecidable on ?-bits: keep the node
    }

    Dlop result;
    result = Dlop::create_integer(eq ? 1 : 0);

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    const auto& sel_const = const_of(inp_edges_ordered[0].get_driver_pin());
    const bool  binary    = is_two_arm_mux(inp_edges_ordered);
    if (!sel_const.is_numeric() || sel_const.has_unknowns() || (!binary && !sel_const.is_just_i64())) {
      return;  // unknown-bit selector (0sb? poison cond): keep the mux as-is
    }

    size_t sel = binary ? !sel_const.is_known_zero() : sel_const.to_just_i64();

    Dlop result;
    for (auto& e : inp_edges_ordered) {
      if (e.get_port_id() == 0) {
        continue;
      }
      if (e.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        result = const_of(e.get_driver_pin());
        break;
      }
    }

    if (result.get_signed_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: mux:{} selector:{} goes for disconnected pin in mux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Hotmux) {
    replace_part_inputs_const(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult) {
    Dlop result;
    result = Dlop::create_integer(1);
    for (auto& i : inp_edges_ordered) {
      const auto& c = const_of(i.get_driver_pin());
      result        = result.mult_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Div) {
    I(inp_edges_ordered.size() == 2);
    Dlop a = const_of(inp_edges_ordered[0].get_driver_pin());
    Dlop b = const_of(inp_edges_ordered[1].get_driver_pin());

    auto result = a.div_op(b);

    replace_node(node, result);
  } else if (op == Ntype_op::Rem) {
    I(inp_edges_ordered.size() == 2);
    Dlop a = const_of(inp_edges_ordered[0].get_driver_pin());
    Dlop b = const_of(inp_edges_ordered[1].get_driver_pin());

    auto result = a.rem_op(b);  // truncated remainder; invalid on rem-by-zero

    replace_node(node, result);
  } else if (op == Ntype_op::Not) {
    if (inp_edges_ordered.size() != 1) {
      return;
    }
    replace_node(node, const_of(inp_edges_ordered[0].get_driver_pin()).not_op());
  } else if (op == Ntype_op::Xor) {
    Dlop result;
    result = Dlop::create_integer(0);
    for (auto& e : inp_edges_ordered) {
      result = result.xor_op(const_of(e.get_driver_pin()));
    }
    if (std::getenv("LHD_DBG_XOR")) {
      std::fprintf(stderr, "DBG xor nid=%llu n=%zu ->", (unsigned long long)node.get_debug_nid(), inp_edges_ordered.size());
      for (auto& e : inp_edges_ordered) {
        std::fprintf(stderr, " [pid=%u v=%s]", (unsigned)e.get_port_id(), const_of(e.get_driver_pin()).to_pyrope().c_str());
      }
      std::fprintf(stderr, " = %s\n", result.to_pyrope().c_str());
    }
    replace_logic_node(node, result);
  } else if (op == Ntype_op::SRA) {
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto b_pin = livehd::graph_util::get_driver_of_sink_name(node, "b");
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop amt = const_of(b_pin);
    // is_just_i64 also rejects >62-bit amounts, whose sra_op yields nil.
    if (amt.has_unknowns() || amt.is_negative() || !amt.is_just_i64()) {
      return;
    }
    replace_node(node, const_of(a_pin).sra_op(amt));
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    // Each side of a compare is an operand BANK that may hold several slots
    // (one per operand), and a bank REDUCES BY SUM -- "Sum and both comparison
    // sides sum their operands independently", the rule the old
    // connect_folded_const folded duplicate constants under. Every operand here
    // is a constant (replace_all_inputs_const's precondition), so reduce each
    // bank and compare the two sums.
    //
    // Folding this shape is not an optimization, it is the only agreed reading
    // of it: the downstream consumers do NOT agree on a multi-operand compare
    // bank (pass/abc's blaster takes the last operand of each side, cgen emits
    // the pairwise cross-product), so an all-constant one must never reach
    // them. A bank with a NON-constant operand cannot get here at all.
    Dlop a_sum, b_sum;
    a_sum      = Dlop::create_integer(0);
    b_sum      = Dlop::create_integer(0);
    bool has_a = false, has_b = false;
    for (auto& e : inp_edges_ordered) {
      const bool is_b        = Ntype::sink_bank(type_op_of(node), e.get_port_id()) != 0;
      (is_b ? b_sum : a_sum) = (is_b ? b_sum : a_sum).add_op(const_of(e.get_driver_pin()));
      (is_b ? has_b : has_a) = true;
    }
    if (!has_a || !has_b || a_sum.is_invalid() || b_sum.is_invalid() || a_sum.is_nil() || b_sum.is_nil()) {
      return;
    }
    Dlop a   = a_sum;
    Dlop b   = b_sum;
    auto cmp = op == Ntype_op::LT ? a.lt_op(b) : a.gt_op(b);
    if (cmp->has_unknowns()) {
      return;  // three-valued compare on ?-bits: keep the node
    }
    Dlop result;
    result = Dlop::create_integer(cmp->is_known_true() ? 1 : 0);
    replace_node(node, result);
  } else if (op == Ntype_op::Get_mask) {
    auto a_pin    = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    if (a_pin.is_invalid() || mask_pin.is_invalid()) {
      return;
    }
    Dlop a    = const_of(a_pin);
    Dlop mask = const_of(mask_pin);
    if (mask.is_negative()) {
      // The -1 to-positive idiom: identity on a non-negative value only.
      if (mask.is_just_i64() && mask.to_just_i64() == -1 && a.is_positive()) {
        replace_node(node, a);
      }
      return;
    }
    replace_node(node, livehd::eval_get_mask(a, mask));
  } else if (op == Ntype_op::Concat) {
    // Every lane VALUE is constant, so the whole assembly is one non-negative
    // sum(w_i)-bit constant.
    //
    // The lane table MUST come from graph_util::concat_lanes, never from a hand
    // walk of inp_edges: the width lives on the ODD sink pids and a lane's
    // window is not recoverable from its driver, so a decoder that re-derived
    // widths would shift every lane above the one it got wrong. An empty table
    // means the cell is malformed and must fail CLOSED -- folding it as "zero
    // lanes" would put a constant 0 where a real value was due.
    const auto lanes = livehd::graph_util::concat_lanes(node);
    if (lanes.empty()) {
      return;
    }
    // Fixed-size (no push_back): the Dlop::Concat_lane span below holds RAW
    // pointers into this storage, so it must not reallocate.
    std::vector<Dlop> values(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
      if (!lanes[i].value.is_const()) {
        return;  // a width operand is always const, so "all inputs constant"
                 // does NOT imply every lane VALUE is one
      }
      values[i]   = const_of(lanes[i].value);
      const int w = lanes[i].width;
      // Dlop::concat_op DEBUG-asserts that a lane fits its window -- an
      // over-wide lane is a caller bug there, even though
      // the CELL truncates it. get_signed_bits() over-approximates once the sign bit
      // itself is unknown, so this refuses slightly more than the assert would:
      // the safe direction, since an unfolded Concat still computes.
      if (values[i].get_payload_bits() > w) {
        return;
      }
    }
    std::vector<Dlop::Concat_lane> hl;
    hl.reserve(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
      // The n-ary form is the ONLY correct one here. The binary member
      // concat_op(other) sizes the low operand by its SIGNIFICANT bits, which
      // is precisely the width a concat may not be sized by (and it cannot
      // express `{a, b}` at all).
      hl.push_back(Dlop::Concat_lane{&values[i], lanes[i].width});
    }
    auto result = Dlop::concat_op(std::span<const Dlop::Concat_lane>(hl.data(), hl.size()));
    if (!result->is_integer()) {
      return;  // nil: a non-numeric lane has no bit window -- do not fold
    }
    replace_node(node, *result);
  } else {
#ifndef NDEBUG
    Pass::info("FIXME: cprop still does not copy prop node:{}\n", debug_name(node));
#endif
  }
}

void Cprop::replace_node(hhds::Node_class& node, const Dlop& result) {
  if (result.is_invalid() || result.is_nil()) {
    // Invalid/Nil are not values (division by zero, illegal shifts, etc.).
    return;
  }
  auto dpin = create_const(*current_graph, result);
  livehd::cprop_value::forwarded(node, dpin);
  for (const auto& sink : consumer_sinks(node)) {
    livehd::graph_util::connect_folded_const(*current_graph, dpin, sink);
  }
  livehd::cprop_value::retire(node);
}

void Cprop::replace_logic_node(hhds::Node_class& node, const Dlop& result) {
  if (result.is_invalid() || result.is_nil()) {
    return;
  }
  auto dpin = create_const(*current_graph, result);
  livehd::cprop_value::forwarded(node, dpin);
  for (const auto& sink : consumer_sinks(node)) {
    livehd::graph_util::connect_folded_const(*current_graph, dpin, sink);
  }
  livehd::cprop_value::retire(node);
}

bool Cprop::scalar_mux(hhds::Node_class& node, Inp_pins& inp_edges_ordered, bool factor_packs) {
  if (inp_edges_ordered.size() != 3) {
    return false;
  }

  if (inp_edges_ordered[1].get_driver_pin() == inp_edges_ordered[2].get_driver_pin()) {
    return collapse_forward_for_pin(node, inp_edges_ordered[1].get_driver_pin());
  }

  // If both arms update the same packed lane of the same base, select only
  // their lane values. This is the reset-over-enable sibling of the one-arm
  // factoring below and is what removes the final word-wide Mux in a nested
  // `if (reset) bit=R; else if (enable) bit=D;` update.
  auto factor_set_mask_pair = [&]() {
    const auto sel = inp_edges_ordered[0].get_driver_pin();
    auto       sm0 = inp_edges_ordered[1].get_driver_pin().get_master_node();
    auto       sm1 = inp_edges_ordered[2].get_driver_pin().get_master_node();
    if (sel.is_invalid() || sm0.is_invalid() || sm1.is_invalid() || type_op_of(sm0) != Ntype_op::Set_mask
        || type_op_of(sm1) != Ntype_op::Set_mask) {
      return false;
    }
    const auto base0  = livehd::graph_util::get_driver_of_sink_name(sm0, "a");
    const auto base1  = livehd::graph_util::get_driver_of_sink_name(sm1, "a");
    const auto mask0  = livehd::graph_util::get_driver_of_sink_name(sm0, "mask");
    const auto value0 = livehd::graph_util::get_driver_of_sink_name(sm0, "value");
    const auto value1 = livehd::graph_util::get_driver_of_sink_name(sm1, "value");
    const auto range0 = const_mask_range(sm0);
    const auto range1 = const_mask_range(sm1);
    const int  width  = range0.second - range0.first;
    if (!same_pin(base0, base1) || mask0.is_invalid() || value0.is_invalid() || value1.is_invalid() || range0.first < 0
        || range0 != range1 || width <= 0) {
      return false;
    }
    auto lane_mux = livehd::cprop_value::make_node(*current_graph, Ntype_op::Mux);
    livehd::graph_util::setup_sink_pid(lane_mux, 0).connect_driver(sel);
    livehd::graph_util::setup_sink_pid(lane_mux, 1).connect_driver(value0);
    livehd::graph_util::setup_sink_pid(lane_mux, 2).connect_driver(value1);
    auto lane = lane_mux.create_driver_pin(0);

    clear_all_sinks(node);
    livehd::graph_util::set_type_op(node, Ntype_op::Set_mask);
    livehd::graph_util::connect_mask_operands(node, base0, mask0, lane);
    normalize_emitted(lane_mux);
    if (!sm0.has_out_edges()) {
      bwd_del_node(sm0);
    }
    if (!sm1.has_out_edges()) {
      bwd_del_node(sm1);
    }
    return true;
  };
  if (factor_packs && factor_set_mask_pair()) {
    return true;
  }

  // Keep a conditional packed-lane update narrow:
  //
  //   mux(s, base, set_mask(base, mask, value))
  //     -> set_mask(base, mask, mux(s, get_mask(base, mask), value))
  //
  // (and the symmetric false-arm form). Slang emits this shape for every
  // conditional assignment to one bit of a packed register. Leaving the Mux
  // outside makes ABC select the ENTIRE packed word once per written bit -- a
  // 256-bit LRU matrix with 120 state bits became an 80k-GE serial cone. The
  // Set_mask and Get_mask are constant-mask wiring in pass.abc; after this
  // rewrite only the selected lane crosses Boolean mapping.
  auto factor_set_mask_arm = [&](size_t updated_idx, size_t base_idx) {
    const auto sel     = inp_edges_ordered[0].get_driver_pin();
    const auto updated = inp_edges_ordered[updated_idx].get_driver_pin();
    const auto base    = inp_edges_ordered[base_idx].get_driver_pin();
    if (sel.is_invalid() || updated.is_invalid() || base.is_invalid()) {
      return false;
    }
    auto sm = updated.get_master_node();
    if (sm.is_invalid() || type_op_of(sm) != Ntype_op::Set_mask) {
      return false;
    }
    const auto sm_base = livehd::graph_util::get_driver_of_sink_name(sm, "a");
    const auto mask    = livehd::graph_util::get_driver_of_sink_name(sm, "mask");
    const auto value   = livehd::graph_util::get_driver_of_sink_name(sm, "value");
    const auto range   = const_mask_range(sm);
    const int  width   = range.second - range.first;
    if (!same_pin(sm_base, base) || mask.is_invalid() || value.is_invalid() || range.first < 0 || width <= 0) {
      return false;
    }
    auto get = livehd::cprop_value::make_node(*current_graph, Ntype_op::Get_mask);
    livehd::graph_util::connect_mask_operands(get, base, mask);
    auto old_lane = get.create_driver_pin(0);
    livehd::graph_util::set_ubits(old_lane, width);

    auto lane_mux = livehd::cprop_value::make_node(*current_graph, Ntype_op::Mux);
    livehd::graph_util::setup_sink_pid(lane_mux, 0).connect_driver(sel);
    if (updated_idx == 2) {
      livehd::graph_util::setup_sink_pid(lane_mux, 1).connect_driver(old_lane);
      livehd::graph_util::setup_sink_pid(lane_mux, 2).connect_driver(value);
    } else {
      livehd::graph_util::setup_sink_pid(lane_mux, 1).connect_driver(value);
      livehd::graph_util::setup_sink_pid(lane_mux, 2).connect_driver(old_lane);
    }
    auto lane = lane_mux.create_driver_pin(0);

    clear_all_sinks(node);
    livehd::graph_util::set_type_op(node, Ntype_op::Set_mask);
    livehd::graph_util::connect_mask_operands(node, base, mask, lane);
    normalize_emitted(get);
    normalize_emitted(lane_mux);
    if (!sm.has_out_edges()) {
      bwd_del_node(sm);
    }
    return true;
  };
  if (factor_packs && (factor_set_mask_arm(2, 1) || factor_set_mask_arm(1, 2))) {
    return true;
  }

  if (is_two_arm_mux(inp_edges_ordered)) {
    const auto selector  = inp_edges_ordered[0].get_driver_pin();
    const auto condition = decode_bool_condition(selector);
    if (condition && condition->base != selector && !condition->base.is_const() && is_bool01(condition->base)) {
      const auto arm0 = inp_edges_ordered[1].get_driver_pin(), arm1 = inp_edges_ordered[2].get_driver_pin();
      clear_all_sinks(node);
      livehd::graph_util::setup_sink_pid(node, 0).connect_driver(condition->base);
      livehd::graph_util::setup_sink_pid(node, 1).connect_driver(condition->true_when_base ? arm0 : arm1);
      livehd::graph_util::setup_sink_pid(node, 2).connect_driver(condition->true_when_base ? arm1 : arm0);
      const auto old = selector.get_master_node();
      if (!livehd::graph_util::is_builtin_node(old) && !old.has_out_edges()) {
        sweep_dead_node(old);
      }
      inp_edges_ordered = ordered_inp_edges(node);
    }
  }

  // Constant 0/1 arms are a boolean materialization of the selector itself.
  if (inp_edges_ordered[1].get_driver_pin().is_const() && inp_edges_ordered[2].get_driver_pin().is_const()) {
    const auto& c0 = const_of(inp_edges_ordered[1].get_driver_pin());
    const auto& c1 = const_of(inp_edges_ordered[2].get_driver_pin());
    if (!c0.has_unknowns() && !c1.has_unknowns()) {
      const bool  zero0 = c0.is_known_zero();
      const bool  one1  = c1.is_just_i64() && c1.to_just_i64() == 1;
      const auto& sel   = inp_edges_ordered[0].get_driver_pin();
      // Mux(s, 0, 1) == s -- only when s is already a 0/1 VALUE (the cell
      // treats any nonzero s as true, so a wide s must keep the mux).
      // The inverted-arm sibling Mux(s,1,0) -> EQ(s,0) was measured at only
      // ~67 sites and its EQ spelling gets inlined into latch enable guards
      // (churning the pinned canonical emission), so it is deliberately NOT
      // rewritten.
      if (zero0 && one1 && is_bool01(sel) && collapse_forward_for_pin(node, sel)) {
        return true;
      }
    }
  }

  // One constant arm over 0/1 values is plain logic:
  //   s ? 1 : f -> s | f          s ? t : 0 -> s & t
  //   s ? 0 : f -> !s & f         s ? t : 1 -> !s | t
  // A per-bit `if (rst) b = 1; else if (en) b = d;` nest and the enable
  // chain tolg builds next to it are exactly this shape. As a Mux tree they
  // reach mux sharing, which rebuilds them as a Hotmux over path conjunctions
  // (an LRU bit matrix grew ~3x). As And/Or they flatten and meet
  // scalar_bool. Both-constant arms keep the rules above (see Mux(s,1,0)).
  //
  // A latch Q arm stays a Mux: it is the latch's hold arm, and the latch
  // contract exempts Q only as a DIRECT Mux data arm. `s ? 1 : q` as `s | q`
  // is a Q -> D path through logic, rejected as a transparent self-update.
  if (is_two_arm_mux(inp_edges_ordered)) {
    const auto sel       = inp_edges_ordered[0].get_driver_pin();
    const auto f         = inp_edges_ordered[1].get_driver_pin();
    const auto t         = inp_edges_ordered[2].get_driver_pin();
    const auto k         = t.is_const() ? t : f;
    const auto other     = t.is_const() ? f : t;
    const bool one_arm   = f.is_const() != t.is_const();
    const bool latch_arm = one_arm && !is_graph_input_pin(other) && type_op_of(other.get_master_node()) == Ntype_op::Latch;
    if (one_arm && !latch_arm && !sel.is_const() && is_bool01(sel) && is_bool01(other) && !const_of(k).has_unknowns()
        && const_of(k).is_just_i64() && (const_of(k).to_just_i64() == 0 || const_of(k).to_just_i64() == 1)) {
      const bool k_one   = const_of(k).to_just_i64() == 1;
      const bool negated = t.is_const() ? !k_one : k_one;
      auto       literal = sel;
      if (negated) {
        auto sel_master = sel.get_master_node();
        auto inner      = sel_master.is_invalid() ? hhds::Pin_class{} : eq_against_const(sel_master, 0);
        if (!inner.is_invalid() && !inner.is_const() && is_bool01(inner)) {
          literal = inner;  // !(y == 0) is y for y in {0,1}
        } else {
          auto eq = make_node(*current_graph, Ntype_op::EQ);
          livehd::graph_util::append_sink_operand(eq, Ntype_op::EQ, 0).connect_driver(sel);
          livehd::graph_util::append_sink_operand(eq, Ntype_op::EQ, 0)
              .connect_driver(create_const(*current_graph, *Dlop::create_integer(0)));
          literal = eq.create_driver_pin(0);
          livehd::graph_util::set_ubits(literal, 1);
        }
      }
      const auto new_op = k_one ? Ntype_op::Or : Ntype_op::And;
      clear_all_sinks(node);
      livehd::graph_util::set_type_op(node, new_op);
      livehd::graph_util::append_sink_operand(node, new_op, 0).connect_driver(literal);
      livehd::graph_util::append_sink_operand(node, new_op, 0).connect_driver(other);
      inp_edges_ordered = ordered_inp_edges(node);
      return false;  // retyped: scalar_node continues it as And/Or
    }
  }

  bool false_path_zero = false;
  if (inp_edges_ordered[1].get_driver_pin().is_const()) {
    const auto& v   = const_of(inp_edges_ordered[1].get_driver_pin());
    false_path_zero = v.is_known_zero() || v.is_string();
  }

  bool true_path_sel = inp_edges_ordered[0].get_driver_pin() == inp_edges_ordered[2].get_driver_pin();

  // Mux selectors are 0/1 (port = sel+1), so mux(s,0,s) == s. The old
  // -1-as-true folds (mux(s,0,-1)->s, mux(s,s,-1)->s, mux(s,-1,s)->-1,
  // mux(s,s,0)->Not(s)) are only bit-accurate when the consumer reads a
  // single bit; for wider consumers they swap -1 (all ones) for 1 — e.g. a
  // yosys-consolidated 8-bit write-enable mux(reset,0,-1) must yield 0xff,
  // not 1 (caught by lgcheck BMC on mem_reset). Keep only the sound rule.
  if (false_path_zero && true_path_sel) {
    return collapse_forward_for_pin(node, inp_edges_ordered[0].get_driver_pin());
  }

  return false;
}

void Cprop::scalar_sext(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  const auto pos_dpin = inp_edges_ordered[1].get_driver_pin();
  if (!pos_dpin.is_const()) {
    return;
  }

  int64_t self_pos;
  {
    const auto& v = const_of(pos_dpin);
    if (!v.is_just_i64()) {
      return;
    }
    self_pos = v.to_just_i64();
  }

  const auto wire_dpin = inp_edges_ordered[0].get_driver_pin();

  // Sext(X,1) maps boolean 1 to -1. It preserves truthiness only, so bypass
  // it on a selector with a proven boolean input, never on a Mux data arm.
  if (self_pos == 1 && is_bool01(wire_dpin)) {
    for (auto sink : consumer_sinks(node)) {
      if (type_op_of(sink.get_master_node()) == Ntype_op::Mux && sink.get_port_id() == 0) {
        sink.del_sink();
        sink.connect_driver(wire_dpin);
      }
    }
  }

  auto wire_master = wire_dpin.get_master_node();
  if (type_op_of(wire_master) != Ntype_op::Sext) {
    return;
  }

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))
  auto parent_pos_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "b");
  if (!parent_pos_dpin.is_const()) {
    return;
  }

  const auto& parent_pos_const = const_of(parent_pos_dpin);
  if (!parent_pos_const.is_just_i64()) {
    return;
  }
  auto parent_pos = parent_pos_const.to_just_i64();

  auto b = std::min(self_pos, parent_pos);
  if (b != self_pos) {
    auto new_const_dpin = create_const(*current_graph, *Dlop::create_integer(b));
    inp_edges_ordered[1].del_sink();
    setup_sink_by_name(node, "b").connect_driver(new_const_dpin);
  }

  auto parent_wire_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "a");
  inp_edges_ordered[0].del_sink();
  setup_sink_by_name(node, "a").connect_driver(parent_wire_dpin);
}

// Boolean EQ-chain folds. The lowering spells `!x` as EQ(x,0) and to-bool as
// EQ(EQ(x,0),0); on an already-0/1 value both nodes of the double chain are
// the identity. Forward order guarantees the inner EQ was visited first, so a
// triple chain EQ(EQ(EQ(z,0),0),0) reduces in two visits to EQ(z,0).
bool Cprop::scalar_eq(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  if (inp_edges_ordered.size() != 2) {
    return false;
  }

  // EQ(b, 1) == b for b in {0,1}.
  auto b_pin = eq_against_const(node, 1);
  if (!b_pin.is_invalid() && is_bool01(b_pin)) {
    return collapse_forward_for_pin(node, b_pin);
  }

  auto x_pin = eq_against_const(node, 0);
  if (x_pin.is_invalid() || x_pin.is_const()) {
    return false;
  }
  auto x_master = x_pin.get_master_node();
  if (x_master.is_invalid() || type_op_of(x_master) != Ntype_op::EQ) {
    return false;
  }
  // node = EQ(EQ(y,0), 0). The inner EQ output is 0/1 by contract, and for a
  // 0/1 value the double compare-to-zero is the identity: fold to y when y is
  // itself provably 0/1. (When y is wide this chain is a genuine to-bool
  // coercion and must stay.)
  auto y_pin = eq_against_const(x_master, 0);
  if (y_pin.is_invalid() || !is_bool01(y_pin)) {
    return false;
  }
  return collapse_forward_for_pin(node, y_pin);  // inner EQ dies via DCE when unused
}

// Boolean hygiene for the 0/1 logic a lowered `if` chain leaves behind: path
// conditions, flop enables and bit-matrix updates. tolg spells `x != 0` as
// `(x == 0) ^ 1`, and a per-bit reset/enable nest folds into And/Or cones
// that repeat a condition next to its own negation. Every rule is exact:
//   x ^ 1 -> EQ(x,0)                     x in {0,1}
//   and(.., c, .., !c, ..) -> 0          any widths: one of the pair is 0
//   or(.., c, .., !c, ..) -> 1           every operand in {0,1}
//   or(b, and(b, r..), ..) -> or(b, ..)  absorption, any widths
//   or(b, and(!b, r..), ..) -> or(b, and(r..), ..)
//                                        b, !b and and(r..) in {0,1}
//   and(x, x, ..) / or(x, x, ..)         duplicate operands dropped
// `c`/`!c` are compared through decode_bool_condition, so `x==0`, `(x==0)^1`
// after its rewrite, and `s ? 0 : 1` all name the same literal.
bool Cprop::scalar_bool(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  const auto op = type_op_of(node);
  if (op == Ntype_op::Xor) {
    if (inp_edges_ordered.size() != 2) {
      return false;
    }
    hhds::Pin_class value;
    bool            has_one = false;
    for (const auto& sink : inp_edges_ordered) {
      const auto driver = sink.get_driver_pin();
      if (driver.is_const()) {
        const auto& c = const_of(driver);
        has_one       = !c.has_unknowns() && c.is_just_i64() && c.to_just_i64() == 1;
      } else {
        value = driver;
      }
    }
    if (!has_one || value.is_invalid() || !is_bool01(value)) {
      return false;
    }
    clear_all_sinks(node);
    livehd::graph_util::set_type_op(node, Ntype_op::EQ);
    livehd::graph_util::append_sink_operand(node, Ntype_op::EQ, 0).connect_driver(value);
    livehd::graph_util::append_sink_operand(node, Ntype_op::EQ, 0)
        .connect_driver(create_const(*current_graph, *Dlop::create_integer(0)));
    livehd::graph_util::set_ubits(node.create_driver_pin(0), 1);
    return true;
  }
  if (op != Ntype_op::And && op != Ntype_op::Or) {
    return false;
  }

  bool changed = false;

  // Duplicate operands: x & x == x and x | x == x.
  {
    absl::flat_hash_set<hhds::Class_index> seen;
    for (const auto& sink : inp_edges_ordered) {
      const auto driver = sink.get_driver_pin();
      if (!driver.is_const() && !seen.insert(driver.get_class_index()).second) {
        sink.del_sink();
        changed = true;
      }
    }
    if (changed) {
      inp_edges_ordered = ordered_inp_edges(node);
    }
  }

  struct Literal {
    hhds::Pin_class sink;
    Bool_condition  condition;
    bool            boolean;
  };
  absl::flat_hash_map<hhds::Class_index, Literal> literals;
  absl::flat_hash_set<hhds::Class_index>          operands;
  bool                                            all_boolean = true;
  for (auto sink : inp_edges_ordered) {
    auto pin = sink.get_driver_pin();
    operands.insert(pin.get_class_index());
    all_boolean &= is_bool01(pin);
  }
  for (auto sink : inp_edges_ordered) {
    auto pin = sink.get_driver_pin();
    if (pin.is_const()) {
      continue;
    }
    const auto condition = decode_bool_condition(pin);
    if (!condition) {
      continue;
    }
    const bool boolean  = is_bool01(pin);
    auto [it, inserted] = literals.try_emplace(condition->base.get_class_index(), Literal{sink, *condition, boolean});
    if (inserted) {
      continue;
    }
    const auto& prior = it->second;
    if (prior.condition.true_when_base != condition->true_when_base) {
      if (op == Ntype_op::And || all_boolean) {
        replace_node(node, *Dlop::create_integer(op == Ntype_op::And ? 0 : 1));
        return true;
      }
    } else if (boolean && prior.boolean) {
      sink.del_sink();
      changed = true;
    }
  }
  if (changed) {
    inp_edges_ordered = ordered_inp_edges(node);
  }
  if (op == Ntype_op::And) {
    size_t          nonboolean = 0;
    hhds::Pin_class constant;
    for (auto sink : inp_edges_ordered) {
      const auto pin = sink.get_driver_pin();
      if (!is_bool01(pin)) {
        ++nonboolean;
      }
      if (pin.is_const()) {
        constant = sink;
      }
    }
    if (!constant.is_invalid() && inp_edges_ordered.size() > 1) {
      const auto  pin   = constant.get_driver_pin();
      const auto& value = const_of(pin);
      if (nonboolean == (is_bool01(pin) ? 0u : 1u) && !value.has_unknowns() && value.is_just_i64()) {
        if (!(value.to_just_i64() & 1)) {
          replace_node(node, *Dlop::create_integer(0));
          return true;
        }
        constant.del_sink();
        changed = true;
      }
    }
  } else {
    // Inspect each private And once. Shared operands debit one pass-wide edge
    // budget: many observers cannot each rewalk a large surviving conjunction.
    for (auto sink : inp_edges_ordered) {
      const auto pin = sink.get_driver_pin();
      if (pin.is_const() || is_graph_input_pin(pin)) {
        continue;
      }
      auto child = pin.get_master_node();
      if (type_op_of(child) != Ntype_op::And) {
        continue;
      }
      const bool                   owned    = has_single_consumer(pin);
      bool                         absorbed = false, complete = true;
      size_t                       boolean_count = 0;
      std::vector<hhds::Pin_class> complements;
      for (auto input : child.inp_sorted_pins()) {
        auto& budget = livehd::cprop_value::active->inspection_budget;
        if (!owned) {
          if (!budget) {
            complete = false;
            break;
          }
          --budget;
        }
        const auto value = input.get_driver_pin();
        if (operands.contains(value.get_class_index())) {
          absorbed = true;
          break;
        }
        const bool boolean  = is_bool01(value);
        boolean_count      += boolean;
        if (!owned || !boolean || value.is_const()) {
          continue;
        }
        const auto condition = decode_bool_condition(value);
        if (!condition) {
          continue;
        }
        const auto literal = literals.find(condition->base.get_class_index());
        if (literal != literals.end() && literal->second.boolean
            && literal->second.condition.true_when_base != condition->true_when_base) {
          complements.push_back(input);
        }
      }
      if (absorbed) {
        sink.del_sink();
        changed = true;
        if (!child.has_out_edges()) {
          bwd_del_node(child);
        }
      } else if (complete && owned && !complements.empty()) {
        // Keep at least one Boolean factor: otherwise removing !b can expose
        // high bits of a wider residual value in b | (!b & residual).
        if (boolean_count == complements.size()) {
          complements.pop_back();
        }
        if (complements.empty()) {
          continue;
        }
        livehd::cprop_value::forget(pin);
        for (auto input : complements) {
          input.del_sink();
        }
        auto rest = ordered_inp_edges(child);
        if (rest.size() == 1) {
          collapse_forward_for_pin(child, rest.front().get_driver_pin());
        }
        changed = true;
      }
    }
  }
  if (changed && !node.is_invalid()) {
    inp_edges_ordered = ordered_inp_edges(node);
  }

  return changed;
}

// Compose chained constant shifts. All rules are exact under the unlimited-
// precision signed semantics (SHL never drops bits, SRA is floor division):
//   SRA(SRA(x,a),b) -> SRA(x,a+b)      SHL(SHL(x,a),b) -> SHL(x,a+b)
//   SRA(SHL(x,a),b) -> SHL(x,a-b) | x | SRA(x,b-a)   (by sign of a-b)
//   SHL(SRA(x,a),a) -> And(x, -2^a)    (clears the low a bits in place)
// Returns true when the node was rewired (caller re-reads its input edges).
bool Cprop::scalar_shift(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  const auto op = type_op_of(node);
  I(op == Ntype_op::SHL || op == Ntype_op::SRA);
  if (inp_edges_ordered.size() != 2) {
    return false;
  }
  const int outer = const_shl_amount(node);  // shared helper: constant `b` sink or -1
  if (outer < 0) {
    return false;
  }
  auto x_pin = drv_at(node, 0);
  if (x_pin.is_invalid() || x_pin.is_const()) {
    return false;
  }
  auto inner_node = x_pin.get_master_node();
  if (inner_node.is_invalid() || inner_node.get_class_index() == node.get_class_index()) {
    return false;
  }
  const auto inner_op = type_op_of(inner_node);
  if (inner_op != Ntype_op::SHL && inner_op != Ntype_op::SRA) {
    return false;
  }
  const int inner = const_shl_amount(inner_node);
  if (inner < 0) {
    return false;
  }
  auto src = drv_at(inner_node, 0);
  if (src.is_invalid()) {
    return false;
  }

  // The fused sweep visits in forward order, so `inner_node` was already swept:
  // once this node stops reading it nothing downstream of here collects it and
  // a dead shift survives into cgen/sim as a dead def.
  auto drop_dead_inner = [&]() {
    if (!inner_node.is_invalid() && !inner_node.has_out_edges()) {
      bwd_del_node(inner_node);
    }
  };

  auto retarget = [&](Ntype_op new_op, int amount) {
    clear_all_sinks(node);
    if (new_op != op) {
      livehd::graph_util::set_type_op(node, new_op);  // SHL/SRA share the a=0,b=1 sink layout
    }
    setup_sink_by_name(node, "a").connect_driver(src);
    setup_sink_by_name(node, "b").connect_driver(create_const(*current_graph, *Dlop::create_integer(amount)));
    drop_dead_inner();
  };

  constexpr int kAmountCap = 1 << 28;  // same bound const_shl_amount enforces
  if (inner_op == op) {                // SRA(SRA) or SHL(SHL): amounts add
    if (inner + outer > kAmountCap) {
      return false;
    }
    retarget(op, inner + outer);
    return true;
  }
  if (op == Ntype_op::SRA) {  // SRA(SHL(x,inner), outer)
    if (outer >= inner) {
      retarget(Ntype_op::SRA, outer - inner);  // amount 0 collapses in the const sweep below
    } else {
      retarget(Ntype_op::SHL, inner - outer);
    }
    return true;
  }
  // SHL(SRA(x,inner), outer): SRA drops the low `inner` bits, so only the
  // exact-rebuild case outer == inner is a pure mask: x & ~(2^inner - 1).
  if (outer == inner && inner == 0) {
    // Both shifts are identities. The And rewrite below would build the
    // all-ones mask ~0 == -1, a node that says nothing; compose to a
    // zero-amount shift instead and let the constant sweep collapse it.
    retarget(op, 0);
    return true;
  }
  if (outer == inner) {
    clear_all_sinks(node);
    livehd::graph_util::set_type_op(node, Ntype_op::And);
    // ~(2^a - 1) == -(2^a): the infinite mask with the low a bits clear.
    auto neg_mask = Dlop::get_mask_value(inner)->not_op();
    setup_sink_by_name(node, "as").connect_driver(src);
    setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, *neg_mask));
    drop_dead_inner();
    return true;
  }
  return false;
}

// Verilog `{N{bit}}` replication survives lowering as Or(SHL(x,k1),...,x): one
// 0/1 source copied to N constant positions (the same shape also appears
// AND-ed with a value as the mux-free select idiom `{N{bit}} & y`). All N
// copies carry the same truth, so the whole tree is Mux(x, 0, sum(2^ki)) --
// which cgen emits as a lazy ternary instead of N-1 word-wide Or/SHL calls.
bool Cprop::try_broadcast_or(hhds::Node_class& node, Inp_pins& inp_edges_ordered) {
  if (inp_edges_ordered.size() < 2 || inp_edges_ordered.size() > 128) {
    return false;
  }

  // Candidate bases for x, from the first operand: the operand itself, or --
  // when that operand is a constant SHL -- the SHL's own source (the bare-x
  // leaf may sit anywhere in the operand list, or not exist at all).
  absl::InlinedVector<hhds::Pin_class, 2> candidates;
  {
    const auto first = inp_edges_ordered[0].get_driver_pin();
    if (first.is_invalid() || first.is_const()) {
      return false;
    }
    auto m = first.get_master_node();
    if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL && const_shl_amount(m) >= 0) {
      auto s = drv_at(m, 0);
      if (!s.is_invalid()) {
        candidates.push_back(s);
      }
    }
    candidates.push_back(first);
  }

  for (const auto& x : candidates) {
    if (!is_bool01(x)) {
      continue;
    }
    auto C  = Dlop::create_integer(0);
    bool ok = true;
    for (auto& e : inp_edges_ordered) {
      if (Ntype::sink_bank(Ntype_op::Or, e.get_port_id()) != 0) {
        ok = false;
        break;
      }
      const auto& d = e.get_driver_pin();
      int         k = -1;
      if (same_pin(d, x)) {
        k = 0;
      } else if (!d.is_invalid() && !d.is_const()) {
        auto m = d.get_master_node();
        if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL) {
          int a = const_shl_amount(m);
          if (a >= 0 && a <= 4096 && same_pin(drv_at(m, 0), x)) {
            k = a;
          }
        } else if (!m.is_invalid() && type_op_of(m) == Ntype_op::Mux) {
          // A partial broadcast this rule already folded on an inner Or of the
          // same chain (forward order visits the inner node first) arrives
          // here as Mux(x, 0, C'): absorb its constant instead of giving up.
          auto sel  = drv_at(m, 0);
          auto arm0 = drv_at(m, 1);
          auto arm1 = drv_at(m, 2);
          if (same_pin(sel, x) && !arm0.is_invalid() && !arm1.is_invalid() && arm0.is_const() && arm1.is_const()) {
            const auto& c0 = const_of(arm0);
            const auto& c1 = const_of(arm1);
            if (c0.is_known_zero() && !c1.has_unknowns() && !c1.is_negative()) {
              C = C->or_op(c1);
              continue;  // matched; the bit union came from C' directly
            }
          }
        }
      }
      if (k < 0) {
        ok = false;
        break;
      }
      C = C->or_op(*Dlop::get_mask_value(k, k));  // bit k alone; Or dedups repeated positions
    }
    if (!ok) {
      continue;
    }

    auto mux = make_node(*current_graph, Ntype_op::Mux);
    livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(x);
    livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(create_const(*current_graph, *Dlop::create_integer(0)));
    livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(create_const(*current_graph, *C));
    auto md = mux.create_driver_pin(0);
    if (collapse_forward_for_pin(node, md)) {
      return true;
    }
    livehd::cprop_value::retire(mux);  // parallel-edge refusal; keep the original tree
    return false;
  }
  return false;
}

bool Cprop::scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const) {
  const auto window = livehd::graph_util::mask_window_of(mask_const);
  if (!window) {
    return false;
  }
  const auto source = drv_at(node, 0);
  return indexed_get_mask(node, source, window->first, window->second);
}

// ---- bit-slice vectorization -------------------------------------------------
//
// A "structured gates" design spells a W-bit N:1 mux as W*(N-1) one-bit mux2
// cells (bedrock's br_mux_bin_structured_gates, one per bit per tree node, so
// the netlist keeps the gate structure). Inlined, every tree level is W cells
//
//   m_i = Mux(s, Get_mask(x, j0+i), Get_mask(y, j0+d+i))     i = 0..W-1
//
// with ONE select, which is exactly the W-bit Mux(s, x[j0 +: W], y[j0+d +: W]):
// each output bit of a mux is the same bit of the chosen arm. Rewriting a run
// turns every consumer's m_i into Get_mask(wide, i), which is again a one-bit
// read with consecutive positions, so the next dependency level vectorizes
// in the same invocation. A 32:1 x 64-bit tree (br_cdc_fifo_flops) drops from 1,984 one-bit
// muxes -- each a separate slot in `lhd sim` -- to 31 word muxes.
//
// Only 2-arm Muxes whose arms are both single-bit constant-window Get_masks
// qualify; a run needs kMinLanes consecutive positions. The new cells are one
// Mux, two arm slices and one bit read per lane, against n lane muxes (and
// their now-dead arm reads) removed, and no rewritten cell qualifies again.
bool Cprop::vectorize_bit_muxes() {
  livehd::cprop_profile::Timer timer(livehd::cprop_profile::vectorize);
  constexpr int                kMinLanes = 4;

  struct Bit_read {
    hhds::Pin_class src;
    int             pos{0};
  };
  const auto bit_read = [](const hhds::Pin_class& p) -> std::optional<Bit_read> {
    if (p.is_invalid() || p.is_const() || is_graph_input_pin(p)) {
      return std::nullopt;
    }
    auto m = p.get_master_node();
    if (m.is_invalid() || type_op_of(m) != Ntype_op::Get_mask) {
      return std::nullopt;
    }
    const auto r = const_mask_range(m);
    auto       x = drv_at(m, 0);
    if (r.first < 0 || r.second != r.first + 1 || x.is_invalid()) {
      return std::nullopt;
    }
    return Bit_read{x, r.first};
  };

  struct Lane {
    hhds::Node_class mux;
    int              pos_a{0};
  };
  struct Group {
    hhds::Pin_class   sel;
    hhds::Pin_class   src_a;
    hhds::Pin_class   src_b;
    int               delta{0};  // pos_b - pos_a
    std::vector<Lane> lanes;
  };
  // A full-width low slice of an operand that provably fits IS the operand.
  const auto slice = [&](const hhds::Pin_class& src, int lo, int n) -> hhds::Pin_class {
    if (lo == 0 && fits_unsigned_window(src, n)) {
      return src;
    }
    auto get = livehd::cprop_value::make_get_mask(*current_graph, src, lo, lo + n);
    auto out = get.create_driver_pin(0);
    livehd::graph_util::set_ubits(out, n);
    return out;
  };

  // A dependency level belongs to one set of lane candidates. Process levels
  // producer first; a wide mux emitted at one level supplies the bit reads for
  // the next. No graph-wide retry per mux-tree depth.
  absl::flat_hash_map<hhds::Class_index, size_t> levels;
  std::vector<std::vector<hhds::Node_class>>     candidates;
  for (auto node : stable_nodes(current_graph)) {
    if (node.is_invalid()) {
      continue;
    }
    const auto op    = type_op_of(node);
    size_t     level = 0;
    if (Ntype::is_comb(op)) {
      for (auto input : node.inp_sorted_pins()) {
        const auto producer = input.get_driver_pin().get_master_node();
        if (auto it = levels.find(producer.get_class_index()); it != levels.end()) {
          level = std::max(level, it->second);
        }
      }
      if (op == Ntype_op::Mux) {
        ++level;
      }
    }
    levels.insert_or_assign(node.get_class_index(), level);
    if (op == Ntype_op::Mux) {
      if (candidates.size() <= level) {
        candidates.resize(level + 1);
      }
      candidates[level].push_back(node);
    }
  }
  bool changed = false;
  for (const auto& level_nodes : candidates) {
    using Key = std::tuple<hhds::Class_index, hhds::Class_index, hhds::Class_index, int>;
    absl::flat_hash_map<Key, size_t> group_index;
    std::vector<Group>               groups;  // first-seen order keeps the rewrite deterministic

    for (auto node : level_nodes) {
      if (node.is_invalid() || type_op_of(node) != Ntype_op::Mux || !node.has_out_edges()) {
        continue;
      }
      if (!is_two_arm_mux(ordered_inp_edges(node))) {
        continue;
      }
      auto sel = drv_at(node, 0);
      auto a   = bit_read(drv_at(node, 1));
      auto b   = bit_read(drv_at(node, 2));
      if (sel.is_invalid() || !a || !b) {
        continue;
      }
      const Key key{sel.get_class_index(), a->src.get_class_index(), b->src.get_class_index(), b->pos - a->pos};
      auto [it, inserted] = group_index.try_emplace(key, groups.size());
      if (inserted) {
        groups.push_back(Group{sel, a->src, b->src, b->pos - a->pos, {}});
      }
      groups[it->second].lanes.push_back(Lane{node, a->pos});
    }

    for (auto& group : groups) {
      if (group.lanes.size() < static_cast<size_t>(kMinLanes)) {
        continue;
      }
      std::stable_sort(group.lanes.begin(), group.lanes.end(), [](const Lane& x, const Lane& y) { return x.pos_a < y.pos_a; });
      size_t start = 0;
      while (start < group.lanes.size()) {
        // A run of STRICTLY consecutive positions. A repeated position is a
        // duplicate lane CSE did not merge; it ends the run and stays scalar.
        size_t end = start + 1;
        while (end < group.lanes.size() && group.lanes[end].pos_a == group.lanes[end - 1].pos_a + 1) {
          ++end;
        }
        const int n = static_cast<int>(end - start);
        if (n >= kMinLanes && group.lanes[start].pos_a >= 0 && group.lanes[start].pos_a + group.delta >= 0) {
          const int pos_a = group.lanes[start].pos_a;
          auto      arm_a = slice(group.src_a, pos_a, n);
          auto      arm_b = slice(group.src_b, pos_a + group.delta, n);
          auto      wide  = make_node(*current_graph, Ntype_op::Mux);
          livehd::graph_util::setup_sink_pid(wide, 0).connect_driver(group.sel);
          livehd::graph_util::setup_sink_pid(wide, 1).connect_driver(arm_a);
          livehd::graph_util::setup_sink_pid(wide, 2).connect_driver(arm_b);
          auto wide_out = wide.create_driver_pin(0);
          livehd::graph_util::set_ubits(wide_out, n);
          for (size_t i = start; i < end; ++i) {
            auto bit     = livehd::cprop_value::make_get_mask(*current_graph,
                                                              wide_out,
                                                              static_cast<int>(i - start),
                                                              static_cast<int>(i - start) + 1);
            auto bit_out = bit.create_driver_pin(0);
            livehd::graph_util::set_ubits(bit_out, 1);
            auto lane = group.lanes[i].mux;
            if (collapse_forward_for_pin(lane, bit_out)) {
              changed = true;
            } else {
              livehd::cprop_value::retire(bit);  // the lane mux stays scalar; the wide mux is still exact
            }
          }
        }
        start = end;
      }
    }
  }
  return changed;
}

// ---- bit-slice reductions ----------------------------------------------------
//
// A bit-matrix reduction spells a word test one bit at a time. bedrock's
// pairwise arbiter computes
//
//   can_grant[i] = AND_{j != i} (!request[j] | priority[i][j])
//
// as fifteen Or(EQ(request[j], 0), priority[16i + j]) operands of one And per
// row. When operands of an And (Or) are the SAME 1-bit expression over
// single-bit slices whose positions keep one per-leaf offset -- operand `a`
// reads leaf l at a + d_l -- the group is one word expression under a mask:
//
//   AND_a f(src_l[a + d_l]) == (f(Y_l) & M) == M     Y_l = src_l[lo+d_l +: n]
//   OR_a  f(src_l[a + d_l]) == (f(Y_l) & M) != 0     M   = sum of 2^(a - lo)
//
// with f's 1-bit operations applied bitwise: And/Or/Xor stay, a negation
// (EQ(x,0), Mux(s,1,0)) becomes Xor with the n-bit all-ones word, a constant
// 1 becomes that word. Bit k of every Y_l is src_l[lo + k + d_l], so bit a-lo
// of f(Y) is exactly operand a. Every intermediate is an n-bit unsigned word.
bool Cprop::vectorize_bit_reductions() {
  livehd::cprop_profile::Timer timer(livehd::cprop_profile::vectorize);
  constexpr int                kMinTerms  = 4;
  constexpr size_t             kMaxLeaves = 8;

  struct Leaf {
    hhds::Pin_class src;
    int             pos{0};
  };
  struct Term {
    std::string       shape;   // canonical, includes each leaf's source identity
    std::vector<Leaf> leaves;  // in shape order
  };
  const auto bit_read = [](const hhds::Pin_class& p) -> std::optional<Leaf> {
    if (p.is_invalid() || p.is_const() || is_graph_input_pin(p)) {
      return std::nullopt;
    }
    auto m = p.get_master_node();
    if (m.is_invalid() || type_op_of(m) != Ntype_op::Get_mask) {
      return std::nullopt;
    }
    const auto r = const_mask_range(m);
    auto       x = drv_at(m, 0);
    if (r.first < 0 || r.second != r.first + 1 || x.is_invalid()) {
      return std::nullopt;
    }
    return Leaf{x, r.first};
  };
  const auto const_bit = [](const hhds::Pin_class& p) -> std::optional<int> {
    if (!p.is_const()) {
      return std::nullopt;
    }
    const auto& c = const_of(p);
    if (c.has_unknowns() || !c.is_just_i64() || (c.to_just_i64() != 0 && c.to_just_i64() != 1)) {
      return std::nullopt;
    }
    return static_cast<int>(c.to_just_i64());
  };
  // The negated operand of a 1-bit negation spelling, if `node` is one.
  const auto negated_operand = [&](const hhds::Node_class& node) -> hhds::Pin_class {
    if (type_op_of(node) == Ntype_op::EQ) {
      auto x = eq_against_const(node, 0);
      return !x.is_invalid() && !x.is_const() && is_bool01(x) ? x : hhds::Pin_class{};
    }
    if (type_op_of(node) == Ntype_op::Mux && is_two_arm_mux(ordered_inp_edges(node))) {
      const auto f = const_bit(drv_at(node, 1));
      const auto t = const_bit(drv_at(node, 2));
      const auto s = drv_at(node, 0);
      if (f == 1 && t == 0 && !s.is_invalid() && is_bool01(s)) {
        return s;
      }
    }
    return {};
  };

  absl::flat_hash_map<hhds::Class_index, Term> descriptions;
  const auto                                   describe = [&](const hhds::Pin_class& p, Term& term) {
    if (const auto c = const_bit(p)) {
      term.shape = *c ? "1" : "0";
      return true;
    }
    const auto it = descriptions.find(p.get_class_index());
    if (it == descriptions.end()) {
      return false;
    }
    term = it->second;
    return true;
  };
  const auto compute = [&](const hhds::Pin_class& p, Term& term) -> bool {
    if (auto c = const_bit(p); c.has_value()) {
      term.shape = *c ? "1" : "0";
      return true;
    }
    if (auto leaf = bit_read(p); leaf.has_value()) {
      term.shape = absl::StrCat("L", leaf->src.get_class_index().value);
      term.leaves.push_back(*leaf);
      return true;
    }
    if (p.is_invalid() || p.is_const() || is_graph_input_pin(p) || !is_bool01(p)) {
      return false;
    }
    auto node = p.get_master_node();
    if (auto x = negated_operand(node); !x.is_invalid()) {
      Term inner;
      if (!describe(x, inner)) {
        return false;
      }
      term.shape = absl::StrCat("N(", inner.shape, ")");
      term.leaves.insert(term.leaves.end(), inner.leaves.begin(), inner.leaves.end());
      return term.shape.size() <= 512;
    }
    const auto op = type_op_of(node);
    if (op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor) {
      return false;
    }
    std::vector<Term> kids;
    for (auto sink : node.inp_sorted_pins()) {
      Term kid;
      if (!describe(sink.get_driver_pin(), kid)) {
        return false;
      }
      if (kids.size() >= kMaxLeaves) {
        return false;
      }
      kids.push_back(std::move(kid));
    }
    if (kids.size() < 2) {
      return false;
    }
    // Commutative: order by shape, which names sources but never positions,
    // so every operand of one family sorts the same way.
    std::ranges::stable_sort(kids, [](const Term& x, const Term& y) { return x.shape < y.shape; });
    term.shape = op == Ntype_op::And ? "A(" : op == Ntype_op::Or ? "O(" : "X(";
    for (const auto& kid : kids) {
      absl::StrAppend(&term.shape, kid.shape, ",");
      term.leaves.insert(term.leaves.end(), kid.leaves.begin(), kid.leaves.end());
    }
    term.shape += ")";
    return term.leaves.size() <= kMaxLeaves && term.shape.size() <= 512;
  };

  const auto nodes = stable_nodes(current_graph);
  for (auto node : nodes) {
    if (node.is_invalid()) {
      continue;
    }
    Term term;
    auto pin = node.get_driver_pin(0);
    if (!pin.is_invalid() && compute(pin, term)) {
      descriptions.emplace(pin.get_class_index(), std::move(term));
    }
  }
  bool changed = false;
  for (auto node : nodes) {
    if (node.is_invalid() || !node.has_out_edges()) {
      continue;
    }
    const auto op = type_op_of(node);
    if (op != Ntype_op::And && op != Ntype_op::Or) {
      continue;
    }
    const auto edges = ordered_inp_edges(node);
    if (edges.size() < static_cast<size_t>(kMinTerms)) {
      continue;
    }
    struct Member {
      hhds::Pin_class sink;
      Term            term;
    };
    absl::flat_hash_map<std::string, std::vector<Member>> families;
    std::vector<std::string>                              order;  // first-seen: deterministic
    for (const auto& sink : edges) {
      Term term;
      if (!describe(sink.get_driver_pin(), term) || term.leaves.empty()) {
        continue;
      }
      std::string key = term.shape;
      for (const auto& leaf : term.leaves) {
        absl::StrAppend(&key, "|", leaf.pos - term.leaves.front().pos);
      }
      auto [it, inserted] = families.try_emplace(key);
      if (inserted) {
        order.push_back(key);
      }
      it->second.push_back(Member{sink, std::move(term)});
    }
    for (const auto& key : order) {
      auto& members = families[key];
      if (members.size() < static_cast<size_t>(kMinTerms)) {
        continue;
      }
      absl::flat_hash_set<int> seen;
      int                      lo = std::numeric_limits<int>::max();
      int                      hi = std::numeric_limits<int>::min();
      bool                     ok = true;
      for (const auto& m : members) {
        const int a = m.term.leaves.front().pos;
        ok          = ok && seen.insert(a).second;
        lo          = std::min(lo, a);
        hi          = std::max(hi, a);
      }
      const int n = hi - lo + 1;
      if (!ok || n > 4096) {
        continue;
      }
      const auto& rep = members.front().term;
      for (const auto& leaf : rep.leaves) {
        ok = ok && lo + (leaf.pos - rep.leaves.front().pos) >= 0;
      }
      if (!ok) {
        continue;
      }
      Dlop mask = *Dlop::create_integer(0);
      for (const auto& m : members) {
        mask = *mask.or_op(*Dlop::create_integer(1)->shl_op(*Dlop::create_integer(m.term.leaves.front().pos - lo)));
      }
      const Dlop ones = *Dlop::create_integer(1)->shl_op(*Dlop::create_integer(n))->sub_op(*Dlop::create_integer(1));

      // Rebuild the representative's expression over n-bit words. Its term
      // index is its first CANONICAL leaf; each raw leaf keeps its own offset
      // from it. Operands are commutative, so another family member is this
      // same expression with every position shifted by (a_m - a_rep).
      const int                                              a_rep = rep.leaves.front().pos;
      std::function<hhds::Pin_class(const hhds::Pin_class&)> build;
      const auto word = [&](Ntype_op word_op, const std::vector<hhds::Pin_class>& operands) {
        auto w = make_node(*current_graph, word_op);
        for (const auto& operand : operands) {
          livehd::graph_util::append_sink_operand(w, word_op, 0).connect_driver(operand);
        }
        auto out = w.create_driver_pin(0);
        livehd::graph_util::set_ubits(out, n);
        return out;
      };
      build = [&](const hhds::Pin_class& p) -> hhds::Pin_class {
        if (auto c = const_bit(p); c.has_value()) {
          return create_const(*current_graph, *c ? ones : *Dlop::create_integer(0));
        }
        if (auto leaf = bit_read(p); leaf.has_value()) {
          const int base = lo + (leaf->pos - a_rep);
          auto      get  = livehd::cprop_value::make_get_mask(*current_graph, leaf->src, base, base + n);
          auto      out  = get.create_driver_pin(0);
          livehd::graph_util::set_ubits(out, n);
          return out;
        }
        auto m = p.get_master_node();
        if (auto x = negated_operand(m); !x.is_invalid()) {
          return word(Ntype_op::Xor, {build(x), create_const(*current_graph, ones)});
        }
        // Snapshot the drivers first: build() mints cells, and a live
        // sorted-pin iterator is invalidated by any body mutation.
        std::vector<hhds::Pin_class> drivers;
        for (auto sink : m.inp_sorted_pins()) {
          drivers.push_back(sink.get_driver_pin());
        }
        std::vector<hhds::Pin_class> operands;
        for (const auto& driver : drivers) {
          operands.push_back(build(driver));
        }
        return word(type_op_of(m), operands);
      };
      const auto      f      = build(members.front().sink.get_driver_pin());
      const auto      masked = word(Ntype_op::And, {f, create_const(*current_graph, mask)});
      hhds::Pin_class result;
      if (op == Ntype_op::And) {
        auto eq = make_node(*current_graph, Ntype_op::EQ);
        livehd::graph_util::append_sink_operand(eq, Ntype_op::EQ, 0).connect_driver(masked);
        livehd::graph_util::append_sink_operand(eq, Ntype_op::EQ, 0).connect_driver(create_const(*current_graph, mask));
        result = eq.create_driver_pin(0);
      } else {
        auto any = make_node(*current_graph, Ntype_op::Ror);
        livehd::graph_util::append_sink_operand(any, Ntype_op::Ror, 0).connect_driver(masked);
        result = any.create_driver_pin(0);
      }
      livehd::graph_util::set_ubits(result, 1);
      for (const auto& m : members) {
        m.sink.del_sink();
      }
      livehd::graph_util::append_sink_operand(node, op, 0).connect_driver(result);
      changed = true;
    }
    // Every operand joined one family: the reduction is now that one test.
    if (const auto left = ordered_inp_edges(node); left.size() == 1) {
      collapse_forward_for_pin(node, left.front().get_driver_pin());
    }
  }
  return changed;
}

// Concat lanes that are CONTIGUOUS slices of one source, in order, are one
// wider slice: {x[b +: w2], x[a +: w1]} with b == a + w1 is x[a +: w1 + w2].
// This is what re-packs a vectorized bit row (`out = {m63, ..., m0}` becomes
// one Get_mask of the word mux, or the word mux itself). A lane qualifies only
// when its declared width equals its slice width, so no lane is zero-extended.
bool Cprop::merge_concat_slices(hhds::Node_class& node) {
  // Flatten a private concat region only at its terminal consumer. Expanding
  // every prefix would copy an ever-growing lane table. Lane widths must
  // exactly match before an inner concat can be consumed.
  if (has_single_consumer(node)) {
    const auto edge   = *node.out_edges().begin();
    auto       parent = edge.sink.get_master_node();
    if (type_op_of(parent) == Ntype_op::Concat && edge.sink.get_port_id() % 2 == 0) {
      const auto width = drv_at(parent, edge.sink.get_port_id() + 1);
      if (width.is_const() && const_of(width).is_just_i64()
          && const_of(width).to_just_i64() == livehd::graph_util::concat_total_width(node)) {
        return false;
      }
    }
  }
  std::vector<Pack_lane>        pending, flat;
  std::vector<hhds::Node_class> consumed;
  for (const auto& lane : livehd::graph_util::concat_lanes(node)) {
    pending.push_back({lane.value, lane.offset, lane.offset + lane.width});
  }
  while (!pending.empty()) {
    const auto lane = pending.back();
    pending.pop_back();
    auto child = lane.value.get_master_node();
    if (!lane.value.is_const() && child != node && type_op_of(child) == Ntype_op::Concat && has_single_consumer(child)
        && !livehd::graph_util::has_color(child) && livehd::graph_util::concat_total_width(child) == lane.hi - lane.lo) {
      consumed.push_back(child);
      for (const auto& part : livehd::graph_util::concat_lanes(child)) {
        pending.push_back({part.value, lane.lo + part.offset, lane.lo + part.offset + part.width});
      }
    } else {
      flat.push_back(lane);
    }
  }
  if (!consumed.empty()) {
    std::sort(flat.begin(), flat.end(), [](const auto& a, const auto& b) { return a.lo < b.lo; });
    emit_concat(*current_graph, node, flat);
    for (auto child : consumed) {
      livehd::cprop_value::retire(child);
    }
  }

  const auto lanes = livehd::graph_util::concat_lanes(node);  // MSB-first
  if (lanes.size() < 2) {
    return false;
  }
  struct Run {
    hhds::Pin_class value;  // the original lane value when the run is one lane
    hhds::Pin_class src;    // invalid unless the run is a slice
    int             src_lo{0};
    int             lo{0};  // window inside the Concat result
    int             hi{0};
    int             merged{1};
  };
  std::vector<Run> runs;  // LSB-first
  for (auto it = lanes.rbegin(); it != lanes.rend(); ++it) {
    const auto&     lane = *it;
    hhds::Pin_class src;
    int             src_lo = 0;
    if (!lane.value.is_invalid() && !lane.value.is_const() && !is_graph_input_pin(lane.value)) {
      auto m = lane.value.get_master_node();
      if (!m.is_invalid() && type_op_of(m) == Ntype_op::Get_mask) {
        const auto r = const_mask_range(m);
        auto       x = drv_at(m, 0);
        if (r.first >= 0 && r.second - r.first == lane.width && !x.is_invalid()) {
          src    = x;
          src_lo = r.first;
        }
      }
    }
    if (!runs.empty() && !src.is_invalid() && !runs.back().src.is_invalid() && same_pin(runs.back().src, src)
        && runs.back().src_lo + (runs.back().hi - runs.back().lo) == src_lo) {
      runs.back().hi += lane.width;
      ++runs.back().merged;
      continue;
    }
    runs.push_back(Run{lane.value, src, src_lo, lane.offset, lane.offset + lane.width, 1});
  }
  if (runs.size() == lanes.size()) {
    return false;
  }

  const auto run_value = [&](const Run& r) -> hhds::Pin_class {
    if (r.merged == 1) {
      return r.value;
    }
    const int w = r.hi - r.lo;
    if (r.src_lo == 0 && fits_unsigned_window(r.src, w)) {
      return r.src;
    }
    auto get = livehd::cprop_value::make_get_mask(*current_graph, r.src, r.src_lo, r.src_lo + w);
    auto out = get.create_driver_pin(0);
    livehd::graph_util::set_ubits(out, w);
    return out;
  };

  if (runs.size() == 1) {
    auto value = run_value(runs.front());
    if (collapse_forward_for_pin(node, value)) {
      return true;
    }
    if (!value.is_invalid() && !same_pin(value, runs.front().src)) {
      auto m = value.get_master_node();
      if (!m.is_invalid() && !m.has_out_edges()) {
        livehd::cprop_value::retire(m);
      }
    }
    return false;
  }
  std::vector<Pack_lane> merged;
  merged.reserve(runs.size());
  for (const auto& r : runs) {
    merged.push_back(Pack_lane{run_value(r), r.lo, r.hi});
  }
  emit_concat(*current_graph, node, merged);
  return true;
}

bool Cprop::indexed_get_mask(hhds::Node_class& node, hhds::Pin_class source, int lo, int hi) {
  if (!source.is_const() && type_op_of(source.get_master_node()) == Ntype_op::Or
      && !livehd::cprop_value::active->wiring.has_layout(source)) {
    livehd::cprop_value::remember(source.get_master_node());
  }
  auto pieces = livehd::cprop_value::active->wiring.read(source, lo, hi);
  if (!pieces || pieces->empty()) {
    return false;
  }
  if (pieces->size() == 1 && pieces->front().source == source && pieces->front().source_lo == lo) {
    return false;
  }
  // A lane source may already be a canonical Get_mask. Compose that ONE
  // explicit producer so constructors do not manufacture a new mask chain.
  const auto slice = [&](hhds::Pin_class pin, int begin, int width) {
    if (!pin.is_const() && type_op_of(pin.get_master_node()) == Ntype_op::Get_mask) {
      const auto inner = pin.get_master_node();
      const auto range = const_mask_range(inner);
      if (range.first >= 0 && begin + width <= range.second - range.first) {
        pin    = drv_at(inner, 0);
        begin += range.first;
      }
    }
    if (pin.is_const()) {
      return create_const(*current_graph, *const_of(pin).get_mask_op_opt(begin, begin + width));
    }
    if (begin == 0 && fits_unsigned_window(pin, width)) {
      return pin;
    }
    auto get    = livehd::cprop_value::make_get_mask(*current_graph, pin, begin, begin + width);
    auto output = get.create_driver_pin(0);
    livehd::graph_util::set_ubits(output, width);
    return output;
  };
  if (pieces->size() == 1) {
    const auto& piece = pieces->front();
    const auto  value = slice(piece.source, piece.source_lo, hi - lo);
    return collapse_forward_for_pin(node, value);
  }
  std::vector<Pack_lane> lanes;
  for (const auto& piece : *pieces) {
    lanes.push_back({slice(piece.source, piece.source_lo, piece.hi - piece.lo),
                     static_cast<int>(piece.lo - lo),
                     static_cast<int>(piece.hi - lo)});
  }
  auto old = source.get_master_node();
  emit_concat(*current_graph, node, lanes);
  if (!old.is_invalid() && !old.has_out_edges()) {
    sweep_dead_node(old);
  }
  return true;
}

bool Cprop::scalar_get_mask(hhds::Node_class& node) {
  auto a_pin    = drv_at(node, 0);
  auto mask_pin = drv_at(node, 2);
  if (a_pin.is_invalid() || mask_pin.is_invalid() || !node.has_out_edges()) {
    livehd::cprop_value::retire(node);
    return true;
  }
  if (!mask_pin.is_const()) {
    return false;
  }

  const auto& mask_const = const_of(mask_pin);

  // A private modular Sum region is absorbed only at its terminal mask.
  // Interior masks defer, avoiding growing operand lists at every prefix.
  const int modulus  = low_mask_width(mask_const);
  bool      interior = false;
  if (modulus > 0 && has_single_consumer(node)) {
    const auto next = (*node.out_edges().begin()).sink.get_master_node();
    if (type_op_of(next) == Ntype_op::Sum && has_single_consumer(next)) {
      const auto mask    = (*next.out_edges().begin()).sink.get_master_node();
      const auto literal = type_op_of(mask) == Ntype_op::Get_mask ? drv_at(mask, 2) : hhds::Pin_class{};
      interior           = literal.is_const() && low_mask_width(const_of(literal)) == modulus;
    }
  }
  if (!interior && modulus > 0 && !a_pin.is_const() && has_single_consumer(a_pin)
      && type_op_of(a_pin.get_master_node()) == Ntype_op::Sum) {
    auto sum = a_pin.get_master_node();
    struct Operand {
      hhds::Pin_class pin;
      bool            subtract;
    };
    std::vector<Operand>          pending, leaves;
    std::vector<hhds::Node_class> consumed;
    for (auto input : sum.inp_sorted_pins()) {
      pending.push_back({input.get_driver_pin(), Ntype::sink_bank(Ntype_op::Sum, input.get_port_id()) != 0});
    }
    while (!pending.empty()) {
      auto operand = pending.back();
      pending.pop_back();
      auto mask = operand.pin.get_master_node();
      if (operand.subtract || operand.pin.is_const() || type_op_of(mask) != Ntype_op::Get_mask
          || !has_single_consumer(operand.pin)) {
        leaves.push_back(operand);
        continue;
      }
      const auto literal = drv_at(mask, 2), source = drv_at(mask, 0);
      auto       child = source.get_master_node();
      if (!literal.is_const() || low_mask_width(const_of(literal)) != modulus || source.is_invalid()
          || type_op_of(child) != Ntype_op::Sum || !has_single_consumer(source)) {
        leaves.push_back(operand);
        continue;
      }
      const auto inputs = ordered_inp_edges(child);
      if (std::ranges::any_of(inputs, [](auto input) { return Ntype::sink_bank(Ntype_op::Sum, input.get_port_id()) != 0; })) {
        leaves.push_back(operand);
        continue;
      }
      consumed.push_back(mask);
      consumed.push_back(child);
      for (auto input : inputs) {
        pending.push_back({input.get_driver_pin(), false});
      }
    }
    if (!consumed.empty()) {
      livehd::cprop_value::forget(a_pin);
      clear_all_sinks(sum);
      for (const auto& leaf : leaves) {
        livehd::graph_util::append_sink_operand(sum, Ntype_op::Sum, leaf.subtract ? 1 : 0).connect_driver(leaf.pin);
      }
      for (auto child : consumed) {
        livehd::cprop_value::retire(child);
      }
    }
  }

  // Rule 4: get_mask(a, -1) == a — only when `a` is provably non-negative.
  // get_mask always yields a non-negative value (it zero-extends the selected
  // bits), so it is the to-positive wrapper for signed-read pins (e.g. module
  // ports, which cgen declares `signed`). Bypassing it around a pin that can
  // go negative changes the value: u3 a=0b101 must read 5, not -3 (caught by
  // LEC once the lgcheck BMC stage became sound).
  if (mask_const.is_just_i64() && mask_const.to_just_i64() == -1) {
    const bool nonneg = livehd::cprop_value::unsigned_width(a_pin) >= 0;
    if (!nonneg) {
      return false;
    }
    return collapse_forward_for_pin(node, a_pin);
  }

  // A low contiguous mask is redundant only when an explicit expression
  // proves that the input has no set bits above its selection window.
  {
    const int me = low_mask_width(mask_const);  // <0 unless a low-contiguous 2^n-1
    if (me > 0) {
      if (fits_unsigned_window(a_pin, me)) {
        if (collapse_forward_for_pin(node, a_pin)) {
          return true;
        }
      }
    }
  }

  // Low window of a complement: Get_mask(Not(Get_mask(y, ones n)), ones m) with
  // m <= n reads only bits [0,m) of the complement, and each of those bits
  // agrees with Not(y)'s (Not is a per-bit complement of the infinite string,
  // and the inner mask only rewrites bits >= n). The logical-NOT lowering
  // And(Not(And(x,1)),1) is this shape once the And masks canonicalize to
  // Get_mask. Rewiring the Not to y is only safe when this node is the Not's
  // sole consumer -- any other reader may observe bits >= n.
  {
    const int m_w = low_mask_width(mask_const);
    if (m_w > 0 && !a_pin.is_const() && !is_graph_input_pin(a_pin)) {
      auto not_node = a_pin.get_master_node();
      if (!not_node.is_invalid() && type_op_of(not_node) == Ntype_op::Not) {
        auto w_pin = drv_at(not_node, 0);
        if (has_single_consumer(a_pin) && !w_pin.is_invalid() && !w_pin.is_const() && !is_graph_input_pin(w_pin)) {
          auto inner = w_pin.get_master_node();
          if (!inner.is_invalid() && type_op_of(inner) == Ntype_op::Get_mask && inner.get_class_index() != node.get_class_index()) {
            auto inner_mask_pin = drv_at(inner, 2);
            if (inner_mask_pin.is_const() && low_mask_width(const_of(inner_mask_pin)) >= m_w) {
              auto y = drv_at(inner, 0);
              if (!y.is_invalid() && !same_pin(y, a_pin)) {  // a self-feeding Not must stay
                auto not_sink = find_sink_pin(not_node, "a");
                if (!not_sink.is_invalid()) {
                  not_sink.del_sink();
                  y.connect_sink(not_sink);
                  if (!inner.has_out_edges()) {
                    bwd_del_node(inner);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  const auto window    = livehd::graph_util::mask_window_of(mask_const);
  const auto source_op = type_op_of(a_pin.get_master_node());
  if (window && (source_op == Ntype_op::Set_mask || source_op == Ntype_op::Concat)) {
    return indexed_get_mask(node, a_pin, window->first, window->second);
  }
  return scalar_get_mask_packed(node, mask_const);
}

bool Cprop::scalar_set_mask(hhds::Node_class& node) {
  auto base_pin  = livehd::graph_util::get_driver_of_sink_name(node, "a");
  auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
  auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");
  if (base_pin.is_invalid() || mask_pin.is_invalid() || value_pin.is_invalid() || !base_pin.is_const() || !mask_pin.is_const()) {
    return false;
  }

  const auto& base   = const_of(base_pin);
  auto        window = livehd::graph_util::mask_window_of(const_of(mask_pin));
  if (!base.is_known_zero() || !window || window->first != 0) {
    return false;
  }
  const int me = window->second;
  if (value_pin.is_const() || is_graph_input_pin(value_pin)) {
    return false;
  }

  // set_mask(0, ones[0,n), value) == value when the value is already known to
  // fit below bit n by its cell contract. A preliminary arithmetic width hint
  // does not prove this. Boundary/state pins remain excluded because this
  // rewrite is limited to computed expressions.
  auto       vm       = value_pin.get_master_node();
  const auto vmo      = type_op_of(vm);
  const bool computed = !vm.is_invalid() && vmo != Ntype_op::Invalid && is_computed_comb_op(vmo);
  if (!computed || !fits_unsigned_window(value_pin, me)) {
    return false;
  }

  return collapse_forward_for_pin(node, value_pin);
}

// And(x, 2^n-1) [binary, exactly one constant] is value-identical to
// Get_mask(x, 2^n-1) for ANY sign of x: both keep the low n bits and yield the
// non-negative packed value. Retype so every low-mask truncation shares the
// ONE canonical Get_mask shape: the packed-slice walker, the width no-op rules
// (scalar_get_mask Rule 4/5) and the slice normalizer all key on Get_mask, so
// an And-spelled truncation was invisible to every one of those folds -- the
// dominant miss being the And(SRA(x,k),1) bit-extract, which as
// Get_mask(SRA(x,k),1) resolves through Or/SHL/Set_mask pack chains and
// vanishes outright when x is already known 0/1.
void Cprop::canonicalize_and_mask(hhds::Node_class& node) {
  if (node.is_invalid() || type_op_of(node) != Ntype_op::And || !node.has_out_edges()) {
    return;
  }
  auto edges = node.inp_pins_snapshot();  // SNAPSHOT: this node is rewired below
  if (edges.size() != 2) {
    return;
  }
  int const_idx = -1;
  for (int i = 0; i < 2; ++i) {
    if (edges[i].get_driver_pin().is_const()) {
      if (const_idx >= 0) {
        const_idx = -2;  // two constants: constant-fold territory, not ours
        break;
      }
      const_idx = i;
    }
  }
  if (const_idx < 0) {
    return;
  }
  auto      mask_pin = edges[const_idx].get_driver_pin();
  const int n        = low_mask_width(const_of(mask_pin));
  if (n <= 0) {
    return;
  }
  auto x_pin = edges[const_idx ^ 1].get_driver_pin();
  // With literal width hints the And and Get_mask spellings agree for every
  // operand producer; there is no producer-specific sign-slot exception.
  if (x_pin.is_invalid() || x_pin.is_const()) {
    return;
  }
  edges[0].del_sink();  // one driver per sink pin
  edges[1].del_sink();
  livehd::graph_util::set_type_op(node, Ntype_op::Get_mask);
  livehd::graph_util::connect_mask_operands(node, x_pin, mask_pin);
}

// Hash-cons identical combinational nodes: two nodes with the same op reading
// the SAME driver pins compute the same unlimited-precision value, so every
// consumer of the duplicate can read the survivor instead. Sub-inlining and
// loop replication routinely clone whole expression cones (7.4% of minion's
// emitted sim statements were byte-identical duplicate defs), and cgen emits
// one Slop call per surviving node, so each merge is a direct sim-op win --
// and one node fewer for every other backend.
//
// Key = (op, sorted (sink BANK, driver-pin index, producer generation) list) -- the CANONICAL FORM
// of the node's operand multiset, and an exact map key rather than a digest.
//
// BANK, not raw sink pid. A commutative cell spends one sink pid PER OPERAND
// (graph/cell.hpp's ONE DRIVER PER SINK PIN block), so `Sum(a@0, b@2)` and
// `Sum(b@0, a@2)` are the SAME node and must produce the SAME key; keying on
// the raw pid would make every operand reordering a CSE miss. Ntype::sink_bank
// folds the per-operand pids back to the role (0/1 for Sum/LT/GT, 0 for the
// single-bank reductions) and is the identity for a positional cell, where the
// bank half is the pid and disambiguates exactly as before.
//
// SORTING IS A CANONICAL FORM HERE, unlike a sort over sink port ids: the pair
// sorts on the DRIVER's class index, which discriminates every operand even
// when their roles do not. Within a bank the operands are commutative by the
// cell's own identity op, so ordering them by driver is value-preserving.
//
// The map does the rest: absl hashes the canonical key (a FILTER) and then
// compares two colliding keys ELEMENT BY ELEMENT before treating them as the
// same node. A hash collision can therefore never merge two different cells --
// which matters, because a false CSE merge is a silent miscompile. Duplicated
// operands survive canonicalization (the list is sorted, never deduped), so
// `Sum(a, a)` = 2a keeps a different key from `Sum(a)`, and `Xor(a, a)` = 0
// from `Xor(a)`. And/Or ARE idempotent, but their duplicates are removed by
// scalar simplification during the forward visit, not by the key.
//
// Guards, each load-bearing:
//  * pure combinational ops only (is_computed_comb_op), minus LUT: its function
//    table lives in a node attribute the key does not cover;
//  * names: prefer keeping a named twin (user wires are addressable by sim
//    queries/VCD); when BOTH carry names, skip rather than pick a loser.
void Cprop::cse_pass(const std::vector<hhds::Node_class>& order) {
  namespace gu = livehd::graph_util;
  using Key    = std::pair<uint16_t, std::vector<std::tuple<uint32_t, uint64_t, uint64_t>>>;
  struct Entry {
    hhds::Node_class node;
    uint64_t         generation;
  };
  absl::flat_hash_map<Key, Entry> seen;
  auto&                           facts      = *livehd::cprop_value::active;
  const auto                      generation = [&](const hhds::Node_class& node) {
    auto it = facts.generations.find(node.get_class_index());
    return it == facts.generations.end() ? uint64_t{0} : it->second;
  };
  for (auto node : order) {
    // Original-order handles may have been retired and recycled by a consuming
    // rewrite. New nodes are normalized at emission, never as this old handle.
    if (node.is_invalid() || generation(node) != 0) {
      continue;
    }
    canonicalize_and_mask(node);
    scalar_node(node);
    if (node.is_invalid()) {
      continue;
    }
    remember_node(node);
    livehd::cprop_profile::Timer timer(livehd::cprop_profile::cse);
    const auto                   op = type_op_of(node);
    if (!is_computed_comb_op(op) || op == Ntype_op::LUT || !node.has_out_edges()) {
      continue;
    }
    Key key;
    key.first   = static_cast<uint16_t>(op);
    bool usable = true;
    for (auto sink : node.inp_sorted_pins()) {
      auto pin = sink.get_driver_pin();
      if (pin.is_invalid()) {
        usable = false;
        break;
      }
      key.second.emplace_back(Ntype::sink_bank(op, sink.get_port_id()),
                              pin.get_class_index().value,
                              generation(pin.get_master_node()));
    }
    if (!usable || key.second.empty()) {
      continue;
    }
    std::sort(key.second.begin(), key.second.end());
    auto [it, inserted] = seen.try_emplace(std::move(key), Entry{node, generation(node)});
    if (inserted) {
      continue;
    }
    auto kept = it->second.node;
    if (kept.is_invalid() || generation(kept) != it->second.generation) {
      it->second = {node, generation(node)};
      continue;
    }
    if (gu::has_color(node) != gu::has_color(kept) || (gu::has_color(node) && gu::color_of(node) != gu::color_of(kept))) {
      continue;
    }
    const auto kd = kept.get_driver_pin(0), nd = node.get_driver_pin(0);
    const bool kept_named = gu::has_name(kept) || !gu::pin_name_of(kd).empty();
    const bool node_named = gu::has_name(node) || !gu::pin_name_of(nd).empty();
    if (kept_named && node_named) {
      continue;
    }
    // Keep the earlier identity so keys already made from it remain valid.
    if (node_named) {
      if (gu::has_name(node)) {
        kept.attr(hhds::attrs::name).set(std::string{gu::node_name_of(node)});
      }
      if (!gu::pin_name_of(nd).empty()) {
        gu::set_pin_name(kd, gu::pin_name_of(nd));
      }
    }
    collapse_forward_for_pin(node, kd);
  }
}

void Cprop::remember_node(const hhds::Node_class& node) {
  livehd::cprop_value::remember(node);
  livehd::cprop_value::active->conditions.erase(node.get_driver_pin(0).get_class_index());
  (void)decode_bool_condition(node.get_driver_pin(0));
}
bool Cprop::canonicalize_pack(hhds::Node_class& node) { return canonicalize_concat_pack(current_graph, node); }

void Cprop::normalize_emitted(hhds::Node_class& node) {
  if (node.is_invalid()) {
    return;
  }
  auto       inputs = ordered_inp_edges(node);
  const auto op     = type_op_of(node);
  if (op == Ntype_op::Get_mask) {
    // Wiring constructors do one indexed read; no modular-cone discovery.
    auto mask = drv_at(node, 2);
    if (mask.is_const() && scalar_get_mask_packed(node, const_of(mask))) {
      return;
    }
  } else if (op == Ntype_op::Mux) {
    if (scalar_mux(node, inputs, false)) {
      return;
    }
  }
  if (node.is_invalid()) {
    return;
  }
  inputs = ordered_inp_edges(node);
  scalar_bool(node, inputs);
  if (node.is_invalid()) {
    return;
  }
  inputs = ordered_inp_edges(node);
  try_constant_prop(node, inputs);
  if (node.is_invalid()) {
    return;
  }
  inputs = ordered_inp_edges(node);
  try_collapse_forward(node, inputs);
  if (!node.is_invalid()) {
    livehd::cprop_value::remember(node);
    (void)decode_bool_condition(node.get_driver_pin(0));
  }
}

void Cprop::scalar_node(hhds::Node_class& node) {
  livehd::cprop_profile::Timer timer(livehd::cprop_profile::scalar);
  if (node.is_invalid()) {
    return;
  }
  auto op = type_op_of(node);
  // IO/state/Sub/const/attr cells are not copy-propagatable. Hotmux sits
  // between Mux (36) and IO (39) and IS handled (const one-hot selector fold,
  // same-arm collapse); Concat sits ABOVE the boundary band by encoding
  // accident and is handled too (all-const lane fold) -- see
  // is_computed_comb_op.
  if (!is_computed_comb_op(op)) {
    return;
  }

  if (!node.has_out_edges() && op != Ntype_op::Hotmux) {
    bwd_del_node(node);
    return;
  }

  auto inp_edges_ordered = ordered_inp_edges(node);
  if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
    if (node.is_invalid()) {
      return;
    }
  }

  if (op == Ntype_op::Sext) {
    if (inp_edges_ordered.size() >= 2) {
      scalar_sext(node, inp_edges_ordered);
    }
  } else if (op == Ntype_op::Mux) {
    bool del = scalar_mux(node, inp_edges_ordered);
    if (del) {
      return;
    }
    if (type_op_of(node) != Ntype_op::Mux) {
      // A constant-arm rule retyped it to And/Or: continue as that op.
      inp_edges_ordered = ordered_inp_edges(node);
      if (scalar_bool(node, inp_edges_ordered)) {
        if (node.is_invalid() || !node.has_out_edges()) {
          return;
        }
        inp_edges_ordered = ordered_inp_edges(node);
      }
    }
  } else if (op == Ntype_op::EQ) {
    if (scalar_eq(node, inp_edges_ordered)) {
      return;
    }
  } else if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    if (scalar_shift(node, inp_edges_ordered)) {
      // Rewired in place (new source and/or amount): refresh the edge view
      // so the constant sweep below sees the composed shift, not the stale
      // one (an amount rewritten to 0 collapses right there).
      inp_edges_ordered = ordered_inp_edges(node);
    }
  } else if (op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    if (op == Ntype_op::Or && try_broadcast_or(node, inp_edges_ordered)) {
      return;
    }
    if (scalar_bool(node, inp_edges_ordered)) {
      if (node.is_invalid() || !node.has_out_edges()) {
        return;
      }
      inp_edges_ordered = ordered_inp_edges(node);
      if (type_op_of(node) == Ntype_op::EQ) {
        // x ^ 1 became EQ(x,0): let the double-negation fold see it now.
        if (scalar_eq(node, inp_edges_ordered) || node.is_invalid()) {
          return;
        }
        inp_edges_ordered = ordered_inp_edges(node);
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    bool del = scalar_get_mask(node);
    if (del || node.is_invalid()) {
      return;
    }
    // The packed-slice fold can rewire `a`/`mask` in place and return false,
    // which would leave the vector captured above stale.
    inp_edges_ordered = ordered_inp_edges(node);
  } else if (op == Ntype_op::Set_mask) {
    if (scalar_set_mask(node)) {
      return;
    }
  }

  auto replaced_some = try_constant_prop(node, inp_edges_ordered);

  if (node.is_invalid()) {
    return;
  }

  if (replaced_some) {
    inp_edges_ordered = ordered_inp_edges(node);
  }

  try_collapse_forward(node, inp_edges_ordered);
  if (!node.is_invalid()) {
    low_lane(node);
  }
}

void Cprop::do_trans(const std::shared_ptr<hhds::Graph>& g) {
  if (!g) {
    return;
  }

  auto gio  = g->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};

  TRACE_EVENT("pass", nullptr, [&name](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, name));
  });

  livehd::cprop_profile::Invocation profile(name);
  livehd::cprop_value::Facts        facts;
  livehd::cprop_value::Scope        fact_scope(facts);
  current_graph          = g.get();
  // One ordinary forward sweep, including canonicalization and CSE. Region
  // passes below each own disjoint candidates and never retry the graph.
  const auto order       = stable_nodes(current_graph);
  size_t     edge_budget = 0;
  for (auto node : order) {
    for ([[maybe_unused]] auto input : node.inp_sorted_pins()) {
      ++edge_budget;
    }
  }
  facts.wiring.set_edge_budget(edge_budget);
  facts.inspection_budget = edge_budget;
  cse_pass(order);
  // Consumer-side ownership absorbs each private write suffix once. Delaying
  // layout emission preserves the Set_mask shape used by mux lane factoring.
  auto packs = storage_nodes(current_graph);
  for (auto it = packs.rbegin(); it != packs.rend(); ++it) {
    canonicalize_concat_pack(current_graph, *it);
  }
  vectorize_bit_muxes();
  mux_share_pass();
  vectorize_bit_reductions();
  // Concats containing vectorized lanes are the explicit consumers affected
  // by region emission. This visits lane operands once, with no scalar retry.
  for (auto node : storage_nodes(current_graph)) {
    if (!node.is_invalid() && type_op_of(node) == Ntype_op::Concat) {
      merge_concat_slices(node);
    }
  }
  cleanup_dead_nodes(current_graph);
  current_graph = nullptr;
}

void Cprop::bwd_del_node(hhds::Node_class& node) {
  // Aggressive del: also remove single-user inputs that become dead.
  // WARNING: only call when all needed downstream edges have been added.

  I(!Ntype::is_loop_last(type_op_of(node)));

  absl::flat_hash_set<hhds::Class_index> potential_set;
  std::deque<hhds::Node_class>           potential;

  for (const auto& in_pin : node.inp_sorted_pins()) {
    const auto in_drv = in_pin.get_driver_pin();
    if (is_graph_input_pin(in_drv) || is_graph_output_pin(in_drv)) {
      continue;
    }
    auto master = in_drv.get_master_node();
    // CONST_NODE (and other singletons) cannot be deleted: const pins are
    // leaves of the form CONST_NODE.pid_N, so dropping the consumer just
    // leaves the pin unreferenced — harmless and dedup-friendly.
    if (livehd::graph_util::is_builtin_node(master)) {
      continue;
    }
    if (potential_set.contains(master.get_class_index())) {
      continue;
    }
    potential.emplace_back(master);
    potential_set.insert(master.get_class_index());
  }

  livehd::cprop_value::retire(node);

  while (!potential.empty()) {
    auto n = potential.front();
    potential.pop_front();

    if (n.is_invalid()) {
      continue;
    }

    if (!Ntype::is_loop_last(type_op_of(n)) && type_op_of(n) != Ntype_op::Hotmux && !n.has_out_edges()) {
      for (const auto& in_pin : n.inp_sorted_pins()) {
        const auto in_drv = in_pin.get_driver_pin();
        if (is_graph_input_pin(in_drv) || is_graph_output_pin(in_drv)) {
          continue;
        }
        auto d_master = in_drv.get_master_node();
        if (livehd::graph_util::is_builtin_node(d_master)) {
          continue;
        }
        if (potential_set.contains(d_master.get_class_index())) {
          continue;
        }
        potential.emplace_back(d_master);
        potential_set.insert(d_master.get_class_index());
      }
      livehd::cprop_value::retire(n);
    }
  }
}
