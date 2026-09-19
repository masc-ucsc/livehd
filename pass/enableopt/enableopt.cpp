// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "enableopt.hpp"

#include <functional>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cprop.hpp"
#include "cprop_profile.hpp"
#include "node_util.hpp"

namespace {
using namespace livehd::graph_util;
using Inp_pins = Cprop::Inp_pins;
bool is_two_arm_mux(const Inp_pins& e) {
  return e.size() == 3 && e[0].get_port_id() == 0 && e[1].get_port_id() == 1 && e[2].get_port_id() == 2;
}
bool is_two_arm_mux(const hhds::Node_class& node) {
  // A shared indexed mux may feed many states. Reject it after at most four
  // inputs instead of copying every arm again for each destination.
  unsigned expected = 0;
  for (auto input : node.inp_sorted_pins()) {
    if (expected == 3 || input.get_port_id() != expected++) {
      return false;
    }
  }
  return expected == 3;
}
hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid) {
  auto sink = n.get_sink_pin(static_cast<hhds::Port_id>(pid));
  return sink.is_invalid() ? hhds::Pin_class{} : sink.get_driver_pin();
}
template <typename T>
bool has_single_consumer(const T& p) {
  auto edges = p.out_edges();
  auto it    = edges.begin();
  if (it == edges.end()) {
    return false;
  }
  return ++it == edges.end();
}
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

class State_optimizer {
  struct Clause {
    absl::flat_hash_map<hhds::Class_index, bool> literals;
    decltype(hhds::Class_index{}.value)          unresolved_xor = 0;
    bool                                         usable         = true;
  };
  absl::flat_hash_map<hhds::Class_index, Clause> clauses;

  absl::flat_hash_map<hhds::Class_index, Bool_condition>      conditions;
  absl::flat_hash_set<hhds::Class_index>                      booleans;
  absl::flat_hash_set<hhds::Class_index>                      state_free;
  absl::flat_hash_map<hhds::Class_index, hhds::Pin_class>     hold_sources;
  absl::flat_hash_map<hhds::Class_index, std::optional<bool>> dependencies;

  bool is_bool01(const hhds::Pin_class& p) const {
    if (p.is_invalid()) {
      return false;
    }
    if (p.is_const()) {
      const auto& c = const_of(p);
      return !c.has_unknowns() && c.is_just_i64() && (c.to_just_i64() == 0 || c.to_just_i64() == 1);
    }
    return booleans.contains(p.get_class_index());
  }
  bool independent_of_state(const hhds::Pin_class& p) const {
    return !p.is_invalid() && (p.is_const() || is_graph_input_pin(p) || state_free.contains(p.get_class_index()));
  }
  std::optional<Bool_condition> decode_bool_condition(const hhds::Pin_class& p) const {
    if (p.is_invalid()) {
      return std::nullopt;
    }
    if (auto it = conditions.find(p.get_class_index()); it != conditions.end()) {
      return it->second;
    }
    return Bool_condition{p, true};
  }
  // Reduce the boolean materializations emitted by tolg and by a slang round
  // trip: `s ? 1 : 0`, its inverse, and `x == 0` chains. This is deliberately a
  // structural decoder, not a general boolean-equivalence proof.
  [[nodiscard]] std::optional<Bool_condition> compute_condition(const hhds::Pin_class& p) {
    if (p.is_invalid()) {
      return std::nullopt;
    }
    if (p.is_const() || is_graph_input_pin(p)) {
      return Bool_condition{p, true};
    }
    auto n = p.get_master_node();
    if (type_op_of(n) == Ntype_op::Mux && is_two_arm_mux(n)) {
      auto sel  = drv_at(n, 0);
      auto arm0 = drv_at(n, 1);
      auto arm1 = drv_at(n, 2);
      auto v0   = const_truth(arm0);
      auto v1   = const_truth(arm1);
      if (!sel.is_invalid() && v0.has_value() && v1.has_value() && *v0 != *v1) {
        auto result = decode_bool_condition(sel);
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
        auto result = decode_bool_condition(value);
        if (result.has_value()) {
          result->true_when_base = !result->true_when_base;
        }
        return result;
      }
    }
    return Bool_condition{p, true};
  }

