// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "reassoc.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attrs.hpp"
#include "cell.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace {
namespace gu = livehd::graph_util;

// The region blaster keeps a wide disjoint shift/OR pack as native wiring
// (region_blast.cpp kNativeWiringBits); merging links there could change which
// cells it recognizes, so wide cells stay as they are.
constexpr int kMaxReassocBits = 4096;

[[nodiscard]] bool reassoc_op(Ntype_op op) { return op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor; }

[[nodiscard]] std::string ctrl_members_of(const hhds::Node_class& node) {
  const auto attr = node.attr(livehd::attrs::ctrl_members);
  return attr.has() ? std::string{attr.get()} : std::string{};
}

// The only driver pin an And/Or/Xor cell has. Every reader (the blaster, the
// partitioner) reads the widest driver pin, which is this one.
[[nodiscard]] int out_width(const hhds::Node_class& node) {
  int width = 0;
  for (const auto& pin : node.out_sorted_pins()) {
    width = std::max(width, gu::bits_of(pin));
  }
  return width;
}

// Does `node` expose exactly one consumer edge? Stops at the second.
[[nodiscard]] bool single_consumer(const hhds::Node_class& node, hhds::Pin_class& sink) {
  size_t count = 0;
  for (const auto& edge : node.out_edges()) {
    if (++count > 1) {
      return false;
    }
    sink = edge.sink;
  }
  return count == 1;
}

// A cell a tree may merge through: the operator, a bounded known width, no
// per-cell synthesis policy or native-boundary marker, and no latch operand --
// `clk & latch` (and a chain of such gates) is the integrated clock-gate shape
// clock_gates.cpp recognizes two inputs at a time, so a gate keeps its AND.
[[nodiscard]] bool mergeable(const hhds::Node_class& node) {
  if (gu::is_builtin_node(node) || !reassoc_op(gu::type_op_of(node))) {
    return false;
  }
  if (node.attr(livehd::attrs::synth_policy).has() || node.attr(livehd::attrs::native_comb_boundary).has()) {
    return false;
  }
  const int width = out_width(node);
  if (width <= 0 || width >= kMaxReassocBits) {
    return false;
  }
  for (const auto& sink : node.inp_sorted_pins()) {
    const auto drv = sink.get_driver_pin();
    if (!drv.is_invalid() && !drv.is_const() && gu::type_op_of(drv.get_master_node()) == Ntype_op::Latch) {
      return false;
    }
  }
  return true;
}

// Region identity in the partitioner's terms. A top definition stores flat
// colors; a virtual-flat coloring (pass.color over the flattened hierarchy)
// stores a sub-definition's colors per instance path in `hier_color`, which
// pass/partition/flatten.cpp reads back, so two cells share a region only when
// they agree on every path. Each cell's hier entries fold into one order-free
// signature (cells with no entry, such as freshly inlined loop links, are
// uncolored on every path and agree with each other).
class Region_keys {
public:
  explicit Region_keys(const hhds::Graph& graph) {
    const auto* store = graph.find_attr_store(livehd::attrs::hier_color);
    if (store == nullptr) {
      return;
    }
    for (const auto& [key, color] : *store) {
      auto path     = key;
      path.flat_key = 0;
      const auto h
          = hhds::Hier_attr_key_hash{}(path) ^ (static_cast<uint64_t>(static_cast<uint32_t>(color)) * 0x9e3779b97f4a7c15ULL);
      auto& sig     = signature_[key.flat_key];
      sig.first += mix(h);
      sig.second += mix(h ^ 0xc2b2ae3d27d4eb4fULL) | 1U;
    }
  }
  [[nodiscard]] bool same_region(const hhds::Node_class& a, const hhds::Node_class& b) const {
    if (gu::has_color(a) != gu::has_color(b) || gu::color_of(a) != gu::color_of(b)) {
      return false;
    }
    if (signature_.empty()) {
      return true;
    }
    return signature_of(a) == signature_of(b);
  }

private:
  absl::flat_hash_map<hhds::Attr_key, std::pair<uint64_t, uint64_t>> signature_;

