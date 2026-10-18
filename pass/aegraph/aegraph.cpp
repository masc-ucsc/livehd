//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "aegraph.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "cprop_muxctx.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"

namespace livehd::aegraph {

namespace gu = livehd::graph_util;

// ---------------------------------------------------------------------------
// Egraph
// ---------------------------------------------------------------------------

namespace {
// splitmix64 finalizer: a deterministic, per-process-stable mixer (absl seeds
// its own hashing per process, and no decision here may depend on that).
constexpr uint64_t mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  h *= 0xbf58476d1ce4e5b9ULL;
  h ^= h >> 31;
  return h;
}
}  // namespace

uint64_t Egraph::hash_of(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands) {
  uint64_t h = mix(0x1234567ULL, static_cast<uint64_t>(op));
  h          = mix(h, (attrs.has_color ? 1U : 0U) | (attrs.has_proven ? 2U : 0U));
  h          = mix(h, static_cast<uint32_t>(attrs.color));
  h          = mix(h, attrs.proven);
  h          = mix(h, attrs.policy);
  for (const auto& operand : operands) {
    h = mix(h, (static_cast<uint64_t>(operand.bank) << 32) | operand.cls);
  }
  return h;
}

Class_id Egraph::add_leaf() {
  classes_.push_back(Eclass{Kind::leaf, kNoEnode, std::nullopt});
  return static_cast<Class_id>(classes_.size() - 1);
}

Class_id Egraph::add_literal(const Dlop& value) {
  classes_.push_back(Eclass{Kind::literal, kNoEnode, value});
  return static_cast<Class_id>(classes_.size() - 1);
}

Class_id Egraph::add_const(const Dlop& value) {
  I(!value.has_unknowns());
  auto& bucket = consts_[value.hash()];
  for (const auto id : bucket) {
    if (classes_[id].value->same_repr(value)) {
      return id;
    }
  }
  classes_.push_back(Eclass{Kind::konst, kNoEnode, value});
  bucket.push_back(static_cast<Class_id>(classes_.size() - 1));
  return bucket.back();
}

uint32_t Egraph::policy_id(std::string_view policy) {
  if (policy.empty()) {
    return 0;
  }
  const auto [it, inserted] = policy_ids_.try_emplace(std::string{policy}, static_cast<uint32_t>(policies_.size()));
  if (inserted) {
    policies_.emplace_back(policy);
  }
  return it->second;
}

Class_id Egraph::intern(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands, bool& created) {
  const uint64_t h        = hash_of(op, attrs, operands);
  auto           bucket   = buckets_.find(h);
  Enode_id       previous = kNoEnode;
  if (bucket != buckets_.end()) {
    // A 64-bit hash is a FILTER: compare the whole key before merging, because
    // a false hash-cons merge is a silent miscompile.
    for (Enode_id id = bucket->second; id != kNoEnode; id = enodes_[id].chain) {
      previous            = id;
      const Enode& member = enodes_[id];
      if (member.op != op || !(member.attrs == attrs) || member.n_operands != operands.size()) {
        continue;
      }
      if (std::equal(operands.begin(), operands.end(), pool_.begin() + member.first_operand)) {
        created = false;
        return member.cls;
      }
    }
  }
  created = true;

  Enode node;
  node.op            = op;
  node.attrs         = attrs;
  node.first_operand = static_cast<uint32_t>(pool_.size());
  node.n_operands    = static_cast<uint32_t>(operands.size());
  node.hash          = h;
  pool_.insert(pool_.end(), operands.begin(), operands.end());

  classes_.push_back(Eclass{Kind::node, kNoEnode, std::nullopt});
  node.cls                    = static_cast<Class_id>(classes_.size() - 1);
  const auto id               = static_cast<Enode_id>(enodes_.size());
  classes_[node.cls].first    = id;
  enodes_.push_back(node);
  if (bucket == buckets_.end()) {
    buckets_.emplace(h, id);
  } else {
    enodes_[previous].chain = id;
  }
  return node.cls;
}

void Egraph::set_value(Class_id cls, Dlop value) { classes_[cls].value = std::move(value); }

// ---------------------------------------------------------------------------
// Constant analysis (three-valued; folds only to fully known values)
// ---------------------------------------------------------------------------