  struct Hold_mux_match {
    hhds::Node_class mux;
    hhds::Pin_class  data;
  };

  [[nodiscard]] bool is_latch_hold_value(const hhds::Pin_class& p, const hhds::Pin_class& q) const {
    if (same_pin(p, q)) {
      return true;
    }
    auto it = hold_sources.find(p.get_class_index());
    return it != hold_sources.end() && same_pin(it->second, q);
  }

  // Find an enable-qualified `Q` hold mux anywhere in the latch's D cone. Slang
  // can wrap a generated hold mux in Get_mask/Sext nodes and then add another
  // hold mux while re-reading `always_latch`, so looking only at din's immediate
  // driver misses the nested form. A candidate must have a single consumer: its
  // disabled value is irrelevant only on this latch-D path, not to arbitrary
  // other observers of a shared mux.
  [[nodiscard]] std::vector<Hold_mux_match> find_latch_hold_muxes(const hhds::Pin_class& start, const hhds::Pin_class& q,
                                                                  const Bool_condition& open) {
    std::vector<Hold_mux_match>            matches;
    absl::flat_hash_set<hhds::Class_index> seen;
    std::vector<hhds::Pin_class>           work{start};
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || p.is_const() || is_graph_input_pin(p) || !seen.insert(p.get_class_index()).second) {
        continue;
      }
      auto n = p.get_master_node();
      if (livehd::graph_util::is_type_register(n) || type_op_of(n) == Ntype_op::Sub || !has_single_consumer(p)) {
        continue;
      }
      if (type_op_of(n) == Ntype_op::Mux) {
        auto       sel  = drv_at(n, 0);
        auto       arm0 = drv_at(n, 1);
        auto       arm1 = drv_at(n, 2);
        const bool q0   = is_latch_hold_value(arm0, q);
        const bool q1   = is_latch_hold_value(arm1, q);
        if (q0 != q1) {
          auto selected = decode_bool_condition(sel);
          if (selected.has_value()) {
            if (!q0) {  // q on arm1 => data selected while selector is false
              selected->true_when_base = !selected->true_when_base;
            }
            if (has_single_consumer(n) && same_pin(selected->base, open.base) && selected->true_when_base == open.true_when_base) {
              matches.push_back({n, q0 ? arm1 : arm0});
            }
          }
        }
      }
      // Arbitrary node: a compact loop's carry-in sink legitimately holds the
      // seed AND the previous-ordinal self edge, so take the plural reader.
      for (auto isnk : n.inp_sorted_pins()) {
        for (auto idrv : isnk.get_driver_pins()) {
          work.push_back(idrv);
        }
      }
    }
    return matches;
  }

  // Dependency results belong to this state's private D region. Shared
  // combinational boundaries are unknown unless the forward facts already
  // prove that they depend on no state. Each owned pin is evaluated once.
  [[nodiscard]] std::optional<bool> cone_reaches_q(const hhds::Pin_class& start, const hhds::Pin_class& q) {
    struct Visit {
      hhds::Pin_class pin;
      bool            finish;
    };
    std::vector<Visit> work{
        {start, false}
    };
    while (!work.empty()) {
      auto [pin, finish] = work.back();
      work.pop_back();
      if (finish) {
        bool reaches = false;
        bool unknown = false;
        for (auto sink : pin.get_master_node().inp_sorted_pins()) {
          for (auto input : sink.get_driver_pins()) {
            const auto it = dependencies.find(input.get_class_index());
            if (it == dependencies.end() || !it->second.has_value()) {
              unknown = true;
            } else {
              reaches |= *it->second;
            }
          }
        }
        dependencies[pin.get_class_index()] = reaches   ? std::optional<bool>{true}
                                              : unknown ? std::nullopt
                                                        : std::optional<bool>{false};
        continue;
      }
      if (dependencies.contains(pin.get_class_index())) {
        continue;
      }
      auto [it, fresh] = dependencies.emplace(pin.get_class_index(), std::nullopt);
      (void)fresh;
      if (same_pin(pin, q)) {
        it->second = true;
        continue;
      }
      if (independent_of_state(pin)) {
        it->second = false;
        continue;
      }
      if (pin.is_invalid()) {
        continue;
      }
      auto node = pin.get_master_node();
      if (is_type_register(node)) {
        it->second = false;
        continue;
      }
      if (type_op_of(node) == Ntype_op::Sub || !has_single_consumer(pin)) {
        continue;
      }
      work.push_back({pin, true});
      for (auto sink : node.inp_sorted_pins()) {
        for (auto input : sink.get_driver_pins()) {
          work.push_back({input, false});
        }
      }
    }
    return dependencies.at(start.get_class_index());
  }

  // Pair `gate ? 1 : data_enable` with a din override on the same gate. The
  // override branch must not reach Q, while the normal branch must contain the
  // hold path. This proves that the extra aggregate-enable cause is reset-like
  // priority logic and does not qualify normal latch data.
  [[nodiscard]] bool has_guarded_data_override(const hhds::Pin_class& din, const hhds::Pin_class& q, const Bool_condition& gate,
                                               bool override_when_base) {
    absl::flat_hash_set<hhds::Class_index> seen;
    std::vector<hhds::Pin_class>           work{din};
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || p.is_const() || is_graph_input_pin(p) || !seen.insert(p.get_class_index()).second) {
        continue;
      }
      auto n = p.get_master_node();
      if (livehd::graph_util::is_type_register(n) || type_op_of(n) == Ntype_op::Sub || !has_single_consumer(p)) {
        continue;
      }
      if (type_op_of(n) == Ntype_op::Mux) {
        auto selected = decode_bool_condition(drv_at(n, 0));
        auto arm0     = drv_at(n, 1);
        auto arm1     = drv_at(n, 2);
        if (selected.has_value() && same_pin(selected->base, gate.base)) {
          const bool override_on_sel1 = selected->true_when_base == override_when_base;
          auto       override_arm     = override_on_sel1 ? arm1 : arm0;
          auto       normal_arm       = override_on_sel1 ? arm0 : arm1;
          if (cone_reaches_q(override_arm, q) == false && cone_reaches_q(normal_arm, q) == true) {
            return true;
          }
        }
      }
      // Arbitrary node: a compact loop's carry-in sink legitimately holds the
      // seed AND the previous-ordinal self edge, so take the plural reader.
      for (auto isnk : n.inp_sorted_pins()) {
        for (auto idrv : isnk.get_driver_pins()) {
          work.push_back(idrv);
        }
      }
    }
    return false;
  }

  // Slang represents an async-reset latch's aggregate write-enable as
  // `reset ? 1 : data_enable`. Recover the normal data enable only when din has
  // the matching priority override. Reset value and priority remain untouched.
  [[nodiscard]] std::optional<Bool_condition> latch_data_open_condition(const hhds::Node_class& latch, const hhds::Pin_class& q,
                                                                        const hhds::Pin_class& din, const hhds::Pin_class& enable) {
    auto open = decode_bool_condition(enable);
    if (!open.has_value() || !open->true_when_base || open->base.is_invalid() || open->base.is_const()
        || is_graph_input_pin(open->base)) {
      return open;
    }
    auto mux = open->base.get_master_node();
    if (type_op_of(mux) != Ntype_op::Mux) {
      return open;
    }
    auto sel  = drv_at(mux, 0);
    auto arm0 = drv_at(mux, 1);
    auto arm1 = drv_at(mux, 2);
    auto gate = decode_bool_condition(sel);
    auto v0   = const_truth(arm0);
    auto v1   = const_truth(arm1);
    if (!gate.has_value()) {
      return open;
    }
    const bool arm0_override = v0.has_value() && *v0;
    const bool arm1_override = v1.has_value() && *v1;
    if (arm0_override == arm1_override) {
      return open;
    }
    const bool override_on_sel1   = arm1_override;
    const bool override_when_base = override_on_sel1 ? gate->true_when_base : !gate->true_when_base;
    if (!has_guarded_data_override(din, q, *gate, override_when_base)) {
      return open;
    }

    // If a dedicated reset pin exists, require it to agree too. Older slang
    // latch lowering lacks that pin; the paired data override above is then the
    // sound structural evidence.
    auto reset = livehd::graph_util::get_driver_of_sink_name(latch, "reset_pin");
    if (!reset.is_invalid()) {
      auto reset_active = decode_bool_condition(reset);
      if (!reset_active.has_value()) {
        return open;
      }
      auto negreset = livehd::graph_util::get_driver_of_sink_name(latch, "negreset");
      if (!negreset.is_invalid()) {
        auto negative = const_truth(negreset);
        if (!negative.has_value()) {
          return open;
        }
        if (*negative) {
          reset_active->true_when_base = !reset_active->true_when_base;
        }
      }
      if (!same_pin(gate->base, reset_active->base) || override_when_base != reset_active->true_when_base) {
        return open;
      }
    }
    return decode_bool_condition(override_on_sel1 ? arm0 : arm1);
  }