  [[nodiscard]] static uint64_t mix(uint64_t x) {
    x ^= x >> 33U;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33U;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^ (x >> 33U);
  }
  [[nodiscard]] std::pair<uint64_t, uint64_t> signature_of(const hhds::Node_class& node) const {
    const auto key = hhds::make_node_attr_key(static_cast<uint64_t>(node.get_debug_nid() & ~static_cast<hhds::Nid>(3)));
    const auto it  = signature_.find(key);
    return it == signature_.end() ? std::pair<uint64_t, uint64_t>{0, 0} : it->second;
  }
};

// A link that may disappear into its consumer: one consumer edge, into a
// mergeable cell of the same operator in the same synthesis region.
[[nodiscard]] bool structurally_absorbable(const hhds::Node_class& node, const hhds::Node_class& consumer,
                                           const Region_keys& regions) {
  if (consumer.is_invalid() || consumer == node || gu::type_op_of(consumer) != gu::type_op_of(node)) {
    return false;
  }
  if (!regions.same_region(node, consumer)) {
    return false;  // a region cut
  }
  return ctrl_members_of(node) == ctrl_members_of(consumer);
}

// For every bit b in [width(node), read): does extending `node` (as the
// blaster's abc_bit reads it past the driver width) give the same value as
// applying the operator to its operands' bit b? Bits below width(node) are the
// operator itself. A narrower intermediate is therefore exact when each operand
// already extends the way the intermediate does: zeros for an unsigned link
// (each operand provably zero above it), the link's top bit for a signed one
// (each operand repeats its own bit width-1 above).
[[nodiscard]] bool width_neutral(const hhds::Node_class& node, int read) {
  const int width = out_width(node);
  if (width >= read) {
    return true;
  }
  bool unsigned_link = true;
  for (const auto& pin : node.out_sorted_pins()) {
    unsigned_link = gu::is_unsign(pin);
  }
  for (const auto& sink : node.inp_sorted_pins()) {
    const auto drv = sink.get_driver_pin();
    if (drv.is_invalid()) {
      return false;
    }
    const int bits = gu::bits_of(drv);
    if (drv.is_const() && bits == 0) {
      const auto& value = gu::const_of(drv);
      if (value.has_unknowns() || !value.is_numeric()) {
        return false;
      }
      // signed bits: the minimal two's-complement carrier, sign included.
      const int carrier = value.get_signed_bits();
      if (unsigned_link ? (value.is_negative() || carrier > width + 1) : carrier > width) {
        return false;
      }
      continue;
    }
    if (bits <= 0) {
      return false;
    }
    const bool unsigned_operand = gu::is_unsign(drv);
    if (unsigned_link) {
      if (!unsigned_operand || bits > width) {
        return false;
      }
    } else if (unsigned_operand ? bits > width - 1 : bits > width) {
      return false;
    }
  }
  return true;
}

// A cell whose shape a rewrite may change: no per-cell synthesis policy or
// native-boundary marker, and a bounded known width.
[[nodiscard]] bool rewritable(const hhds::Node_class& node) {
  if (node.attr(livehd::attrs::synth_policy).has() || node.attr(livehd::attrs::native_comb_boundary).has()) {
    return false;
  }
  const int width = out_width(node);
  return width > 0 && width < kMaxReassocBits;
}

// A guard operand of an `op` link: Mux(c, x, id) or Mux(c, id, x) with the
// operator's identity on one arm (0 for Or/Xor; for And all ones, which the
// guard rules spell -1 and bitwidth may respell at the Mux's width).
[[nodiscard]] bool guard_operand(const hhds::Pin_class& pin, Ntype_op op) {
  if (pin.is_invalid() || pin.is_const() || gu::is_graph_input_pin(pin)) {
    return false;
  }
  const auto mux = pin.get_master_node();
  if (gu::is_builtin_node(mux) || gu::type_op_of(mux) != Ntype_op::Mux) {
    return false;
  }
  std::array<hhds::Pin_class, 3> in;
  for (const auto& sink : mux.inp_sorted_pins()) {
    const auto pid = sink.get_port_id();
    if (pid > 2) {
      return false;  // a multi-way Mux
    }
    in[pid] = sink.get_driver_pin();
  }
  const int width = out_width(mux);
  for (int arm = 1; arm <= 2; ++arm) {
    if (in[arm].is_invalid() || !in[arm].is_const()) {
      continue;
    }
    const auto& value = gu::const_of(in[arm]);
    if (value.has_unknowns() || !value.is_just_i64()) {
      continue;
    }
    const auto v = value.to_just_i64();
    if (op != Ntype_op::And) {
      if (v == 0) {
        return true;
      }
    } else if (v == -1 || (width > 0 && width < 63 && v == (int64_t{1} << width) - 1)) {
      return true;
    }
  }
  return false;
}