namespace {

// One evaluated operand: the sink port it occupies, its bank, its class and
// the class's analysis value (null = no value).
struct Eval_operand {
  hhds::Port_id port = 0;
  uint32_t      bank = 0;
  Class_id      cls  = kNoClass;
  const Dlop*   val  = nullptr;
};

struct Fold {
  enum class Kind : uint8_t { none, value, forward };
  Kind     kind = Kind::none;
  Dlop     value;
  Class_id fwd = kNoClass;
};

Fold fold_value(const Dlop& v) {
  Fold f;
  // Invalid / nil are "no value" (a non-numeric operand, an illegal shift): the
  // node keeps computing. x/0 and x%0 are NOT here: Dlop makes them all-unknown.
  if (v.is_invalid() || v.is_nil()) {
    return f;
  }
  f.kind  = Fold::Kind::value;
  f.value = v;
  return f;
}

Fold fold_value(const spool_ptr<Dlop>& v) { return fold_value(*v); }

Fold fold_forward(Class_id cls) {
  Fold f;
  f.kind = Fold::Kind::forward;
  f.fwd  = cls;
  return f;
}

[[nodiscard]] const Eval_operand* find_port(std::span<const Eval_operand> ops, hhds::Port_id port) {
  for (const auto& op : ops) {
    if (op.port == port) {
      return &op;
    }
  }
  return nullptr;
}

// A constant bit window [lo,hi) operand pair, validated exactly like
// graph_util::bit_range.
[[nodiscard]] std::optional<std::pair<int, int>> window_of(const Dlop& l, const Dlop& h) {
  if (!l.is_integer() || !h.is_integer() || !l.is_just_i64() || !h.is_just_i64() || l.has_unknowns() || h.has_unknowns()
      || l.to_just_i64() < 0 || h.to_just_i64() <= l.to_just_i64() || h.to_just_i64() > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return std::pair{static_cast<int>(l.to_just_i64()), static_cast<int>(h.to_just_i64())};
}

[[nodiscard]] bool is_two_arm(std::span<const Eval_operand> ops) {
  return ops.size() == 3 && ops[0].port == 0 && ops[1].port == 1 && ops[2].port == 2;
}

// The folding table. `ops` is in ascending sink-port order. The result is the
// three-valued value of the node when EVERY operand has one (a Mux needs only
// its selector), mirroring the all-constant folds of classic cprop one op at a
// time. Whether the value may become a constant is the caller's P7 decision.
Fold fold_node(Ntype_op op, std::span<const Eval_operand> ops, const Egraph& eg) {
  const auto none = Fold{};
  if (ops.empty()) {
    return none;
  }

  if (op == Ntype_op::Mux && ops.size() >= 2 && ops[0].port == 0) {
    // Q1: an out-of-range or disconnected selection is '?', so comparing only the
    // CONNECTED arms is sound: Mux(s, a, a, a) and Mux(s, a, <unconnected>, a)
    // are both `a`, because the '?' of every other selection may resolve to `a`.
    bool same = true;
    for (size_t i = 2; i < ops.size(); ++i) {
      same = same && ops[i].cls == ops[1].cls;
    }
    if (same) {
      return fold_forward(ops[1].cls);
    }
  }

  if (op == Ntype_op::Mux) {
    const auto* sel = ops[0].port == 0 ? ops[0].val : nullptr;
    if (sel == nullptr || !sel->is_numeric() || sel->has_unknowns()) {
      return none;
    }
    const bool binary = is_two_arm(ops);
    if (!binary && !sel->is_just_i64()) {
      return none;
    }
    const int64_t pick = binary ? (sel->is_known_zero() ? 0 : 1) : sel->to_just_i64();
    if (pick < 0) {
      return none;
    }
    // An out-of-range or disconnected selection is '?' (Q1); the engine does
    // not pick a refinement for it, so it keeps the Mux.
    if (const auto* arm = find_port(ops, static_cast<hhds::Port_id>(pick + 1)); arm != nullptr && arm->port != 0) {
      return fold_forward(arm->cls);
    }
    return none;
  }

  if (op == Ntype_op::Hotmux) {
    // Interleaved (control, value) pairs and an optional trailing default. Only
    // exclusive Hotmuxes reach the build (a non-exclusive one is an observable
    // overlap obligation and stays a skeleton leaf).
    for (size_t i = 0; i < ops.size(); ++i) {
      if (ops[i].port != i) {
        return none;
      }
    }
    const size_t pairs   = ops.size() / 2;
    const bool   default_arm = ops.size() % 2 == 1;
    if (pairs == 0) {
      return none;
    }
    // Every control a known constant: the selected value (or the default, or 0).
    // Two active controls are overlapping controls: preserve them.
    bool all_controls_const = true;
    for (size_t i = 0; i < pairs; ++i) {
      all_controls_const = all_controls_const && ops[2 * i].val != nullptr && !ops[2 * i].val->has_unknowns();
    }
    if (all_controls_const) {
      std::optional<Class_id> selected;
      for (size_t i = 0; i < pairs; ++i) {
        if (!ops[2 * i].val->is_known_zero()) {
          if (selected) {
            return none;
          }
          selected = ops[2 * i + 1].cls;
        }
      }
      if (selected) {
        return fold_forward(*selected);
      }
      if (default_arm) {
        return fold_forward(ops.back().cls);
      }
      return fold_value(*Dlop::create_integer(0));
    }
    // Equal values collapse only with established exclusivity (true here): the
    // all-controls-zero result has to agree too, the default or a literal 0.
    for (size_t i = 1; i < pairs; ++i) {
      if (ops[2 * i + 1].cls != ops[1].cls) {
        return none;
      }
    }
    if (default_arm) {
      return ops.back().cls == ops[1].cls ? fold_forward(ops[1].cls) : none;
    }
    return ops[1].val != nullptr && ops[1].val->is_known_zero() ? fold_forward(ops[1].cls) : none;
  }

  for (const auto& operand : ops) {
    if (operand.val == nullptr) {
      return none;
    }
  }

  switch (op) {
    case Ntype_op::SHL: {
      const auto *a = find_port(ops, 0), *b = find_port(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        return none;
      }
      if (b->val->is_known_zero() && a->val->is_integer()) {
        return fold_forward(a->cls);  // a << 0 is exactly a
      }
      Dlop result;
      result = Dlop::create_integer(0);
      result = result.or_op(a->val->shl_op(*b->val));
      return fold_value(result);
    }
    case Ntype_op::SRA: {
      const auto *a = find_port(ops, 0), *b = find_port(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        return none;
      }
      // is_just_i64 also rejects >62-bit amounts, whose sra_op yields nil.
      if (b->val->has_unknowns() || b->val->is_negative() || !b->val->is_just_i64()) {
        return none;
      }
      if (b->val->is_known_zero() && a->val->is_integer()) {
        return fold_forward(a->cls);  // a >> 0 is exactly a
      }
      return fold_value(a->val->sra_op(*b->val));
    }
    case Ntype_op::Ror: {
      // Unary three-valued OR-reduction per operand: known 1 when a bit is set,
      // known 0 when every operand is zero, else undecided.
      bool any_true = false;
      bool any_unk  = false;
      for (const auto& operand : ops) {
        const auto r = operand.val->ror_op();
        if (r->is_invalid() || r->is_nil()) {
          return none;
        }
        any_true = any_true || r->is_known_true();
        any_unk  = any_unk || r->has_unknowns();
      }
      if (!any_true && any_unk) {
        return none;
      }
      return fold_value(*Dlop::create_integer(any_true ? 1 : 0));
    }
    case Ntype_op::Rxor:
    case Ntype_op::Popcount: {
      const auto *a = find_port(ops, 0), *b = find_port(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr || b->val->has_unknowns() || !b->val->is_just_i64()
          || b->val->to_just_i64() < 0 || b->val->to_just_i64() > std::numeric_limits<int>::max()) {
        return none;
      }
      const auto selected = a->val->get_mask_op_opt(0, static_cast<int>(b->val->to_just_i64()));
      return fold_value(op == Ntype_op::Rxor ? selected->rxor_op() : selected->popcount_op());
    }
    case Ntype_op::Set_mask: {
      const auto *a = find_port(ops, 0), *lo = find_port(ops, 2), *hi = find_port(ops, 3), *v = find_port(ops, 4);
      if (ops.size() != 4 || a == nullptr || lo == nullptr || hi == nullptr || v == nullptr) {
        return none;
      }
      const auto window = window_of(*lo->val, *hi->val);
      if (!window) {
        return none;
      }
      return fold_value(livehd::eval_set_mask(*a->val, *v->val, window->first, window->second));
    }
    case Ntype_op::Get_mask: {
      const auto *a = find_port(ops, 0), *lo = find_port(ops, 2), *hi = find_port(ops, 3);
      if (ops.size() != 3 || a == nullptr || lo == nullptr || hi == nullptr) {
        return none;
      }
      const auto window = window_of(*lo->val, *hi->val);
      if (!window) {
        return none;
      }
      const Dlop result = livehd::eval_get_mask(*a->val, window->first, window->second);
      // A window that covers the whole '?' literal is exactly that literal:
      // forward its class instead of describing a new one (P7).
      if (result.has_unknowns() && result.same_repr(*a->val) && eg.cls(a->cls).kind == Egraph::Kind::literal) {
        return fold_forward(a->cls);
      }
      return fold_value(result);
    }
    case Ntype_op::Sum: {
      Dlop result;
      result = Dlop::create_integer(0);
      for (const auto& operand : ops) {
        // EVEN pid = added, ODD = subtracted; one pid per operand (cell.hpp).
        result = operand.bank == 0 ? result.add_op(*operand.val) : result.sub_op(*operand.val);
      }
      return fold_value(result);
    }
    case Ntype_op::Or: {
      Dlop result;
      result = Dlop::create_integer(0);
      for (const auto& operand : ops) {
        result = result.or_op(*operand.val);
      }
      return fold_value(result);
    }
    case Ntype_op::And: {
      Dlop result;
      result = Dlop::create_integer(-1);
      for (const auto& operand : ops) {
        result = result.and_op(*operand.val);
      }
      return fold_value(result);
    }
    case Ntype_op::Xor: {
      Dlop result;
      result = Dlop::create_integer(0);
      for (const auto& operand : ops) {
        result = result.xor_op(*operand.val);
      }
      return fold_value(result);
    }
    case Ntype_op::Mult: {
      Dlop result;
      result = Dlop::create_integer(1);
      for (const auto& operand : ops) {
        result = result.mult_op(*operand.val);
      }
      return fold_value(result);
    }
    case Ntype_op::EQ: {
      // "All operands equal", three-valued: a known mismatch decides false; an
      // agreeing pair with unknown bits is undecided.
      bool eq      = true;
      bool any_unk = false;
      for (size_t i = 1; i < ops.size(); ++i) {
        const auto r = ops[0].val->eq_op(*ops[i].val);
        if (r->is_known_false()) {
          eq = false;
          break;
        }
        any_unk = any_unk || r->has_unknowns();
      }
      if (eq && any_unk) {
        return none;
      }
      return fold_value(*Dlop::create_integer(eq ? 1 : 0));
    }
    case Ntype_op::LT:
    case Ntype_op::GT: {
      // Each side is an operand BANK that reduces by sum.
      Dlop a_sum, b_sum;
      a_sum      = Dlop::create_integer(0);
      b_sum      = Dlop::create_integer(0);
      bool has_a = false, has_b = false;
      for (const auto& operand : ops) {
        const bool is_b        = operand.bank != 0;
        (is_b ? b_sum : a_sum) = (is_b ? b_sum : a_sum).add_op(*operand.val);
        (is_b ? has_b : has_a) = true;
      }
      if (!has_a || !has_b || a_sum.is_invalid() || b_sum.is_invalid() || a_sum.is_nil() || b_sum.is_nil()) {
        return none;
      }
      const auto cmp = op == Ntype_op::LT ? a_sum.lt_op(b_sum) : a_sum.gt_op(b_sum);
      if (cmp->has_unknowns()) {
        return none;
      }
      return fold_value(*Dlop::create_integer(cmp->is_known_true() ? 1 : 0));
    }
    case Ntype_op::Div:
    case Ntype_op::Rem: {
      const auto *a = find_port(ops, 0), *b = find_port(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        return none;
      }
      // Truncated remainder (the sign follows the dividend), like Verilog `%`.
      return fold_value(op == Ntype_op::Div ? a->val->div_op(*b->val) : a->val->rem_op(*b->val));
    }
    case Ntype_op::Not: {
      if (ops.size() != 1) {
        return none;
      }
      return fold_value(ops[0].val->not_op());
    }
    case Ntype_op::Sext: {
      // Sext(a, b) keeps a's low b bits and sign-extends from bit b-1. The
      // cell's b is the kept bit COUNT while Dlop::sext_op takes the sign-bit
      // POSITION (the same keep-1 cgen_verilog and the LEC encoder use).
      const auto *a = find_port(ops, 0), *b = find_port(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr || !b->val->is_numeric() || b->val->has_unknowns()
          || !b->val->is_just_i64() || b->val->to_just_i64() < 1 || b->val->to_just_i64() > (int64_t{1} << 30)) {
        return none;
      }
      return fold_value(a->val->sext_op(*Dlop::create_integer(b->val->to_just_i64() - 1)));
    }
    case Ntype_op::Concat: {
      // Interleaved (value, width) lanes, MSB first: lane i is pids 2i / 2i+1.
      if (ops.size() % 2 != 0) {
        return none;
      }
      std::vector<Dlop>              values(ops.size() / 2);
      std::vector<Dlop::Concat_lane> lanes;
      std::vector<int>               widths(ops.size() / 2);
      for (size_t i = 0; i < ops.size() / 2; ++i) {
        const auto& value = ops[2 * i];
        const auto& width = ops[2 * i + 1];
        if (value.port != 2 * i || width.port != 2 * i + 1 || !width.val->is_just_i64() || width.val->has_unknowns()) {
          return none;
        }
        const auto w = width.val->to_just_i64();
        if (w <= 0 || w > std::numeric_limits<int32_t>::max()) {
          return none;
        }
        values[i] = *value.val;
        widths[i] = static_cast<int>(w);
        // Dlop::concat_op asserts that a lane fits its window; get_payload_bits
        // over-approximates, so this refuses slightly more than the assert.
        if (values[i].get_payload_bits() > widths[i]) {
          return none;
        }
      }
      lanes.reserve(values.size());
      for (size_t i = 0; i < values.size(); ++i) {
        lanes.push_back(Dlop::Concat_lane{&values[i], widths[i]});
      }
      const auto result = Dlop::concat_op(std::span<const Dlop::Concat_lane>(lanes.data(), lanes.size()));
      if (!result->is_integer()) {
        return none;
      }
      if (result->has_unknowns() && values.size() == 1 && result->same_repr(values[0])
          && eg.cls(ops[0].cls).kind == Egraph::Kind::literal) {
        return fold_forward(ops[0].cls);  // a single lane wide enough for its whole '?' literal
      }
      return fold_value(*result);
    }
    default: return none;
  }
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

[[nodiscard]] bool is_dead_eligible(const hhds::Node_class& node) {
  if (node.is_invalid() || gu::is_builtin_node(node)) {
    return false;
  }
  const auto op = gu::type_op_of(node);
  return Ntype::is_comb(op) && op != Ntype_op::Hotmux && op != Ntype_op::Clock_cell && !gu::has_runtime_check(node);
}

class Engine {
public:
  Engine(hhds::Graph& graph, const Cycle_cutter& cut_cycles) : g_(graph), cut_cycles_(cut_cycles) {}

  Stats run();

private:
  // How a pure node entered the e-graph.
  enum class How : uint8_t { created, merged, constant, forward };
  struct Built {
    Class_id cls = kNoClass;
    How      how = How::created;
  };
  struct Member {
    hhds::Node_class node;
    Class_id         cls;
    How              how;
  };
  enum class Replaced : uint8_t { done, dead, declined };

  void             collect_nodes();
  struct Component {
    std::vector<hhds::Node_class> nodes;
    bool                          cyclic = false;
  };
  [[nodiscard]] std::vector<Component> components(const std::vector<hhds::Node_class>& subset) const;
  [[nodiscard]] const Dlop*            known_value(const hhds::Pin_class& pin) const;
  // The one data port a Mux with a known selector reads, if any.
  [[nodiscard]] std::optional<hhds::Port_id> live_mux_port(const hhds::Node_class& mux) const;
  void                                 build_all(const std::vector<hhds::Node_class>& producers_first);
  void                                 resolve_false_cycles();
  [[nodiscard]] bool is_pure_candidate(const hhds::Node_class& node) const;
  Class_id         class_of(const hhds::Pin_class& pin);
  std::optional<Built> build(const hhds::Node_class& node);
  hhds::Pin_class  realize(Class_id cls);
  void             elaborate();
  Replaced         replace(const Member& member, const hhds::Pin_class& target);
  void             sweep_dead();

  hhds::Graph&         g_;
  const Cycle_cutter&  cut_cycles_;
  Egraph               eg_;
  Stats        stats_;

  std::vector<hhds::Node_class>                      nodes_;
  std::vector<hhds::Node_class>                      cyclic_nodes_;  // members of the remaining cyclic components
  absl::flat_hash_set<hhds::Class_index>             cyclic_;        // the same, by node
  absl::flat_hash_map<hhds::Class_index, Class_id>   pin_class_;
  std::vector<hhds::Pin_class>                       rep_pin_;  // class -> the pin that realizes it (invalid until known)
  std::vector<Member>                                members_;
};

void Engine::collect_nodes() {
  absl::flat_hash_set<hhds::Class_index> seen;
  for (auto node : g_.body().nodes(hhds::Node_order::forward)) {
    if (seen.insert(node.get_class_index()).second) {
      nodes_.push_back(node);
    }
  }
  // The forward order may cut a loop; every other node still gets a (leaf)
  // treatment, in storage order.
  for (auto node : g_.body().nodes()) {
    if (seen.insert(node.get_class_index()).second) {
      nodes_.push_back(node);
    }
  }
}

// The known value of a pin: a constant, or an already-built class with a value.
const Dlop* Engine::known_value(const hhds::Pin_class& pin) const {
  if (pin.is_const()) {
    return &gu::const_of(pin);
  }
  if (const auto it = pin_class_.find(pin.get_class_index()); it != pin_class_.end()) {
    return eg_.value_of(it->second);
  }
  return nullptr;
}

std::optional<hhds::Port_id> Engine::live_mux_port(const hhds::Node_class& mux) const {
  const auto selector = mux.get_sink_pin(0).get_driver_pins();
  if (selector.size() != 1) {
    return std::nullopt;
  }
  const auto* sel = known_value(selector.front());
  if (sel == nullptr || !sel->is_numeric() || sel->has_unknowns()) {
    return std::nullopt;
  }
  bool binary = true;
  {
    hhds::Port_id expect = 0;
    for (const auto& sink : mux.inp_sorted_pins()) {
      binary = binary && sink.get_port_id() == expect++;
    }
    binary = binary && expect == 3;
  }
  if (!binary && !sel->is_just_i64()) {
    return std::nullopt;
  }
  const int64_t pick = binary ? (sel->is_known_zero() ? 0 : 1) : sel->to_just_i64();
  if (pick < 0 || pick >= (int64_t{1} << 20)) {
    return std::nullopt;
  }
  const auto port = static_cast<hhds::Port_id>(pick + 1);
  for (const auto& sink : mux.inp_sorted_pins()) {
    if (sink.get_port_id() == port) {
      return port;
    }
  }
  return std::nullopt;  // an out-of-range or disconnected selection stays a Mux (Q1)
}

// Pre-pass: Tarjan's SCC over the combinational structure of `subset`. State
// (loop-last ops) and the IO boundary break cycles; a non-trivial SCC (or a self
// edge) is a comb cycle, possibly a FALSE one at word level (a packed struct
// written field by field), and its members are OPAQUE: never hash-consed, never
// folded. Components come back in Tarjan's emission order, consumers first (the
// reverse is a producers-first topological order of the acyclic part). A Mux
// with a known selector reads only its chosen arm, so the other arms are not
// data edges: a feedback ring closed through constant-selected routers is not
// a cycle. Iterative: a design's cone can be deeper than any stack.
std::vector<Engine::Component> Engine::components(const std::vector<hhds::Node_class>& subset) const {
  const auto n = static_cast<uint32_t>(subset.size());
  absl::flat_hash_map<hhds::Class_index, uint32_t> index_of;
  index_of.reserve(n);
  const auto transparent = [](const hhds::Node_class& node) {
    return !gu::is_builtin_node(node) && !Ntype::is_loop_last(gu::type_op_of(node)) && gu::type_op_of(node) != Ntype_op::IO;
  };
  for (uint32_t i = 0; i < n; ++i) {
    index_of.emplace(subset[i].get_class_index(), i);
  }
  std::vector<absl::InlinedVector<uint32_t, 4>> adj(n);
  std::vector<bool>                             self(n, false);
  absl::flat_hash_map<hhds::Class_index, std::optional<hhds::Port_id>> live;
  for (uint32_t i = 0; i < n; ++i) {
    if (!transparent(subset[i])) {
      continue;
    }
    for (const auto& out : subset[i].out_sorted_pins()) {
      for (const auto& edge : out.out_edges()) {
        const auto sink = edge.sink.get_master_node();
        if (!transparent(sink)) {
          continue;
        }
        if (gu::type_op_of(sink) == Ntype_op::Mux && edge.sink.get_port_id() != 0) {
          auto it = live.find(sink.get_class_index());
          if (it == live.end()) {
            it = live.emplace(sink.get_class_index(), live_mux_port(sink)).first;
          }
          if (it->second && *it->second != edge.sink.get_port_id()) {
            continue;
          }
        }
        const auto it = index_of.find(sink.get_class_index());
        if (it == index_of.end()) {
          continue;
        }
        if (it->second == i) {
          self[i] = true;
        }
        adj[i].push_back(it->second);
      }
    }
  }

  std::vector<Component> out;
  constexpr uint32_t     kUnvisited = UINT32_MAX;
  std::vector<uint32_t>  disc(n, kUnvisited), low(n, 0);
  std::vector<bool>      on_stack(n, false);
  std::vector<uint32_t>  stack;
  struct Frame {
    uint32_t node;
    uint32_t cursor;
  };
  std::vector<Frame> call;
  uint32_t           counter = 0;
  for (uint32_t root = 0; root < n; ++root) {
    if (disc[root] != kUnvisited || !transparent(subset[root])) {
      continue;
    }
    call.push_back({root, 0});
    disc[root] = low[root] = counter++;
    stack.push_back(root);
    on_stack[root] = true;
    while (!call.empty()) {
      const auto v = call.back().node;
      if (call.back().cursor < adj[v].size()) {
        const auto w = adj[v][call.back().cursor++];
        if (disc[w] == kUnvisited) {
          disc[w] = low[w] = counter++;
          stack.push_back(w);
          on_stack[w] = true;
          call.push_back({w, 0});
        } else if (on_stack[w]) {
          low[v] = std::min(low[v], disc[w]);
        }
        continue;
      }
      if (low[v] == disc[v]) {
        Component component;
        uint32_t  w = 0;
        do {
          w = stack.back();
          stack.pop_back();
          on_stack[w] = false;
          component.nodes.push_back(subset[w]);
        } while (w != v);
        component.cyclic = component.nodes.size() > 1 || self[v];
        out.push_back(std::move(component));
      }
      call.pop_back();
      if (!call.empty()) {
        low[call.back().node] = std::min(low[call.back().node], low[v]);
      }
    }
  }
  return out;
}

// A pure node enters the e-graph; everything else is a skeleton leaf. The
// skeleton keeps its original node: operands are rewired to the extracted
// representatives when a duplicate is retired, and its outputs are leaves.
bool Engine::is_pure_candidate(const hhds::Node_class& node) const {
  if (gu::is_builtin_node(node) || cyclic_.contains(node.get_class_index())) {
    return false;
  }
  const auto op = gu::type_op_of(node);
  // Clock_cell is a clock-network operator, LUT's function table lives in a
  // node attribute the key does not cover, and a runtime_check node is an
  // observable obligation: all skeleton leaves. A non-exclusive Hotmux is the
  // UnusedOverlapSurvivesForFormal obligation: never merged, never deleted.
  if (!Ntype::is_comb(op) || op == Ntype_op::Clock_cell || op == Ntype_op::LUT || gu::has_runtime_check(node)) {
    return false;
  }
  if (op == Ntype_op::Hotmux && !livehd::muxctx::exclusive(node)) {
    return false;
  }
  return true;
}

Class_id Engine::class_of(const hhds::Pin_class& pin) {
  const auto key = pin.get_class_index();
  if (const auto it = pin_class_.find(key); it != pin_class_.end()) {
    return it->second;
  }
  Class_id cls = kNoClass;
  if (pin.is_const()) {
    const auto& value = gu::const_of(pin);
    // A '?'-bearing constant is a literal: one const node, never merged by
    // value. Everything else interns by value.
    cls = value.has_unknowns() ? eg_.add_literal(value) : eg_.add_const(value);
  } else {
    cls = eg_.add_leaf();
  }
  if (rep_pin_.size() <= cls) {
    rep_pin_.resize(cls + 1);
  }
  if (rep_pin_[cls].is_invalid()) {
    rep_pin_[cls] = pin;
  }
  pin_class_.emplace(key, cls);
  return cls;
}

std::optional<Engine::Built> Engine::build(const hhds::Node_class& node) {
  const auto op = gu::type_op_of(node);

  // One driver pin, port 0: anything else is not a value the engine models.
  hhds::Pin_class out;
  size_t          n_out = 0;
  for (const auto& pin : node.out_sorted_pins()) {
    out = pin;
    ++n_out;
  }
  if (n_out != 1 || out.get_port_id() != 0) {
    return std::nullopt;
  }

  absl::InlinedVector<Eval_operand, 8> eval;
  absl::InlinedVector<Operand, 8>      operands;
  for (const auto& sink : node.inp_sorted_pins()) {
    const auto drivers = sink.get_driver_pins();
    if (drivers.size() != 1 || drivers.front().is_invalid()) {
      return std::nullopt;  // an unconnected or multi-driver sink: not a plain operand
    }
    const auto cls  = class_of(drivers.front());
    const auto bank = static_cast<uint32_t>(Ntype::sink_bank(op, sink.get_port_id()));
    eval.push_back(Eval_operand{sink.get_port_id(), bank, cls, nullptr});
    operands.push_back(Operand{bank, cls});
  }
  if (operands.empty()) {
    return std::nullopt;
  }
  // Value pointers only now: class_of above can grow the class table, which
  // would move the values an earlier pointer referred to.
  for (auto& operand : eval) {
    operand.val = eg_.value_of(operand.cls);
  }

  // Constant analysis first: a forward or a fully known value needs no e-node.
  Fold fold = fold_node(op, eval, eg_);
  if (fold.kind == Fold::Kind::forward) {
    return Built{fold.fwd, How::forward};
  }
  if (fold.kind == Fold::Kind::value && !fold.value.has_unknowns()) {
    return Built{eg_.add_const(fold.value), How::constant};
  }

  // Normalization. A commutative bank is a multiset: sort by class, keep
  // multiplicity (Sum(a,a) = 2a, Xor(a,a) = 0). A positional operand's bank is
  // its port id, so the sort keeps positional operands in port order.
  if (op == Ntype_op::Hotmux) {
    // Whole (control, value) pairs commute only with established exclusivity.
    // Keep dense pair association; a trailing default stays where it is.
    for (size_t i = 0; i + 1 < operands.size(); ++i) {
      if (eval[i].port != i) {
        return std::nullopt;
      }
    }
    std::vector<std::pair<Operand, Operand>> pairs;
    for (size_t i = 0; i + 1 < operands.size(); i += 2) {
      pairs.emplace_back(operands[i], operands[i + 1]);
    }
    std::ranges::sort(pairs, [](const auto& x, const auto& y) {
      return std::pair{x.first.cls, x.second.cls} < std::pair{y.first.cls, y.second.cls};
    });
    for (size_t i = 0; i < pairs.size(); ++i) {
      operands[2 * i].cls     = pairs[i].first.cls;
      operands[2 * i + 1].cls = pairs[i].second.cls;
    }
  } else {
    std::ranges::sort(operands);
  }

  Enode_attrs attrs;
  attrs.has_color = gu::has_color(node);
  attrs.color     = attrs.has_color ? gu::color_of(node) : 0;
  attrs.has_proven = gu::has_proven(node);
  attrs.proven     = attrs.has_proven ? gu::proven_of(node) : 0;
  if (const auto policy = node.attr(livehd::attrs::synth_policy); policy.has()) {
    attrs.policy = eg_.policy_id(policy.get());
  }

  bool       created = false;
  const auto cls     = eg_.intern(op, attrs, operands, created);
  if (rep_pin_.size() <= cls) {
    rep_pin_.resize(cls + 1);
  }
  if (created) {
    rep_pin_[cls] = out;
    if (fold.kind == Fold::Kind::value) {
      eg_.set_value(cls, fold.value);  // a three-valued value; never materialized (P7)
    }
  }
  return Built{cls, created ? How::created : How::merged};
}

hhds::Pin_class Engine::realize(Class_id cls) {
  if (rep_pin_.size() <= cls) {
    rep_pin_.resize(cls + 1);
  }
  if (rep_pin_[cls].is_invalid()) {
    const auto& c = eg_.cls(cls);
    I(c.kind == Egraph::Kind::konst && c.value.has_value());
    rep_pin_[cls] = gu::create_const(g_, *c.value);
    ++stats_.consts_created;
  }
  return rep_pin_[cls];
}

// Retire `member.node`, whose class is realized by `target`. false = declined:
// the node stays, because forwarding would change what a consumer sees.
Engine::Replaced Engine::replace(const Member& member, const hhds::Pin_class& target) {
  auto       node   = member.node;
  const auto source = node.get_driver_pin(0);
  if (!node.has_out_edges()) {
    return Replaced::dead;  // the sweep deletes it
  }

  const auto out_view = node.out_edges();
  gu::Edge_vec outs(out_view.begin(), out_view.end());

  if (!target.is_const()) {
    for (const auto& out : outs) {
      // A ring that closes over a compact loop keeps this node: forwarding
      // would make the loop's output drive its own input, which reads as a carry.
      if (gu::closes_loop_self_edge(target, out.sink)) {
        return Replaced::declined;
      }
      // Parallel-edge refusal: if the target ALREADY drives this consumer sink,
      // the reconnect needs a second parallel edge and hhds dedups it, so the
      // consumer would lose an operand (Sum(a,a') = 2a halves to a). And/Or are
      // idempotent, so a dropped duplicate is value-neutral there.
      const auto consumer_op = gu::type_op_of(out.sink.get_master_node());
      if (consumer_op != Ntype_op::And && consumer_op != Ntype_op::Or) {
        for (const auto& driver : out.sink.get_driver_pins()) {
          if (driver == target) {
            return Replaced::declined;
          }
        }
      }
    }

    const auto target_node = target.get_master_node();
    // Naming policy for a CSE twin: user wires are addressable by sim queries
    // and VCD, so keep the named one, and when BOTH carry names keep both.
    if (!gu::is_builtin_node(target_node) && gu::type_op_of(target_node) == gu::type_op_of(node)
        && Ntype::is_comb(gu::type_op_of(target_node)) && !Ntype::is_loop_last(gu::type_op_of(target_node))) {
      const bool kept_named = gu::has_name(target_node) || !gu::pin_name_of(target).empty();
      const bool node_named = gu::has_name(node) || !gu::pin_name_of(source).empty();
      if (kept_named && node_named) {
        return Replaced::declined;
      }
      if (node_named) {
        if (gu::has_name(node)) {
          target_node.attr(hhds::attrs::name).set(std::string{gu::node_name_of(node)});
        }
        if (!gu::pin_name_of(source).empty()) {
          gu::set_pin_name(target, gu::pin_name_of(source));
        }
      }
    }
    // Class members are equal as unbounded integers, so the retired root's
    // width stamp is a valid realization of the representative when it has none.
    if (!gu::is_builtin_node(target_node) && Ntype::is_comb(gu::type_op_of(target_node)) && gu::bits_of(target) == 0
        && gu::bits_of(source) != 0) {
      gu::set_bits(target, gu::bits_of(source));
      if (!gu::is_unsign(source)) {
        gu::set_sign(target);
      }
    }
  }

  // Redirect every consumer, then delete the node in one shot.
  for (const auto& out : outs) {
    target.connect_sink(out.sink);
  }
  node.del_node();
  return Replaced::done;
}

void Engine::elaborate() {
  for (const auto& member : members_) {
    const auto target = realize(member.cls);
    if (target == member.node.get_driver_pin(0)) {
      continue;  // the class representative is the original node: reused as is
    }
    const auto result = replace(member, target);
    if (result == Replaced::declined) {
      ++stats_.kept_twin;
    } else if (result == Replaced::done) {
      switch (member.how) {
        case How::constant: ++stats_.const_folded; break;
        case How::forward : ++stats_.forwarded; break;
        default           : ++stats_.cse_merged; break;
      }
    }
  }
}

// Dead pure nodes: a node nobody reads is deleted, and so is any pure producer
// that loses its last consumer. State, IO, Sub and every obligation are roots.
void Engine::sweep_dead() {
  std::vector<hhds::Node_class> storage;
  for (auto node : g_.body().nodes()) {
    storage.push_back(node);
  }
  for (auto it = storage.rbegin(); it != storage.rend(); ++it) {
    if (it->is_invalid() || !is_dead_eligible(*it) || it->has_out_edges()) {
      continue;
    }
    std::deque<hhds::Node_class>           work;
    absl::flat_hash_set<hhds::Class_index> queued;
    work.push_back(*it);
    queued.insert(it->get_class_index());
    while (!work.empty()) {
      auto cur = work.front();
      work.pop_front();
      if (cur.is_invalid() || !is_dead_eligible(cur) || cur.has_out_edges()) {
        continue;
      }
      for (const auto& in_pin : cur.inp_sorted_pins()) {
        const auto driver = in_pin.get_driver_pin();
        if (driver.is_invalid() || driver.is_const()) {
          continue;
        }
        const auto master = driver.get_master_node();
        if (is_dead_eligible(master) && queued.insert(master.get_class_index()).second) {
          work.push_back(master);
        }
      }
      cur.del_node();
      ++stats_.dead_removed;
    }
  }
}

// Build every candidate of `producers_first` in order.
void Engine::build_all(const std::vector<hhds::Node_class>& producers_first) {
  for (const auto& node : producers_first) {
    if (node.is_invalid() || gu::is_builtin_node(node)) {
      continue;
    }
    if (!is_pure_candidate(node)) {
      ++stats_.skeleton;
      continue;
    }
    if (!node.has_out_edges()) {
      continue;  // dead on arrival: the sweep deletes it
    }
    const auto cls = build(node);
    if (!cls) {
      ++stats_.skeleton;
      continue;
    }
    ++stats_.pure;
    // The node's output now reads as its class for every consumer built later.
    pin_class_[node.get_driver_pin(0).get_class_index()] = cls->cls;
    members_.push_back(Member{node, cls->cls, cls->how});
  }
}

// A component that stays cyclic only because a Mux's selector was not yet known
// when the pre-pass ran (the selector is itself computed, e.g. a constant that
// crossed an inlined module boundary) is re-examined once the acyclic part has
// been built: a selector with a value cuts the dead arms, and the members that
// become acyclic are built in their own producers-first order.
void Engine::resolve_false_cycles() {
  while (!cyclic_nodes_.empty()) {
    bool cuttable = false;
    for (const auto& node : cyclic_nodes_) {
      cuttable = cuttable || (gu::type_op_of(node) == Ntype_op::Mux && live_mux_port(node).has_value());
    }
    if (!cuttable) {
      return;
    }
    std::vector<hhds::Node_class> resolved;
    std::vector<hhds::Node_class> remaining;
    const auto                    parts = components(cyclic_nodes_);
    for (const auto& part : parts) {
      if (part.cyclic) {
        remaining.insert(remaining.end(), part.nodes.begin(), part.nodes.end());
      }
    }
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {  // consumers first -> reverse
      if (!it->cyclic) {
        resolved.insert(resolved.end(), it->nodes.begin(), it->nodes.end());
      }
    }
    if (resolved.empty()) {
      return;
    }
    for (const auto& node : resolved) {
      cyclic_.erase(node.get_class_index());
    }
    cyclic_nodes_ = std::move(remaining);
    build_all(resolved);
  }
}

Stats Engine::run() {
  collect_nodes();
  stats_.nodes_in = nodes_.size();

  // IO, state and other non-transparent nodes are skeleton leaves.
  std::vector<hhds::Node_class> transparent;
  for (const auto& node : nodes_) {
    if (!gu::is_builtin_node(node) && (Ntype::is_loop_last(gu::type_op_of(node)) || gu::type_op_of(node) == Ntype_op::IO)) {
      ++stats_.skeleton;
    } else if (!gu::is_builtin_node(node)) {
      transparent.push_back(node);
    }
  }

  // Pre-pass: combinational SCCs. The classic scalar rewrites get one chance to
  // cut the false ones; the body is then collected again.
  auto parts = components(transparent);
  if (cut_cycles_) {
    absl::flat_hash_set<hhds::Class_index> cyclic_members;
    for (const auto& part : parts) {
      if (part.cyclic) {
        for (const auto& node : part.nodes) {
          cyclic_members.insert(node.get_class_index());
        }
      }
    }
    if (!cyclic_members.empty()) {
      std::vector<hhds::Node_class> members;
      for (const auto& node : nodes_) {
        if (cyclic_members.contains(node.get_class_index())) {
          members.push_back(node);
        }
      }
      cut_cycles_(members);
      nodes_.clear();
      transparent.clear();
      collect_nodes();
      for (const auto& node : nodes_) {
        if (!gu::is_builtin_node(node) && !Ntype::is_loop_last(gu::type_op_of(node)) && gu::type_op_of(node) != Ntype_op::IO) {
          transparent.push_back(node);
        }
      }
      parts = components(transparent);
    }
  }

  // Build: components consumers-first, built producers-first, so every
  // operand's class exists before its consumer is built.
  std::vector<hhds::Node_class> producers_first;
  for (const auto& part : parts) {
    if (part.cyclic) {
      for (const auto& node : part.nodes) {
        cyclic_.insert(node.get_class_index());
        cyclic_nodes_.push_back(node);
      }
    }
  }
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (!it->cyclic) {
      producers_first.insert(producers_first.end(), it->nodes.begin(), it->nodes.end());
    }
  }
  build_all(producers_first);
  resolve_false_cycles();
  stats_.opaque = cyclic_nodes_.size();
  stats_.skeleton += cyclic_nodes_.size();
  stats_.classes = eg_.num_classes();

  // Extract: cost-free. The representative of a node class is its first member
  // (the original node); a constant class is its constant; a forward is the
  // operand class. realize() resolves all three.
  elaborate();
  sweep_dead();

  stats_.nodes_out = 0;
  for ([[maybe_unused]] auto node : g_.body().nodes()) {
    ++stats_.nodes_out;
  }
  return stats_;
}

}  // namespace

Stats optimize(const std::shared_ptr<hhds::Graph>& graph, const Cycle_cutter& cut_cycles) {
  if (!graph) {
    return {};
  }
  Engine engine(*graph, cut_cycles);
  return engine.run();
}

}  // namespace livehd::aegraph