public:
  bool boolean(const hhds::Pin_class& pin) const { return is_bool01(pin); }
  explicit State_optimizer(hhds::Graph& graph, bool has_latches) {
    livehd::cprop_profile::Timer timer(livehd::cprop_profile::order);
    for (auto node : graph.body().nodes(hhds::Node_order::forward)) {
      const auto op       = type_op_of(node);
      bool       no_state = has_latches && !is_type_register(node) && op != Ntype_op::Sub && op != Ntype_op::Memory;
      bool       all_bool = true, any_bool = false, has_data = false;
      const auto control_end = op == Ntype_op::Hotmux ? hotmux_control_end(node) : 0;
      const bool boolean_operator
          = op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mux || op == Ntype_op::Hotmux;
      if (has_latches || boolean_operator) {
        for (auto sink : node.inp_sorted_pins()) {
          if (no_state) {
            for (auto input : sink.get_driver_pins()) {
              no_state &= independent_of_state(input);
            }
          }
          if (!boolean_operator) {
            continue;
          }
          if ((op == Ntype_op::Mux && sink.get_port_id() == 0)
              || (op == Ntype_op::Hotmux && is_hotmux_control(sink.get_port_id(), control_end))) {
            continue;
          }
          for (auto input : sink.get_driver_pins()) {
            const bool boolean  = is_bool01(input);
            all_bool           &= boolean;
            any_bool           |= boolean;
            has_data            = true;
          }
        }
      }
      bool boolean = op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::Ror || op == Ntype_op::Rxor
                     || (op == Ntype_op::And && any_bool)
                     || ((op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mux || op == Ntype_op::Hotmux) && has_data
                         && all_bool);
      if (op == Ntype_op::Get_mask) {
        auto mask = get_driver_of_sink_name(node, "mask");
        boolean
            = mask.is_const() && !const_of(mask).is_negative() && !const_of(mask).has_unknowns() && const_of(mask).popcount() <= 1;
      }
      for (auto output : node.out_sorted_pins()) {
        if (boolean) {
          booleans.insert(output.get_class_index());
        }
        if (no_state) {
          state_free.insert(output.get_class_index());
        }
        if (auto condition = compute_condition(output); condition && (condition->base != output || !condition->true_when_base)) {
          conditions.emplace(output.get_class_index(), *condition);
        }
        if (has_latches && (op == Ntype_op::Get_mask || op == Ntype_op::Sext)) {
          auto source = drv_at(node, 0);
          auto it     = hold_sources.find(source.get_class_index());
          hold_sources.emplace(output.get_class_index(), it == hold_sources.end() ? source : it->second);
        }
      }
    }
  }
  void canonicalize_latch_hold(const hhds::Node_class& latch);
  void canonicalize_flop_hold(const hhds::Node_class& flop);
  void canonicalize_flop_enable(const hhds::Node_class& flop);
  void optimize(const hhds::Node_class& node) {
    dependencies.clear();
    canonicalize_latch_hold(node);
    canonicalize_flop_hold(node);
    canonicalize_flop_enable(node);
  }
};
}  // namespace