class Reassociator {
public:
  Reassociator(hhds::Graph* graph, Reassoc_stats& stats, Preparation_budget* budget, const Reassoc_scope& scope,
               Reassoc_trees trees)
      : graph_(graph), stats_(stats), budget_(budget), scope_(scope), trees_(trees), regions_(*graph) {}

  bool run() {
    if (!guard_terms()) {
      return false;
    }
    std::vector<hhds::Node_class> roots;
    for (const auto node : graph_->body().nodes()) {
      if (!admit_preparation(budget_, "reassoc-scan")) {
        return false;
      }
      if (!in_scope(node) || !is_mergeable(node)) {
        continue;
      }
      hhds::Pin_class sink;
      if (single_consumer(node, sink) && !sink.is_invalid() && in_scope(sink.get_master_node())
          && is_mergeable(sink.get_master_node()) && structurally_absorbable(node, sink.get_master_node(), regions_)) {
        absorbable_.insert(node.get_class_index());
      } else {
        roots.push_back(node);
      }
    }
    // A root that rejects a narrower link on width grounds hands that link
    // back here as a root of its own subtree, so each cell is visited once.
    while (!roots.empty()) {
      auto root = roots.back();
      roots.pop_back();
      if (!admit_preparation(budget_, "reassoc-root")) {
        return false;
      }
      flatten(root, roots);
    }
    return true;
  }

private:
  hhds::Graph*                                 graph_;
  Reassoc_stats&                               stats_;
  Preparation_budget*                          budget_;
  const Reassoc_scope&                         scope_;
  Reassoc_trees                                trees_;
  Region_keys                                  regions_;
  absl::flat_hash_set<hhds::Class_index>       absorbable_;
  absl::flat_hash_map<hhds::Class_index, bool> mergeable_;

  // Guarded accumulation. An unrolled `if c { acc |= x }` is
  // Mux(c, a, Or(a, x)): the hold arm `a` and the same `a` inside the update
  // arm. One per iteration is a serial MUX chain (each link's `a` is the
  // previous Mux) that neither the merge below nor the mapper restructures.
  // Pushing the guard onto the term, Or(a, Mux(c, 0, x)), is the same value
  // bit for bit and leaves each link reading the previous accumulator once,
  // so the chain becomes a plain single-consumer Or chain the merge turns
  // into one balanced n-ary cell. Xor is the same with 0; And holds with all
  // ones: And(a, Mux(c, ~0, x)). The update arm may be either arm.
  //
  // The rewrite swaps the two cells' types in place (the Mux becomes the
  // operator, the operator becomes the guard Mux), so both keep their
  // colors, per-instance regions, control-cone membership and source
  // attributes; it applies only when both sit in one region, the operator
  // cell has exactly the two operands {a, x} and the Mux as its only
  // consumer, and the operator cell is at least as wide as the Mux (each bit
  // the Mux drives is then the operator's own bit, never its extension). A
  // rewritten Mux is an operator from then on, so each Mux rewrites at most
  // once; its consumer is re-examined (a nested `if c1 { if c2 { acc |= x } }`
  // matches from the inside out). A held state output (Flop/Latch/Memory/
  // Sub) stays a direct Mux arm, as in cprop. O(V + E).
  bool guard_terms() {
    std::vector<hhds::Node_class> work;
    for (const auto node : graph_->body().nodes()) {
      if (!admit_preparation(budget_, "reassoc-guard-scan")) {
        return false;
      }
      if (!gu::is_builtin_node(node) && gu::type_op_of(node) == Ntype_op::Mux && in_scope(node)) {
        work.push_back(node);
      }
    }
    while (!work.empty()) {
      const auto mux = work.back();
      work.pop_back();
      if (!admit_preparation(budget_, "reassoc-guard")) {
        return false;
      }
      if (!guard_term(mux)) {
        continue;
      }
      ++stats_.guarded;
      for (const auto& edge : mux.out_edges()) {
        const auto consumer = edge.sink.get_master_node();
        if (!gu::is_builtin_node(consumer) && gu::type_op_of(consumer) == Ntype_op::Mux && in_scope(consumer)) {
          work.push_back(consumer);
        }
      }
    }
    return true;
  }

