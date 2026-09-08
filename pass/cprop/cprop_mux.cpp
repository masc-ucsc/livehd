// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <limits>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cprop.hpp"

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
    const auto edges = node.inp_edges();
    if (edges.size() != 2 || edges[0].sink.get_port_id() != 0 || edges[1].sink.get_port_id() != 0) {
      return {};
    }
    auto a = edges[0].driver;
    auto b = edges[1].driver;
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
    if (gu::bits_of(p) == 1 && gu::is_unsign(p)) {
      return p;
    }
    auto n = gu::create_typed_node(graph, Ntype_op::Ror, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    n.create_sink_pin(0).connect_driver(p);
    return n.create_driver_pin(0);
  }

  Pin negate(Pin p) {
    if (p == zero) {
      return one;
    }
    if (p == one) {
      return zero;
    }
    auto n = gu::create_typed_node(graph, Ntype_op::EQ, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    n.create_sink_pin(0).connect_driver(p);
    n.create_sink_pin(0).connect_driver(zero);
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
    auto n = gu::create_typed_node(graph, Ntype_op::And, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    n.create_sink_pin(0).connect_driver(a);
    n.create_sink_pin(0).connect_driver(b);
    return n.create_driver_pin(0);
  }

  Pin disjunction(const std::vector<Pin>& pins) {
    if (pins.empty()) {
      return zero;
    }
    if (pins.size() == 1) {
      return pins.front();
    }
    auto n = gu::create_typed_node(graph, Ntype_op::Or, 1);
    gu::set_ubits(n.create_driver_pin(0), 1);
    for (auto p : pins) {
      n.create_sink_pin(0).connect_driver(p);
    }
    return n.create_driver_pin(0);
  }

  void collect() {
    for (auto node : graph.body().nodes(hhds::Node_order::forward)) {
      const auto op = gu::type_op_of(node);
      if ((op != Ntype_op::Mux && op != Ntype_op::Hotmux) || !node.has_out_edges() || gu::bits_of(node.get_driver_pin(0)) < 4
          || gu::has_color(node) || gu::has_runtime_check(node)) {
        continue;
      }
      // Dense pid indexing avoids sorting high-fanin Hotmuxes. Reject malformed
      // or gapped cells without reinterpreting their port numbers.
      const auto       edges = node.inp_edges();
      std::vector<Pin> pins(edges.size());
      bool             valid = true;
      for (const auto& e : edges) {
        const auto pid = e.sink.get_port_id();
        if (pid >= pins.size() || !pins[pid].is_invalid() || e.driver.is_invalid()
            || (e.driver.is_const() && gu::const_of(e.driver).has_unknowns())) {
          valid = false;
          break;
        }
        pins[pid] = e.driver;
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
      candidate_ids.emplace(node.get_class_index(), candidates.size());
      candidates.push_back(std::move(c));
    }
    // One ownership decision per candidate. Shared outputs, controls, color,
    // and width/sign boundaries stop a region. Every candidate then belongs
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
      auto        p      = parent.node.get_driver_pin(0);
      auto        q      = c.node.get_driver_pin(0);
      if (gu::bits_of(p) != gu::bits_of(q) || gu::is_unsign(p) != gu::is_unsign(q)) {
        continue;
      }
      const auto pid  = consumer->sink.get_port_id();
      const bool data = gu::type_op_of(parent.node) == Ntype_op::Mux ? pid != 0 : (pid % 2 != 0 || pid == parent.arms.size() * 2);
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
    auto q = flop.get_driver_pin(0);
    auto d = root.node.get_driver_pin(0);
    if (gu::bits_of(q) != gu::bits_of(d) || gu::is_unsign(q) != gu::is_unsign(d)) {
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
    auto&              root = candidates[root_id];
    std::vector<Visit> visits{
        {root_id, {}, one}
    };
    std::vector<Group>                  groups;
    absl::flat_hash_map<size_t, size_t> group_ids;
    size_t                              old_cost  = 0;
    size_t                              terminals = 0;
    size_t                              branches  = 0;
    for (size_t i = 0; i < visits.size(); ++i) {
      const auto  id  = visits[i].candidate;
      const auto& c   = candidates[id];
      old_cost       += c.arms.size();
      std::vector<Target> targets;
      auto                append = [&](Pin p) {
        ++branches;
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
    auto   flop = hold_flop(root);
    size_t hold = absent;
    if (!flop.is_invalid()) {
      for (size_t k = 0; k < groups.size(); ++k) {
        if (groups[k].value == flop.get_driver_pin(0)) {
          hold = k;
        }
      }
    }
    const size_t data_count = groups.size() - (hold != absent);
    if (!data_count) {
      return;
    }
    const size_t new_cost = data_count - 1;
    // Charge only private, actually removable data muxes, and conservatively
    // allow two new one-bit gates per branch. No graph mutation before this
    // check. Repeated alternatives must pay for their shared control logic.
    if ((terminals == groups.size() && hold == absent) || old_cost <= new_cost
        || static_cast<uint64_t>(gu::bits_of(root.node.get_driver_pin(0))) * (old_cost - new_cost) <= 2 * branches) {
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
      for (auto e : sink.inp_edges()) {
        e.del_edge();
      }
      sink.connect_driver(update);
    }
    // Retain the root and its output metadata/name: other regions may use it
    // as an opaque terminal. Replacing that pin would invalidate their snapshot.
    for (auto e : root.node.inp_edges()) {
      e.del_edge();
    }
    gu::clear_proven(root.node);
    if (data.size() == 1) {
      gu::set_type_op(root.node, Ntype_op::Or);
      root.node.create_sink_pin(0).connect_driver(data.front());
    } else {
      gu::set_type_op(root.node, Ntype_op::Hotmux);
      for (size_t i = 0; i + 1 < data.size(); ++i) {
        root.node.create_sink_pin(2 * i).connect_driver(controls[i]);
        root.node.create_sink_pin(2 * i + 1).connect_driver(data[i]);
      }
      root.node.create_sink_pin(2 * (data.size() - 1)).connect_driver(data.back());
      // Binary mux paths partition the input space; absorbed Hotmuxes already
      // had an exclusivity proof. Grouping these disjoint predicates preserves it.
      gu::set_proven(root.node, gu::kFormalOnehot);
    }
    // Parent before child: each absorbed cell had exactly one outgoing edge.
    // Explicitly remove proven Hotmuxes too; generic DCE preserves obligations.
    for (size_t i = 1; i < visits.size(); ++i) {
      auto n = candidates[visits[i].candidate].node;
      I(!n.has_out_edges());
      n.del_node();
    }
  }

public:
  explicit Mux_sharing(hhds::Graph& g) : graph(g) {
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

void Cprop::mux_share_pass() { Mux_sharing(*current_graph).run(); }