// A flop samples din only while enabled. Specialize its immediate mux to
// that condition, reconnecting only this sink so shared observers are intact.
void State_optimizer::canonicalize_flop_hold(const hhds::Node_class& flop) {
  if (flop.is_invalid() || type_op_of(flop) != Ntype_op::Flop) {
    return;
  }
  auto enable = livehd::graph_util::get_driver_of_sink_name(flop, "enable");
  auto open   = decode_bool_condition(enable);
  if (!open.has_value()) {
    return;
  }
  for (;;) {
    auto din = livehd::graph_util::get_driver_of_sink_name(flop, "din");
    if (din.is_invalid() || din.is_const() || is_graph_input_pin(din)) {
      break;
    }
    auto mux = din.get_master_node();
    if (type_op_of(mux) != Ntype_op::Mux) {
      break;
    }
    // TWO-ARM only: with 3+ arms the selector is an INDEX, not a condition, so
    // "sel is truthy exactly when the flop samples" says nothing about WHICH
    // arm is live -- rewiring din to arm 1 would drop arms 2..N.
    if (!is_two_arm_mux(mux)) {
      break;
    }
    auto selected = decode_bool_condition(drv_at(mux, 0));
    if (!selected.has_value() || !same_pin(selected->base, open->base)) {
      break;
    }
    auto data = drv_at(mux, selected->true_when_base == open->true_when_base ? 2 : 1);
    if (data.is_invalid()) {
      break;
    }
    auto sink = livehd::graph_util::find_sink_pin(flop, "din");
    sink.del_sink();
    data.connect_sink(sink);
    if (!mux.has_out_edges()) {
      mux.del_node();
    } else {
      break;  // only the immediate shared mux may be specialized for this sink
    }
  }
}