  bool guard_term(const hhds::Node_class& mux) {
    if (gu::type_op_of(mux) != Ntype_op::Mux || !rewritable(mux)) {
      return false;
    }
    std::array<hhds::Pin_class, 3> in;
    for (const auto& sink : mux.inp_sorted_pins()) {
      const auto pid = sink.get_port_id();
      if (pid > 2) {
        return false;  // a multi-way Mux
      }
      in[pid] = sink.get_driver_pin();
    }
    const auto& sel = in[0];
    if (sel.is_invalid() || sel.is_const() || in[1].is_invalid() || in[2].is_invalid()) {
      return false;
    }
    const int width = out_width(mux);
    for (int update = 1; update <= 2; ++update) {
      const auto& upd  = in[update];
      const auto& hold = in[3 - update];
      if (upd.is_const() || gu::is_graph_input_pin(upd)) {
        continue;
      }
      if (!hold.is_const() && !gu::is_graph_input_pin(hold) && Ntype::is_loop_last(gu::type_op_of(hold.get_master_node()))) {
        return false;  // a state hold stays a Mux arm (cprop's rule, the latch contract)
      }
      const auto cell = upd.get_master_node();
      if (cell == mux || !in_scope(cell) || !mergeable(cell) || out_width(cell) < width) {
        continue;
      }
      hhds::Pin_class consumer;
      if (!single_consumer(cell, consumer) || consumer.get_master_node() != mux
          || consumer.get_port_id() != static_cast<hhds::Port_id>(update)) {
        continue;
      }
      if (!regions_.same_region(cell, mux) || ctrl_members_of(cell) != ctrl_members_of(mux)) {
        continue;  // a region cut
      }
      std::vector<hhds::Pin_class> operands;
      for (const auto& sink : cell.inp_sorted_pins()) {
        operands.push_back(sink.get_driver_pin());
      }
      if (operands.size() != 2 || operands[0].is_invalid() || operands[1].is_invalid()) {
        continue;
      }
      const int at = operands[0] == hold ? 0 : (operands[1] == hold ? 1 : -1);
      if (at < 0 || operands[1 - at] == hold) {
        continue;
      }
      const auto term = operands[1 - at];
      const auto op   = gu::type_op_of(cell);
      const auto id   = gu::create_const(*graph_, *Dlop::create_integer(op == Ntype_op::And ? -1 : 0));
      for (const auto& sink : cell.inp_pins_snapshot()) {
        sink.del_sink();
      }
      for (const auto& sink : mux.inp_pins_snapshot()) {
        sink.del_sink();
      }
      // The operator cell becomes the guard: the term where the Mux took the
      // update, the identity where it held.
      gu::set_type_op(cell, Ntype_op::Mux);
      cell.create_sink_pin(0).connect_driver(sel);
      cell.create_sink_pin(static_cast<hhds::Port_id>(update)).connect_driver(term);
      cell.create_sink_pin(static_cast<hhds::Port_id>(3 - update)).connect_driver(id);
      // The Mux becomes the operator over the held value and the guard.
      gu::set_type_op(mux, op);
      mux.create_sink_pin(0).connect_driver(hold);
      mux.create_sink_pin(1).connect_driver(cell.get_driver_pin(0));
      return true;
    }
    return false;
  }

