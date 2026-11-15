// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cprop.hpp"
#include "cprop_muxctx.hpp"
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
  // One conjunct of a path condition: the control of ONE arm (positive), or the negated OR of the controls of
  // every arm that was passed over (negative). Keeping the conjuncts, not just their AND, lets a later positive
  // equality drop the negative ones it already implies.
  struct Literal {
    std::vector<Pin>                        arms;
    bool                                    positive;
    std::optional<livehd::muxctx::Equality> equal;    // positive: the single arm is `v == k`
    std::vector<livehd::muxctx::Equality>   unequal;  // negative: every arm is `v == k`
    bool                                    unequal_all = false;
    Pin                                     uniform;  // negative over ONE value v (every arm `v == k_i`): v
  };
  // A path is a persistent list: siblings share their prefix, and each node memoizes the conjunction up to it.
  struct Path_node {
    size_t literal;
    size_t parent;
    Pin    condition;
    // The trailing run of negatives over this node's `uniform` value (a decode
    // chain's `!(v == 0) & !(v == 1) & ...`): its first node above. Lets
    // extend_path skip a whole implied run at once.
    size_t run_top = absent;
    // Skew-binary jump pointer (O(log depth) ancestor queries, O(1) per append).
    size_t depth = 0;
    size_t jump  = absent;
  };
  struct Visit {
    size_t                     candidate;
    std::vector<Target>        targets;
    size_t                     path = absent;
    livehd::muxctx::Path_facts facts;
  };
  struct Group {
    Pin              value;
    std::vector<Pin> conditions;
  };

  hhds::Graph&                                              graph;
  bool                                                      state_context;
  bool                                                      hotmux_trees;
  std::function<bool(const Pin&)>                           boolean_fact;
  Pin                                                       zero;
  Pin                                                       one;
  std::vector<Candidate>                                    candidates;
  absl::flat_hash_map<hhds::Class_index, size_t>            candidate_ids;
  absl::flat_hash_map<hhds::Class_index, size_t>            value_ids;
  absl::flat_hash_map<Pin, size_t, Value_hash, Value_equal> values;

  bool exclusive(const Candidate& c) { return livehd::muxctx::exclusive(c.node); }

  std::map<std::pair<bool, std::vector<size_t>>, size_t> literal_ids;
  std::vector<Literal>                                   literals;
  std::vector<Pin>                                       literal_pins;
  std::vector<Path_node>                                 path_nodes;
  absl::flat_hash_map<std::pair<size_t, size_t>, size_t> path_ids;  // (parent, literal) -> path node
  // (run_top, v, k) -> the path nodes of that run whose literal is a uniform
  // negative over v naming k. Keyed by run: a lookup sees only the nodes that
  // share the run's first node above (siblings of one prefix), never every
  // `!(v == k)` of every region or outer arm.
  absl::flat_hash_map<std::tuple<size_t, uint64_t, int64_t>, std::vector<size_t>> negated_at;

  size_t literal_id(std::vector<Pin> arms, bool positive) {
    std::vector<size_t> key;
    for (const auto& p : arms) {
      key.push_back(p.get_class_index().value);
    }
    auto [it, fresh] = literal_ids.try_emplace({positive, std::move(key)}, literals.size());
    if (fresh) {
      Literal lit{std::move(arms), positive, {}, {}, false};
      if (positive) {
        if (lit.arms.size() == 1) {
          lit.equal = livehd::muxctx::equality(lit.arms.front());
        }
      } else {
        lit.unequal_all = true;
        for (const auto& arm : lit.arms) {
          if (const auto eq = livehd::muxctx::equality(arm)) {
            lit.unequal.push_back(*eq);
          } else {
            lit.unequal_all = false;
            break;
          }
        }
        if (lit.unequal_all && !lit.unequal.empty()) {
          lit.uniform = lit.unequal.front().value;
          for (const auto& eq : lit.unequal) {
            if (eq.value != lit.uniform) {
              lit.uniform = Pin{};
              break;
            }
          }
        }
      }
      literals.push_back(std::move(lit));
      literal_pins.emplace_back();
    }
    return it->second;
  }

  Pin literal_pin(size_t id) {
    if (literal_pins[id].is_invalid()) {
      const auto& lit  = literals[id];
      literal_pins[id] = lit.positive ? lit.arms.front() : negate(lit.arms.size() == 1 ? lit.arms.front() : disjunction(lit.arms));
    }
    return literal_pins[id];
  }

  // A positive `v == k` makes a negative literal redundant when every equality of its OR compares the same v with
  // another constant: v == k already excludes each of them.
  bool implies_negation(const Literal& positive, const Literal& negative) const {
    if (!positive.equal || !negative.unequal_all) {
      return false;
    }
    for (const auto& other : negative.unequal) {
      if (other.value != positive.equal->value || other.constant == positive.equal->constant) {
        return false;
      }
    }
    return true;
  }

  // Hash-consed: a path is its literals, so equal paths share one node and one
  // memoized conjunction. extend_path rebuilds the kept prefix every time an
  // equality drops earlier conjuncts; fresh nodes there cost a new And per kept
  // literal per arm (a decode chain under a shared prefix grew 7.9k cells into
  // 122k on XiangShan DecodeUnit).
  size_t append(size_t parent, size_t literal) {
    const auto [it, fresh] = path_ids.try_emplace({parent, literal}, path_nodes.size());
    if (fresh) {
      const size_t id = path_nodes.size();
      Path_node    node{literal, parent, {}};
      node.run_top = parent;
      if (parent != absent) {
        const auto& p = path_nodes[parent];
        node.depth    = p.depth + 1;
        const bool skew
            = p.jump != absent && path_nodes[p.jump].jump != absent
              && p.depth - path_nodes[p.jump].depth == path_nodes[p.jump].depth - path_nodes[path_nodes[p.jump].jump].depth;
        node.jump = skew ? path_nodes[p.jump].jump : parent;
      }
      if (const auto& lit = literals[literal]; !lit.uniform.is_invalid()) {
        if (parent != absent && literals[path_nodes[parent].literal].uniform == lit.uniform) {
          node.run_top = path_nodes[parent].run_top;
        }
        for (const auto& eq : lit.unequal) {
          negated_at[{node.run_top, lit.uniform.get_class_index().value, eq.constant}].push_back(id);
        }
      }
      path_nodes.push_back(node);
    }
    return it->second;
  }

  // `x` is `at` or one of its ancestors.
  bool on_path(size_t x, size_t at) const {
    const auto target = path_nodes[x].depth;
    if (at == absent || path_nodes[at].depth < target) {
      return false;
    }
    while (path_nodes[at].depth > target) {
      const auto j = path_nodes[at].jump;
      at           = j != absent && path_nodes[j].depth >= target ? j : path_nodes[at].parent;
    }
    return at == x;
  }

  // The path with `literal` appended, minus the earlier conjuncts it makes
  // redundant. One walk up the path, skipping each run of negatives over the
  // literal's own value as a whole: every one is implied unless it names the
  // same constant k. Whether the run holds such a `!(v == k)` is one lookup
  // in negated_at (keyed by the run) plus an O(log) ancestor test per hit;
  // a run that does (a contradiction) is walked node by node. Every other
  // conjunct is kept. A decode chain's arm (`v == k` under `!(v == 0) & ... &
  // !(v == k-1)`) costs O(log k), not O(k).
  size_t extend_path(size_t path, size_t literal) {
    const auto& lit = literals[literal];
    if (lit.equal) {
      const auto          v    = lit.equal->value;
      const auto          vkey = v.get_class_index().value;
      bool                redundant = false;
      std::vector<size_t> kept;
      for (size_t at = path; at != absent;) {
        const auto& node = path_nodes[at];
        const auto& cur  = literals[node.literal];
        if (!cur.uniform.is_invalid() && cur.uniform == v) {
          bool contra = false;
          if (const auto hit = negated_at.find({node.run_top, vkey, lit.equal->constant}); hit != negated_at.end()) {
            for (const auto x : hit->second) {
              if (on_path(x, at)) {
                contra = true;
                break;
              }
            }
          }
          if (!contra) {
            redundant = true;  // no `!(v == k)` in this run: all of it is implied
            at        = node.run_top;
            continue;
          }
          if (implies_negation(lit, cur)) {
            redundant = true;
          } else {
            kept.push_back(node.literal);
          }
        } else {
          kept.push_back(node.literal);  // implies_negation needs every equality over v
        }
        at = node.parent;
      }
      if (redundant) {
        size_t rebuilt = absent;
        for (size_t i = kept.size(); i-- > 0;) {
          rebuilt = append(rebuilt, kept[i]);
        }
        return append(rebuilt, literal);
      }
    }
    return append(path, literal);
  }

  Pin path_condition(size_t path) {
    if (path == absent) {
      return one;
    }
    std::vector<size_t> pending;
    for (size_t at = path; at != absent && path_nodes[at].condition.is_invalid(); at = path_nodes[at].parent) {
      pending.push_back(at);
    }
    for (size_t i = pending.size(); i-- > 0;) {
      auto&      node   = path_nodes[pending[i]];
      const auto before = node.parent == absent ? one : path_nodes[node.parent].condition;
      node.condition    = conjunction(before, literal_pin(node.literal));
    }
    return path_nodes[path].condition;
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
    // A fresh cell: operand k sits on slot k * stride. setup_sink_pid scans the
    // pins already there for the next free slot, O(m^2) on an m-arm OR (a
    // value group's conditions, a Hotmux none-of literal).
    const auto    stride = static_cast<hhds::Port_id>(Ntype::sink_bank_count(Ntype_op::Or));
    hhds::Port_id slot   = 0;
    for (auto p : pins) {
      n.create_sink_pin(slot).connect_driver(p);
      slot = static_cast<hhds::Port_id>(slot + stride);
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
      if (!consumer || gu::has_name(c.node) || !gu::pin_name_of(c.node.get_driver_pin(0)).empty()) {
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

  // Plan predicate trees without touching the graph. A predicate only needs
  // the right truth value, so mux(s,0,1) can use s even when s is wide/signed.
  // This is essential to mux(s,mux(c,a,b),mux(d,a,b)) ->
  // mux(mux(s,c,d),a,b) without introducing gratuitous boolean gates.
  void two_group_tree(Candidate& root, const std::vector<Visit>& visits, const std::vector<Group>& groups) {
    struct Ref {
      Pin    pin;
      size_t expression = absent;
      bool   operator==(const Ref& other) const { return expression == other.expression && pin == other.pin; }
    };
    struct Expression {
      Ntype_op         op;
      std::vector<Ref> inputs;
    };
    struct Plan {
      std::vector<Expression> expressions;
      std::vector<bool>       live;
      Ref                     result;
      size_t                  cost = 0;
      size_t                  true_group;
    };
    const Ref  z{zero}, o{one};
    const auto plan_for = [&](size_t true_group) {
      Plan       plan{{}, {}, {}, 0, true_group};
      const auto add = [&](Ntype_op op, std::vector<Ref> inputs) {
        const auto id = plan.expressions.size();
        plan.expressions.push_back({op, std::move(inputs)});
        return Ref{{}, id};
      };
      const auto mux = [&](Pin select, Ref f, Ref t) {
        if (f == t) {
          return f;
        }
        if (select.is_const()) {
          return gu::const_of(select).is_known_zero() ? f : t;
        }
        if (f == z && t == o) {
          return Ref{select};
        }
        if (f == o && t == z) {
          return add(Ntype_op::EQ, {{select}, z});
        }
        return add(Ntype_op::Mux, {{select}, f, t});
      };
      std::vector<Ref> predicates(visits.size());
      for (size_t i = visits.size(); i-- > 0;) {
        const auto& visit  = visits[i];
        const auto& c      = candidates[visit.candidate];
        const auto  target = [&](size_t j) {
          const auto t = visit.targets[j];
          return t.internal ? predicates[t.index] : t.index == true_group ? o : z;
        };
        if (c.arms.size() == 1) {
          predicates[i] = mux(c.arms[0].first, target(1), target(0));
          continue;
        }
        const auto       fallback = target(c.arms.size());
        bool             same     = true;
        std::vector<Ref> inputs;
        for (size_t j = 0; j < c.arms.size(); ++j) {
          auto value = target(j);
          same       = same && value == fallback;
          inputs.push_back({c.arms[j].first});
          inputs.push_back(value);
        }
        inputs.push_back(fallback);
        predicates[i] = same ? fallback : add(Ntype_op::Hotmux, std::move(inputs));
      }
      plan.result = predicates[0];
      plan.live.resize(plan.expressions.size());
      std::vector<Ref> pending{plan.result};
      for (size_t i = 0; i < pending.size(); ++i) {
        const auto id = pending[i].expression;
        if (id == absent || plan.live[id]) {
          continue;
        }
        plan.live[id] = true;
        ++plan.cost;
        for (auto input : plan.expressions[id].inputs) {
          pending.push_back(input);
        }
      }
      return plan;
    };
    auto plan     = plan_for(0);
    auto inverted = plan_for(1);
    if (inverted.cost < plan.cost) {
      plan = std::move(inverted);
    }
    // One final data mux, plus EVERY reachable predicate/control node. The
    // old cost counts cells, including each Hotmux once, not its arm count.
    if (plan.cost + 1 >= visits.size()) {
      return;
    }
    std::vector<Pin> emitted(plan.expressions.size());
    const auto       pin = [&](Ref ref) { return ref.expression == absent ? ref.pin : emitted[ref.expression]; };
    for (size_t i = 0; i < plan.expressions.size(); ++i) {
      if (!plan.live[i]) {
        continue;
      }
      const auto& expression = plan.expressions[i];
      auto        node       = livehd::cprop_value::make_node(graph, expression.op);
      for (size_t j = 0; j < expression.inputs.size(); ++j) {
        gu::setup_sink_pid(node, j).connect_driver(pin(expression.inputs[j]));
      }
      if (expression.op == Ntype_op::Hotmux) {
        // Same controls as the original, already proven/decoded exclusive.
        gu::set_proven(node, gu::kFormalOnehot);
      }
      emitted[i] = node.create_driver_pin(0);
      if (expression.op == Ntype_op::EQ) {
        gu::set_ubits(emitted[i], 1);
      }
      livehd::cprop_value::remember(node);
    }
    livehd::cprop_value::forget(root.node.get_driver_pin(0));
    for (auto sink : root.node.inp_pins_snapshot()) {
      sink.del_sink();
    }
    gu::clear_proven(root.node);
    gu::set_type_op(root.node, Ntype_op::Mux);
    gu::setup_sink_pid(root.node, 0).connect_driver(pin(plan.result));
    gu::setup_sink_pid(root.node, 1).connect_driver(groups[1 - plan.true_group].value);
    gu::setup_sink_pid(root.node, 2).connect_driver(groups[plan.true_group].value);
    livehd::cprop_value::remember(root.node);
    for (size_t i = 1; i < visits.size(); ++i) {
      auto node = candidates[visits[i].candidate].node;
      I(!node.has_out_edges());
      livehd::cprop_value::retire(node);
    }
  }

  void rewrite(size_t root_id) {
    auto& root = candidates[root_id];
    if (state_context && hold_flop(root).is_invalid()) {
      return;
    }
    std::vector<Visit> visits{
        {root_id, {}, absent, {}}
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
          visits.push_back({child->second, {}, absent, {}});
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
    if (!state_context && groups.size() == 2) {
      two_group_tree(root, visits, groups);
      return;
    }
    if (!hotmux_trees) {
      return;  // a Hotmux of path conjunctions trades data selects for predicate gates: a loss for a simulator
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

    const livehd::muxctx::Decode condition = [&](const Pin& p) {
      return livehd::muxctx::compute_condition(
          p,
          [](const Pin& q) -> std::optional<livehd::muxctx::Bool_condition> { return livehd::muxctx::Bool_condition{q, true}; },
          boolean_fact);
    };
    for (size_t i = 0; i < visits.size(); ++i) {
      auto&            visit = visits[i];
      const auto&      c     = candidates[visit.candidate];
      std::vector<Pin> controls;
      for (const auto& [control, value] : c.arms) {
        (void)value;
        const auto known = visit.facts.truth(control, condition);
        controls.push_back(known ? (*known ? one : zero) : boolean(control));
      }
      for (size_t j = 0; j <= controls.size(); ++j) {
        auto             facts = visit.facts;
        // The conjunct this branch adds to the path: nothing when it always holds, `dead` when it never can.
        std::vector<Pin> arms;
        bool             dead = false;
        if (j < c.arms.size()) {
          facts.assume(c.arms[j].first, true, condition);
          dead = controls[j] == zero;
          if (controls[j] != one) {
            arms.push_back(controls[j]);
          }
        } else {
          for (const auto& arm : c.arms) {
            facts.assume(arm.first, false, condition);
          }
          for (const auto& control : controls) {
            dead = dead || control == one;
            if (control != zero) {
              arms.push_back(control);
            }
          }
        }
        const bool reachable = facts.reachable() && !dead;
        size_t     path      = visit.path;
        if (reachable && !arms.empty()) {
          path = extend_path(path, literal_id(std::move(arms), j < c.arms.size()));
        }
        const auto target = visit.targets[j];
        if (target.internal) {
          visits[target.index].path  = path;
          visits[target.index].facts = facts;
        } else {
          groups[target.index].conditions.push_back(reachable ? path_condition(path) : zero);
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
    // The old pins stay (only their edges go) and are reused by port below: a
    // per-port create_sink_pin finds an existing pin by rescanning the pin list
    // from its head, O(arms^2) when a wide Hotmux is rebuilt in place.
    std::vector<Pin> old_sinks;
    for (const auto& spin : root.node.inp_pins_snapshot()) {
      spin.del_sink();
      const auto pid = spin.get_port_id();
      if (pid >= old_sinks.size()) {
        old_sinks.resize(pid + 1);
      }
      old_sinks[pid] = spin;
    }
    const auto sink_at = [&](hhds::Port_id pid) {
      return pid < old_sinks.size() && !old_sinks[pid].is_invalid() ? old_sinks[pid] : root.node.create_sink_pin(pid);
    };
    gu::clear_proven(root.node);
    if (data.size() == 1) {
      gu::set_type_op(root.node, Ntype_op::Or);
      livehd::graph_util::setup_sink_pid(root.node, 0).connect_driver(data.front());
    } else {
      gu::set_type_op(root.node, Ntype_op::Hotmux);
      for (size_t i = 0; i + 1 < data.size(); ++i) {
        sink_at(static_cast<hhds::Port_id>(2 * i)).connect_driver(controls[i]);
        sink_at(static_cast<hhds::Port_id>(2 * i + 1)).connect_driver(data[i]);
      }
      sink_at(static_cast<hhds::Port_id>(2 * (data.size() - 1))).connect_driver(data.back());
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
  explicit Mux_sharing(hhds::Graph& g, bool state, const std::function<bool(const Pin&)>& proof, bool hotmux)
      : graph(g), state_context(state), hotmux_trees(hotmux), boolean_fact(proof ? proof : livehd::cprop_value::is_bool01) {
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
  livehd::share_mux_regions(*current_graph, false, {}, hotmux_trees_);
}

void livehd::share_mux_regions(hhds::Graph& graph, bool state_context, const std::function<bool(const Pin&)>& boolean_fact,
                               bool hotmux_trees) {
  Mux_sharing(graph, state_context, boolean_fact, hotmux_trees).run();
}