// Specialize only private data regions. The shared enable clause is decoded
// once per pin; each destination owns a sparse overlay of facts with rollback.
// Neither entering a branch nor inferring the last enable literal copies a
// path-sized fact vector or scans every literal in the enable.
void State_optimizer::canonicalize_flop_enable(const hhds::Node_class& flop) {
  if (flop.is_invalid() || type_op_of(flop) != Ntype_op::Flop) {
    return;
  }
  const auto enable = get_driver_of_sink_name(flop, "enable");
  if (enable.is_invalid() || enable.is_const()) {
    return;
  }
  auto [clause_it, fresh] = clauses.try_emplace(enable.get_class_index());
  auto& clause            = clause_it->second;
  if (fresh) {
    auto add = [&](const hhds::Pin_class& pin) {
      auto cond = decode_bool_condition(pin);
      if (!cond) {
        clause.usable = false;
        return;
      }
      auto [it, inserted] = clause.literals.emplace(cond->base.get_class_index(), cond->true_when_base);
      if (inserted) {
        clause.unresolved_xor ^= cond->base.get_class_index().value;
      } else if (it->second != cond->true_when_base) {
        clause.usable = false;
      }
    };
    if (!is_graph_input_pin(enable) && type_op_of(enable.get_master_node()) == Ntype_op::Or) {
      for (auto sink : enable.get_master_node().inp_sorted_pins()) {
        const auto pin = sink.get_driver_pin();
        if (pin.is_const() || !is_bool01(pin)) {
          clause.usable = false;
          break;
        }
        add(pin);
      }
    } else {
      add(enable);
    }
  }
  if (!clause.usable || clause.literals.empty()) {
    return;
  }
  using Index = hhds::Class_index;
  absl::flat_hash_map<Index, bool>                   facts;
  std::vector<std::pair<Index, std::optional<bool>>> undo;
  size_t                                             unresolved = clause.literals.size(), satisfied = 0;
  auto                                               unresolved_xor = clause.unresolved_xor;
  const auto                                         truth          = [&](Index id) -> std::optional<bool> {
    auto it = facts.find(id);
    return it == facts.end() ? std::nullopt : std::optional<bool>{it->second};
  };
  const auto set = [&](Index id, std::optional<bool> value) {
    const auto old = truth(id);
    if (auto literal = clause.literals.find(id); literal != clause.literals.end()) {
      if (!old) {
        --unresolved;
        unresolved_xor ^= id.value;
      } else if (*old == literal->second) {
        --satisfied;
      }
      if (!value) {
        ++unresolved;
        unresolved_xor ^= id.value;
      } else if (*value == literal->second) {
        ++satisfied;
      }
    }
    if (value) {
      facts.insert_or_assign(id, *value);
    } else {
      facts.erase(id);
    }
  };
  const auto assign = [&](Index id, bool value) {
    auto old = truth(id);
    if (old) {
      return *old == value;
    }
    undo.emplace_back(id, old);
    set(id, value);
    return true;
  };
  const auto erase = [&](Index id) {
    auto old = truth(id);
    undo.emplace_back(id, old);
    set(id, std::nullopt);
  };
  const auto restore = [&](size_t mark) {
    while (undo.size() > mark) {
      const auto [id, old] = undo.back();
      undo.pop_back();
      set(id, old);
    }
  };
  const auto rewire = [](const hhds::Pin_class& sink, const hhds::Pin_class& driver) {
    if (sink.get_driver_pin() != driver) {
      sink.del_sink();
      driver.connect_sink(sink);
    }
  };
  // Explicit continuations keep arbitrarily deep private update trees off the
  // C++ stack. At most one visit and one rollback are queued per owned edge.
  std::vector<std::function<void()>>                    work;
  absl::flat_hash_set<Index>                            visited;
  std::function<void(hhds::Pin_class, hhds::Pin_class)> visit;
  visit = [&](hhds::Pin_class pin, hhds::Pin_class sink) {
    if (pin.is_invalid() || pin.is_const() || is_graph_input_pin(pin)) {
      return;
    }
    auto       node = pin.get_master_node();
    const auto op   = type_op_of(node);
    if (op != Ntype_op::Mux && op != Ntype_op::Or && op != Ntype_op::Set_mask && op != Ntype_op::Concat) {
      return;
    }
    const bool owned = has_single_consumer(pin);
    if (owned && !visited.insert(pin.get_class_index()).second) {
      return;
    }
    const size_t mark = undo.size();
    work.push_back([&, mark] { restore(mark); });
    if (satisfied == 0 && unresolved == 1) {
      const Index id{unresolved_xor};
      assign(id, clause.literals.at(id));
    }
    if (op == Ntype_op::Mux) {
      // Stop after four inputs even on a shared indexed mux.
      Inp_pins edges;
      for (auto input : node.inp_sorted_pins()) {
        edges.push_back(input);
        if (edges.size() > 3) {
          return;
        }
      }
      if (!is_two_arm_mux(edges)) {
        return;
      }
      const auto selected = decode_bool_condition(edges[0].get_driver_pin());
      if (selected) {
        if (auto known = truth(selected->base.get_class_index()); known) {
          const auto data = edges[*known == selected->true_when_base ? 2 : 1].get_driver_pin();
          rewire(sink, data);
          if (owned) {
            node.del_node();
            work.push_back([&, data, sink] { visit(data, sink); });
          }
          return;
        }
      }
      if (!owned) {
        return;
      }
      for (size_t arm = 1; arm <= 2; ++arm) {
        const auto edge = edges[arm];
        work.push_back([&, edge, selected, arm] {
          const size_t branch = undo.size();
          work.push_back([&, branch] { restore(branch); });
          if (selected
              && !assign(selected->base.get_class_index(), arm == 2 ? selected->true_when_base : !selected->true_when_base)) {
            return;
          }
          const auto driver = edge.get_driver_pin();
          work.push_back([&, driver, edge] { visit(driver, edge); });
        });
      }
      return;
    }
    if (!owned) {
      return;
    }
    if (op == Ntype_op::Or) {
      if (!is_bool01(pin)) {
        return;
      }
      struct Operand {
        hhds::Pin_class               edge;
        std::optional<Bool_condition> condition;
      };
      std::vector<Operand>               operands;
      absl::flat_hash_map<Index, size_t> multiplicity;
      absl::flat_hash_set<Index>         introduced;
      for (auto edge : node.inp_sorted_pins()) {
        const auto driver    = edge.get_driver_pin();
        auto       condition = driver.is_const() ? std::nullopt : decode_bool_condition(driver);
        operands.push_back({edge, condition});
        if (condition) {
          const auto id = condition->base.get_class_index();
          ++multiplicity[id];
          if (!truth(id)) {
            introduced.insert(id);
          }
          // A satisfied operand makes this Boolean OR independent of all its
          // siblings in this context; the parent is its sole observer.
          if (!assign(id, !condition->true_when_base)) {
            rewire(sink, create_const(*flop.get_graph(), *Dlop::create_integer(1)));
            return;
          }
        }
      }
      for (const auto& operand : operands) {
        const bool release = operand.condition && introduced.contains(operand.condition->base.get_class_index())
                             && multiplicity.at(operand.condition->base.get_class_index()) == 1;
        work.push_back([&, operand, release] {
          const size_t branch = undo.size();
          work.push_back([&, branch] { restore(branch); });
          if (operand.condition) {
            const auto id = operand.condition->base.get_class_index();
            if (release) {
              erase(id);
            }
          }
          const auto driver = operand.edge.get_driver_pin();
          work.push_back([&, driver, operand] { visit(driver, operand.edge); });
        });
      }
      return;
    }
    // The lane contracts identify data pins without searching writer chains.
    for (auto edge : node.inp_sorted_pins()) {
      const auto pid = edge.get_port_id();
      if ((op == Ntype_op::Set_mask && pid != 0 && pid != 4) || (op == Ntype_op::Concat && pid % 2 != 0)) {
        continue;
      }
      const auto driver = edge.get_driver_pin();
      work.push_back([&, driver, edge] { visit(driver, edge); });
    }
  };
  const auto sink = find_sink_pin(flop, "din");
  if (sink.is_invalid()) {
    return;
  }
  const auto driver = sink.get_driver_pin();
  work.push_back([&, driver, sink] { visit(driver, sink); });
  while (!work.empty()) {
    auto task = std::move(work.back());
    work.pop_back();
    task();
  }
}

