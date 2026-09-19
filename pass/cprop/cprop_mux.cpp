// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <limits>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cprop.hpp"
#include "cprop_value.hpp"

namespace {
namespace gu            = livehd::graph_util;
using Pin               = hhds::Pin_class;
using Node              = hhds::Node_class;
constexpr size_t absent = std::numeric_limits<size_t>::max();

// Compare constants by representation; other terminals by pin identity. The
// per-pin ID cache below pays for hashing a wide constant only once per pass.
struct Value_hash {
  size_t operator()(const Pin& p) const { return p.is_const() ? gu::const_of(p).hash() : p.get_class_index().value; }
};
struct Value_equal {
  bool operator()(const Pin& a, const Pin& b) const {
    return a == b || (a.is_const() && b.is_const() && gu::const_of(a).same_repr(gu::const_of(b)));
  }
};

struct Decode {
  Pin     selector;
  int64_t value = 0;
};

// The ONE consumer of `node`, or nothing when it has zero or several. hhds
// out_edges() is a lazy range whose size()/front() each re-walk it (a clock-like
// net has a 100k-edge fan-out), so stop at the second edge and reuse the first.
[[nodiscard]] std::optional<hhds::Edge_class> sole_consumer(const Node& node) {
  auto range = node.out_edges();
  auto it    = range.begin();
  if (it == range.end()) {
    return std::nullopt;
  }
  const auto edge = *it;
  if (++it != range.end()) {
    return std::nullopt;
  }
  return edge;
}

class Mux_sharing {
  struct Candidate {
    Node                             node;
    std::vector<std::pair<Pin, Pin>> arms;
    Pin                              fallback;
    size_t                           parent = absent;
  };
  struct Target {
    size_t index;
    bool   internal;
  };
  struct Visit {
    size_t              candidate;
    std::vector<Target> targets;
    Pin                 active;
  };
  struct Group {
    Pin              value;
    std::vector<Pin> conditions;
  };

  hhds::Graph&                                                  graph;
  bool                                                          state_context;
  std::function<bool(const Pin&)>                               boolean_fact;
  Pin                                                           zero;
  Pin                                                           one;
  std::vector<Candidate>                                        candidates;
  absl::flat_hash_map<hhds::Class_index, size_t>                candidate_ids;
  absl::flat_hash_map<hhds::Class_index, std::optional<Decode>> decodes;
  absl::flat_hash_map<hhds::Class_index, size_t>                value_ids;
  absl::flat_hash_map<Pin, size_t, Value_hash, Value_equal>     values;

  std::optional<Decode> decode(Pin control) {
    auto [it, fresh] = decodes.try_emplace(control.get_class_index());
    if (!fresh) {
      return it->second;
    }
    const auto node = control.get_master_node();
    if (control.is_const() || gu::type_op_of(node) != Ntype_op::EQ) {
      return {};
    }
    const auto edges = node.inp_pins_snapshot();
    // EQ is a SINGLE-BANK commutative cell, so its two operands each own a
    // sink pid -- 0 and 1 -- rather than sharing pin 0 (graph/cell.hpp's ONE
    // DRIVER PER SINK PIN block). Requiring BOTH on pid 0 made this decode
    // unsatisfiable, silently disabling the whole mux-chain decode rather than
    // miscompiling. Check the BANK, which is 0 for every EQ operand and is
    // what "these are the two compared values" actually means.
    if (edges.size() != 2 || Ntype::sink_bank(Ntype_op::EQ, edges[0].get_port_id()) != 0
        || Ntype::sink_bank(Ntype_op::EQ, edges[1].get_port_id()) != 0) {
      return {};
    }
    auto a = edges[0].get_driver_pin();
    auto b = edges[1].get_driver_pin();
    if (a.is_const()) {
      std::swap(a, b);
    }
    if (a.is_const() || !b.is_const()) {
      return {};
    }
    const auto& c = gu::const_of(b);
    if (c.has_unknowns() || !c.is_just_i64()) {
      return {};
    }
    // EQ compares LGraph integer VALUES. The same selector cannot equal two
    // distinct integers; no bit-vector truncation or signed cast is assumed.
    it->second = Decode{a, c.to_just_i64()};
    return it->second;
  }

  bool exclusive(const Candidate& c) {
    if (gu::proven_of(c.node) == gu::kFormalOnehot) {
      return true;
    }
    Pin                          selector;
    absl::flat_hash_set<int64_t> constants;
    for (const auto& [control, value] : c.arms) {
      (void)value;
      auto d = decode(control);
      if (!d || (!selector.is_invalid() && selector != d->selector) || !constants.insert(d->value).second) {
        return false;
      }
      selector = d->selector;
    }
    return !selector.is_invalid();
  }