  [[nodiscard]] bool in_scope(const hhds::Node_class& node) const { return !scope_ || scope_(node); }
  [[nodiscard]] bool is_absorbable(const hhds::Node_class& node) const { return absorbable_.contains(node.get_class_index()); }
  [[nodiscard]] bool is_mergeable(const hhds::Node_class& node) {
    auto [it, fresh] = mergeable_.try_emplace(node.get_class_index(), false);
    if (fresh) {
      it->second = mergeable(node);
    }
    return it->second;
  }

  void flatten(const hhds::Node_class& root, std::vector<hhds::Node_class>& roots) {
    const auto op   = gu::type_op_of(root);
    const int  read = out_width(root);
    std::vector<hhds::Pin_class>  pending;
    std::vector<hhds::Pin_class>  leaves;
    std::vector<hhds::Node_class> absorbed;
    const auto                    push_operands = [&](const hhds::Node_class& node) {
      const auto sinks = node.inp_sorted_pins();
      const auto first = pending.size();
      for (const auto& sink : sinks) {
        pending.push_back(sink.get_driver_pin());
      }
      std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(first), pending.end());  // left-to-right pop order
    };
    push_operands(root);
    while (!pending.empty()) {
      const auto pin = pending.back();
      pending.pop_back();
      if (pin.is_invalid()) {
        return;  // an undriven operand: leave the tree as written
      }
      if (!pin.is_const() && !gu::is_graph_input_pin(pin)) {
        const auto child = pin.get_master_node();
        if (child == root) {
          return;  // a combinational ring through the root: leave it intact
        }
        if (is_absorbable(child)) {
          if (width_neutral(child, read)) {
            absorbed.push_back(child);
            push_operands(child);
            continue;
          }
          roots.push_back(child);  // merges its own subtree at its own width
        }
      }
      leaves.push_back(pin);
    }
    if (absorbed.empty()) {
      return;
    }
    if (trees_ == Reassoc_trees::guarded) {
      // Every link ORs (XORs, ANDs) in a guard; anything else stays a chain.
      const auto guarded = [op](const hhds::Node_class& link) {
        return std::ranges::any_of(link.inp_sorted_pins(),
                                   [op](const hhds::Pin_class& sink) { return guard_operand(sink.get_driver_pin(), op); });
      };
      if (!guarded(root) || !std::ranges::all_of(absorbed, guarded)) {
        return;
      }
    }
    // And/Or are idempotent; Xor cancels pairs. Keep first-occurrence order.
    absl::flat_hash_map<hhds::Class_index, size_t> seen;
    std::vector<hhds::Pin_class>                   operands;
    std::vector<size_t>                            count;
    for (const auto& pin : leaves) {
      auto [it, fresh] = seen.try_emplace(pin.get_class_index(), operands.size());
      if (fresh) {
        operands.push_back(pin);
        count.push_back(0);
      }
      ++count[it->second];
    }
    std::vector<hhds::Pin_class> kept;
    kept.reserve(operands.size());
    for (size_t i = 0; i < operands.size(); ++i) {
      if (op != Ntype_op::Xor || (count[i] & 1U) != 0) {
        kept.push_back(operands[i]);
      }
    }
    if (kept.empty()) {
      return;  // a fully cancelling Xor tree: not worth a constant rewrite here
    }
    for (const auto& sink : root.inp_pins_snapshot()) {
      sink.del_sink();
    }
    for (auto& node : absorbed) {
      node.del_node();
    }
    // One operand bank: operand k is pid k (cell.hpp sink-bank convention).
    for (size_t i = 0; i < kept.size(); ++i) {
      root.create_sink_pin(static_cast<hhds::Port_id>(i)).connect_driver(kept[i]);
    }
    ++stats_.roots;
    stats_.absorbed += absorbed.size();
  }
};
}  // namespace

bool reassociate_chains(hhds::Graph* graph, Reassoc_stats& stats, Preparation_budget* budget, const Reassoc_scope& scope,
                        Reassoc_trees trees) {
  if (!graph) {
    return true;
  }
  return Reassociator{graph, stats, budget, scope, trees}.run();
}

}  // namespace livehd::synth