void State_optimizer::canonicalize_latch_hold(const hhds::Node_class& latch) {
  if (latch.is_invalid() || type_op_of(latch) != Ntype_op::Latch) {
    return;
  }
  auto q      = latch.get_driver_pin(0);
  auto din    = livehd::graph_util::get_driver_of_sink_name(latch, "din");
  auto enable = livehd::graph_util::get_driver_of_sink_name(latch, "enable");

  // An always-active latch has no opaque/holding phase and therefore no
  // state: Q follows D combinationally. Pyrope now wires explicit enable=1
  // for this case; keep accepting a missing enable as the historical/default
  // spelling so old LGs and slang's fully-assigned nonblocking blocks also
  // canonicalize. Runtime reset is deliberately excluded because replacing
  // it requires preserving the reset-priority value mux.
  bool always_open = enable.is_invalid();
  if (!enable.is_invalid()) {
    auto en_value = const_truth(enable);
    if (en_value.has_value()) {
      bool active_high = true;
      auto posclk      = livehd::graph_util::get_driver_of_sink_name(latch, "posclk");
      if (!posclk.is_invalid()) {
        auto positive = const_truth(posclk);
        if (!positive.has_value()) {
          return;
        }
        active_high = *positive;
      }
      always_open = *en_value == active_high;
    }
  }
  auto reset = livehd::graph_util::get_driver_of_sink_name(latch, "reset_pin");
  if (always_open && reset.is_invalid() && !q.is_invalid() && !din.is_invalid() && cone_reaches_q(din, q) == false) {
    // SNAPSHOT: out_edges() is a lazy VIEW over live edge storage (hhds
    // graph.hpp) and connect_sink() calls add_edge, which can rehome an entry
    // into the overflow set -- and growing overflow_sets() reallocates the
    // vector the in-flight iterator is borrowing from. Walking and connecting
    // in one loop read freed storage; hhds's debug mutation guard now aborts
    // on it.
    const auto                   q_view = q.out_edges();
    livehd::graph_util::Edge_vec q_outs(q_view.begin(), q_view.end());
    for (const auto& out : q_outs) {
      din.connect_sink(out.sink);
    }
    latch.del_node();
    return;
  }

  auto open = latch_data_open_condition(latch, q, din, enable);
  if (q.is_invalid() || din.is_invalid() || enable.is_invalid() || !open.has_value()) {
    return;
  }
  auto posclk = livehd::graph_util::get_driver_of_sink_name(latch, "posclk");
  if (!posclk.is_invalid()) {
    auto positive = const_truth(posclk);
    if (!positive.has_value()) {
      return;  // dynamic/unknown polarity is not safe to simplify
    }
    if (!*positive) {
      open->true_when_base = !open->true_when_base;
    }
  }

  // Discover the private region once, then consume every matched hold mux.
  // Removing one match must not restart a search over surviving siblings.
  for (auto hit : find_latch_hold_muxes(din, q, *open)) {
    const auto                   mux_view = hit.mux.out_edges();
    livehd::graph_util::Edge_vec mux_outs(mux_view.begin(), mux_view.end());
    for (const auto& out : mux_outs) {
      hit.data.connect_sink(out.sink);
    }
    hit.mux.del_node();
  }

  // Collapse the usual boolean materialization on enable itself. Keep an
  // inverted materialization intact; changing it requires moving inversion
  // into posclk and is outside this deliberately narrow rewrite.
  auto raw_enable = decode_bool_condition(enable);
  if (raw_enable.has_value() && raw_enable->true_when_base && !same_pin(raw_enable->base, enable)) {
    auto enable_sink = livehd::graph_util::find_sink_pin(latch, "enable");
    enable_sink.del_sink();
    raw_enable->base.connect_sink(enable_sink);
  }
}

void Enableopt::do_trans(const std::shared_ptr<hhds::Graph>& graph) {
  if (!graph) {
    return;
  }
  // State outputs are forward sources. Collect the endpoints before rewrites
  // so deleting an always-open latch cannot invalidate a live node iterator.
  std::vector<hhds::Node_class> states;
  for (auto node : graph->body().nodes()) {
    if (is_type_register(node)) {
      states.push_back(node);
    }
  }
  if (states.empty()) {
    return;
  }
  const auto                        profile_name = std::string{"enableopt:"} + std::string{graph->get_io()->get_name()};
  livehd::cprop_profile::Invocation profile(profile_name);
  const bool      has_latches = std::ranges::any_of(states, [](auto node) { return type_op_of(node) == Ntype_op::Latch; });
  State_optimizer optimizer(*graph, has_latches);
  for (auto node : states) {
    optimizer.optimize(node);
  }
  livehd::cprop_profile::Timer sharing(livehd::cprop_profile::sharing);
  livehd::share_mux_regions(*graph, true, [&](const hhds::Pin_class& pin) { return optimizer.boolean(pin); });
}