  size_t value_id(Pin p) {
    auto [it, fresh] = value_ids.try_emplace(p.get_class_index(), 0);
    if (fresh) {
      it->second = values.try_emplace(p, values.size()).first->second;
    }
    return it->second;
  }

  // Predicate operations use {0,1} values, including for wide or signed mux
  // selectors. NOT is equality with zero, never bitwise complement of u1.
  Pin boolean(Pin p) {
    if (p.is_const()) {
      return gu::const_of(p).is_known_zero() ? zero : one;
    }
    if (boolean_fact(p)) {
      return p;
    }
    auto n = livehd::cprop_value::make_node(graph, Ntype_op::Ror, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    livehd::graph_util::setup_sink_pid(n, 0).connect_driver(p);
    return n.create_driver_pin(0);
  }

  Pin negate(Pin p) {
    if (p == zero) {
      return one;
    }
    if (p == one) {
      return zero;
    }
    auto n = livehd::cprop_value::make_node(graph, Ntype_op::EQ, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    livehd::graph_util::setup_sink_pid(n, 0).connect_driver(p);
    livehd::graph_util::setup_sink_pid(n, 0).connect_driver(zero);
    return n.create_driver_pin(0);
  }

  Pin conjunction(Pin a, Pin b) {
    if (a == zero || b == zero) {
      return zero;
    }
    if (a == one || a == b) {
      return b;
    }
    if (b == one) {
      return a;
    }
    auto n = livehd::cprop_value::make_node(graph, Ntype_op::And, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    livehd::graph_util::setup_sink_pid(n, 0).connect_driver(a);
    livehd::graph_util::setup_sink_pid(n, 0).connect_driver(b);
    return n.create_driver_pin(0);
  }

  Pin disjunction(const std::vector<Pin>& pins) {
    if (pins.empty()) {
      return zero;
    }
    if (pins.size() == 1) {
      return pins.front();
    }
    auto n = livehd::cprop_value::make_node(graph, Ntype_op::Or, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    for (auto p : pins) {
      livehd::graph_util::setup_sink_pid(n, 0).connect_driver(p);
    }
    return n.create_driver_pin(0);
  }

  void collect() {
    // Region ownership is independent of topological order: roots keep their
    // identity, and only their private interiors can be removed. State mode
    // collects just private data regions rooted at flop D pins.
    std::vector<Node> pending;
    for (auto node : graph.body().nodes()) {
      if (!state_context) {
        pending.push_back(node);
        continue;
      }
      if (gu::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto data = gu::get_driver_of_sink_name(node, "din");
      if (!data.is_invalid() && !data.is_const() && sole_consumer(data.get_master_node())) {
        pending.push_back(data.get_master_node());
      }
    }
    absl::flat_hash_set<hhds::Class_index> inspected;
    for (size_t next = 0; next < pending.size(); ++next) {
      auto node = pending[next];
      if (state_context && !inspected.insert(node.get_class_index()).second) {
        continue;
      }
      const auto op = gu::type_op_of(node);
      if ((op != Ntype_op::Mux && op != Ntype_op::Hotmux) || !node.has_out_edges() || gu::has_color(node)
          || gu::has_runtime_check(node)) {
        continue;
      }
      // Dense pid indexing avoids sorting high-fanin Hotmuxes. Reject malformed
      // or gapped cells without reinterpreting their port numbers.
      // Read-only scan over the node's own sink-pin list (already ascending by
      // port id); no Edge_class is materialized for a 320-arm Hotmux.
      size_t n_in = 0;
      for ([[maybe_unused]] auto isnk : node.inp_sorted_pins()) {
        ++n_in;
      }
      std::vector<Pin> pins(n_in);
      bool             valid = true;
      for (auto isnk : node.inp_sorted_pins()) {
        const auto pid  = isnk.get_port_id();
        auto       idrv = isnk.get_driver_pin();
        if (pid >= pins.size() || !pins[pid].is_invalid() || idrv.is_invalid()
            || (idrv.is_const() && gu::const_of(idrv).has_unknowns())) {
          valid = false;
          break;
        }
        pins[pid] = idrv;
      }
      if (!valid || pins.size() < 2) {
        continue;
      }
      Candidate c;
      c.node = node;
      if (op == Ntype_op::Mux) {
        if (pins.size() != 3) {
          continue;  // index muxes require a different decode
        }
        c.arms.emplace_back(pins[0], pins[2]);
        c.fallback = pins[1];
      } else {
        for (size_t i = 0; i + 1 < pins.size(); i += 2) {
          c.arms.emplace_back(pins[i], pins[i + 1]);
        }
        c.fallback = pins.size() % 2 ? pins.back() : zero;
        if (!exclusive(c)) {
          continue;  // retain the ORIGINAL overlap obligation
        }
      }
      if (state_context) {
        const auto append = [&](Pin pin) {
          if (!pin.is_const() && sole_consumer(pin.get_master_node())) {
            pending.push_back(pin.get_master_node());
          }
        };
        for (const auto& [control, value] : c.arms) {
          (void)control;
          append(value);
        }
        append(c.fallback);
      }
      candidate_ids.emplace(node.get_class_index(), candidates.size());
      candidates.push_back(std::move(c));
    }
    // One ownership decision per candidate. Shared outputs, controls, color,
    // and explicit value operations stop a region. Every candidate then belongs
    // to exactly one tree, even if the enclosing dataflow is a reconvergent DAG.
    for (auto& c : candidates) {
      const auto consumer = sole_consumer(c.node);
      if (!consumer) {
        continue;
      }
      auto it = candidate_ids.find(consumer->sink.get_master_node().get_class_index());
      if (it == candidate_ids.end()) {
        continue;
      }
      const auto& parent = candidates[it->second];
      const auto  pid    = consumer->sink.get_port_id();
      const bool  data = gu::type_op_of(parent.node) == Ntype_op::Mux ? pid != 0 : (pid % 2 != 0 || pid == parent.arms.size() * 2);
      if (data) {
        c.parent = it->second;
      }
    }
  }

  Node hold_flop(const Candidate& root) const {
    const auto consumer = sole_consumer(root.node);
    if (!consumer) {
      return {};
    }
    auto flop = consumer->sink.get_master_node();
    if (gu::type_op_of(flop) != Ntype_op::Flop || gu::has_color(flop)
        || consumer->sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Flop, "din")) {
      return {};
    }
    // A depth-d Flop is a shift register. Feeding back its LAST Q is not the
    // same as disabling every stage, so only a fixed depth of one qualifies.
    for (auto name : {"pipe_min", "pipe_max"}) {
      auto p = gu::get_driver_of_sink_name(flop, name);
      if (!p.is_invalid() && (!p.is_const() || !gu::const_of(p).is_just_i64() || gu::const_of(p).to_just_i64() != 1)) {
        return {};
      }
    }
    auto en = gu::get_driver_of_sink_name(flop, "enable");
    if (en.is_const() && gu::const_of(en).has_unknowns()) {
      return {};
    }
    return flop;
  }

  void rewrite(size_t root_id) {
    auto& root = candidates[root_id];
    if (state_context && hold_flop(root).is_invalid()) {
      return;
    }
    std::vector<Visit> visits{
        {root_id, {}, one}
    };
    std::vector<Group>                  groups;
    absl::flat_hash_map<size_t, size_t> group_ids;
    size_t                              old_cost  = 0;
    size_t                              terminals = 0;
    for (size_t i = 0; i < visits.size(); ++i) {
      const auto  id  = visits[i].candidate;
      const auto& c   = candidates[id];
      old_cost       += c.arms.size();
      std::vector<Target> targets;
      auto                append = [&](Pin p) {
        auto child = candidate_ids.find(p.get_master_node().get_class_index());
        if (!p.is_const() && child != candidate_ids.end() && candidates[child->second].parent == id) {
          targets.push_back({visits.size(), true});
          visits.push_back({child->second, {}, {}});
        } else {
          ++terminals;
          auto [it, fresh] = group_ids.try_emplace(value_id(p), groups.size());
          if (fresh) {
            groups.push_back({p, {}});
          }
          targets.push_back({it->second, false});
        }
      };
      for (const auto& [control, value] : c.arms) {
        (void)control;
        append(value);
      }
      append(c.fallback);
      visits[i].targets = std::move(targets);
    }
    if (!state_context) {
      if (auto consumer = sole_consumer(root.node); consumer) {
        auto state = consumer->sink.get_master_node();
        if (gu::is_type_register(state) && consumer->sink.get_port_id() == Ntype::get_sink_pid(gu::type_op_of(state), "din")) {
          for (const auto& group : groups) {
            if (group.value == state.get_driver_pin(0)) {
              return;
            }
          }
        }
      }
    }
    auto   flop = hold_flop(root);
    size_t hold = absent;
    if (!flop.is_invalid()) {
      for (size_t k = 0; k < groups.size(); ++k) {
        if (groups[k].value == flop.get_driver_pin(0)) {
          hold = k;
        }
      }
    }
    // Q-hold regions stay in their original Mux form until enableopt has
    // interpreted the destination's conditions. Only that pass extracts hold.
    if ((hold != absent) != state_context) {
      return;
    }
    const size_t data_count = groups.size() - (hold != absent);
    if (!data_count) {
      return;
    }
    // A selection among PROVEN 0/1 values stays the Mux nest it is. Flattening
    // pays one conjunction per path and one disjunction per value -- 1-bit
    // predicate gates, as wide as the data -- to save 1-bit selects, and the
    // scalar folds already turn a nest's constant arms into And/Or. (An LRU bit
    // matrix lane became ~10 gates plus a Hotmux here.) The proof is cprop's
    // own structural bound, never a width hint; a flop hold arm is excluded
    // because its enable extraction is handled on the flop itself.
    bool all_bool01 = true;
    for (size_t k = 0; k < groups.size() && all_bool01; ++k) {
      all_bool01 = k == hold || boolean_fact(groups[k].value);
    }
    if (all_bool01) {
      return;
    }
    const size_t new_cost = data_count - 1;
    // Count only private, removable word muxes. Width annotations cannot
    // influence this structural heuristic; repeated alternatives or a hold
    // must reduce the number of data selections.
    if ((terminals == groups.size() && hold == absent) || old_cost <= new_cost) {
      return;
    }

    for (size_t i = 0; i < visits.size(); ++i) {
      auto&            visit = visits[i];
      const auto&      c     = candidates[visit.candidate];
      std::vector<Pin> controls;
      for (const auto& [control, value] : c.arms) {
        (void)value;
        controls.push_back(boolean(control));
      }
      const auto fallback = negate(disjunction(controls));
      controls.push_back(fallback);
      for (size_t j = 0; j < controls.size(); ++j) {
        const auto active = conjunction(visit.active, controls[j]);
        const auto target = visit.targets[j];
        if (target.internal) {
          visits[target.index].active = active;
        } else {
          groups[target.index].conditions.push_back(active);
        }
      }
    }
    std::vector<Pin> controls;
    std::vector<Pin> data;
    for (size_t k = 0; k < groups.size(); ++k) {
      if (k == hold) {
        continue;
      }
      controls.push_back(disjunction(groups[k].conditions));
      data.push_back(groups[k].value);
    }
    if (hold != absent) {
      auto en     = gu::get_driver_of_sink_name(flop, "enable");
      auto update = disjunction(controls);
      if (!en.is_invalid()) {
        update = conjunction(boolean(en), update);
      }
      auto sink = gu::setup_sink_by_name(flop, "enable");
      sink.del_sink();  // one driver per sink pin: the whole old enable
      sink.connect_driver(update);
    }
    // Retain the root and its output metadata/name: other regions may use it
    // as an opaque terminal. Replacing that pin would invalidate their snapshot.
    // SNAPSHOT: del_sink() is structural, so a lazy pin view would be
    // invalidated by the first disconnect. Only EDGES go, never a pin or the
    // node, so the later elements stay valid.
    for (const auto& spin : root.node.inp_pins_snapshot()) {
      spin.del_sink();
    }
    gu::clear_proven(root.node);
    if (data.size() == 1) {
      gu::set_type_op(root.node, Ntype_op::Or);
      livehd::graph_util::setup_sink_pid(root.node, 0).connect_driver(data.front());
    } else {
      gu::set_type_op(root.node, Ntype_op::Hotmux);
      for (size_t i = 0; i + 1 < data.size(); ++i) {
        livehd::graph_util::setup_sink_pid(root.node, 2 * i).connect_driver(controls[i]);
        livehd::graph_util::setup_sink_pid(root.node, 2 * i + 1).connect_driver(data[i]);
      }
      livehd::graph_util::setup_sink_pid(root.node, 2 * (data.size() - 1)).connect_driver(data.back());
      // Binary mux paths partition the input space; absorbed Hotmuxes already
      // had an exclusivity proof. Grouping these disjoint predicates preserves it.
      gu::set_proven(root.node, gu::kFormalOnehot);
    }
    // Parent before child: each absorbed cell had exactly one outgoing edge.
    // Explicitly remove proven Hotmuxes too; generic DCE preserves obligations.
    for (size_t i = 1; i < visits.size(); ++i) {
      auto n = candidates[visits[i].candidate].node;
      I(!n.has_out_edges());
      livehd::cprop_value::retire(n);
    }
  }

public:
  explicit Mux_sharing(hhds::Graph& g, bool state, const std::function<bool(const Pin&)>& proof)
      : graph(g), state_context(state), boolean_fact(proof ? proof : livehd::cprop_value::is_bool01) {
    zero = gu::create_const(graph, *Dlop::create_integer(0));
    one  = gu::create_const(graph, *Dlop::create_integer(1));
  }
  void run() {
    collect();
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (candidates[i].parent == absent) {
        rewrite(i);
      }
    }
  }
};
}  // namespace

void Cprop::mux_share_pass() {
  livehd::cprop_profile::Timer timer(livehd::cprop_profile::sharing);
  livehd::share_mux_regions(*current_graph, false);
}

void livehd::share_mux_regions(hhds::Graph& graph, bool state_context, const std::function<bool(const Pin&)>& boolean_fact) {
  Mux_sharing(graph, state_context, boolean_fact).run();
}
