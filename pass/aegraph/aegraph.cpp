//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "aegraph.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "aegraph_profile.hpp"
#include "diag.hpp"
#include "cprop_muxctx.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "runtime/analyses.hpp"
#include "runtime/cost.hpp"
#include "runtime/match.hpp"

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

uint64_t Egraph::hash_of(Ntype_op op, uint32_t attr, std::span<const Operand> operands) {
  uint64_t h = mix(0x1234567ULL, static_cast<uint64_t>(op));
  h          = mix(h, attr);
  for (const auto& operand : operands) {
    h = mix(h, (static_cast<uint64_t>(operand.bank) << 32) | operand.cls);
  }
  return h;
}

Egraph::Egraph()  = default;
Egraph::~Egraph() = default;

rt::Analysis_cache& Egraph::analysis_cache() const {
  if (!analysis_) {
    analysis_ = std::make_unique<rt::Analysis_cache>();
  }
  return *analysis_;
}

Class_id Egraph::add_leaf() {
  classes_.emplace_back();
  return static_cast<Class_id>(classes_.size() - 1);
}

Class_id Egraph::add_literal(const Dlop& value) {
  Eclass c;
  c.kind  = Kind::literal;
  c.value = static_cast<uint32_t>(values_.size());
  values_.push_back(value);
  classes_.push_back(c);
  return static_cast<Class_id>(classes_.size() - 1);
}

namespace {
// A Boolean and an Integer of the same numeric value are one constant to the
// graph (a Boolean is the hardware u1, exactly the Integer 0/1), so the pool
// keys on the value and not on that tag. The first spelling seen stays the
// class's value: the forward walk fixes the order, so the choice is
// deterministic. String, Bitwidth and Nil keep their own tag.
spool_ptr<Dlop> bool_as_integer(const Dlop& v) { return Dlop::create_integer(v.is_known_true() ? 1 : 0); }

bool same_constant(const Dlop& a, const Dlop& b) {
  if (a.is_bool() == b.is_bool()) {
    return a.same_repr(b);
  }
  if (a.is_bool() && b.is_integer()) {
    return bool_as_integer(a)->same_repr(b);
  }
  if (b.is_bool() && a.is_integer()) {
    return bool_as_integer(b)->same_repr(a);
  }
  return false;
}
}  // namespace

Class_id Egraph::add_const(const Dlop& value) {
  I(!value.has_unknowns());
  auto& bucket = consts_[value.is_bool() ? bool_as_integer(value)->hash() : value.hash()];
  for (const auto id : bucket) {
    if (same_constant(values_[classes_[id].value], value)) {
      return id;
    }
  }
  Eclass c;
  c.kind  = Kind::konst;
  c.value = static_cast<uint32_t>(values_.size());
  values_.push_back(value);
  classes_.push_back(c);
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

uint32_t Egraph::attr_id(const Enode_attrs& attrs) {
  if (attrs == Enode_attrs{}) {
    return 0;
  }
  const std::array<uint32_t, 5> key{attrs.has_color ? 1U : 0U,
                                    static_cast<uint32_t>(attrs.color),
                                    attrs.has_proven ? 1U : 0U,
                                    attrs.proven,
                                    attrs.policy};
  const auto [it, inserted] = attr_ids_.try_emplace(key, static_cast<uint32_t>(attrs_.size()));
  if (inserted) {
    attrs_.push_back(attrs);
  }
  return it->second;
}

void Egraph::grow_table() {
  const size_t size = table_.empty() ? 16 : table_.size() * 2;
  table_.assign(size, kNoEnode);
  const size_t mask = size - 1;
  for (Enode_id id = 0; id < enodes_.size(); ++id) {
    const auto& n = enodes_[id];
    size_t      i = hash_of(n.op, n.attr, {pool_.data() + n.first_operand, n.n_operands}) & mask;
    while (table_[i] != kNoEnode) {
      i = (i + 1) & mask;
    }
    table_[i] = id;
  }
}

Class_id Egraph::intern(Ntype_op op, const Enode_attrs& attrs, std::span<const Operand> operands, bool& created, Class_id* raw) {
  const uint32_t attr = attr_id(attrs);
  const uint64_t h    = hash_of(op, attr, operands);
  if (!table_.empty()) {
    // The 64-bit hash only picks the slot: compare the whole key before
    // merging, because a false hash-cons merge is a silent miscompile.
    const size_t mask = table_.size() - 1;
    for (size_t i = h & mask; table_[i] != kNoEnode; i = (i + 1) & mask) {
      const Enode& member = enodes_[table_[i]];
      if (member.op != op || member.attr != attr || member.n_operands != operands.size()) {
        continue;
      }
      if (std::equal(operands.begin(), operands.end(), pool_.begin() + member.first_operand)) {
        created = false;
        if (raw != nullptr) {
          *raw = member.cls;
        }
        return resolve(member.cls);  // a rewrite may have replaced or extended it since
      }
    }
  }
  created = true;

  Enode node;
  node.op            = op;
  node.attr          = attr;
  node.first_operand = static_cast<uint32_t>(pool_.size());
  node.n_operands    = static_cast<uint32_t>(operands.size());
  pool_.insert(pool_.end(), operands.begin(), operands.end());

  Eclass cls;
  cls.kind = Kind::node;
  classes_.push_back(cls);
  node.cls                    = static_cast<Class_id>(classes_.size() - 1);
  const auto id               = static_cast<Enode_id>(enodes_.size());
  classes_[node.cls].first    = id;
  if (raw != nullptr) {
    *raw = node.cls;
  }
  enodes_.push_back(node);
  if (enodes_.size() * 4 > table_.size() * 3) {
    grow_table();  // rehashes every e-node, the new one included
  } else {
    const size_t mask = table_.size() - 1;
    size_t       i    = h & mask;
    while (table_[i] != kNoEnode) {
      i = (i + 1) & mask;
    }
    table_[i] = id;
  }
  return node.cls;
}

size_t Egraph::bytes() const {
  // Bytes in use (sizes, not capacities: a reserve or a doubling that was never
  // touched is not resident). The tables only grow, so the value after the
  // build is the peak.
  size_t n = classes_.size() * sizeof(Eclass) + enodes_.size() * sizeof(Enode) + pool_.size() * sizeof(Operand);
  n += values_.size() * sizeof(Dlop) + table_.size() * sizeof(Enode_id) + attrs_.size() * sizeof(Enode_attrs);
  n += consts_.size() * (sizeof(decltype(consts_)::value_type) + 1);
  n += policies_.size() * sizeof(std::string) + policy_ids_.size() * (sizeof(decltype(policy_ids_)::value_type) + 1);
  return n;
}

size_t Egraph::capacity_bytes() const {
  size_t n = classes_.capacity() * sizeof(Eclass) + enodes_.capacity() * sizeof(Enode) + pool_.capacity() * sizeof(Operand);
  n += values_.capacity() * sizeof(Dlop) + table_.capacity() * sizeof(Enode_id) + attrs_.capacity() * sizeof(Enode_attrs);
  n += consts_.capacity() * (sizeof(decltype(consts_)::value_type) + 1);
  n += policies_.capacity() * sizeof(std::string) + policy_ids_.capacity() * (sizeof(decltype(policy_ids_)::value_type) + 1);
  return n;
}

void Egraph::set_value(Class_id cls, Dlop value) {
  classes_[cls].value = static_cast<uint32_t>(values_.size());
  values_.push_back(std::move(value));
}

Class_id Egraph::add_union(Class_id a, Class_id b) {
  Eclass u;
  u.kind    = Kind::uni;
  u.alt_a   = a;
  u.alt_b   = b;
  u.value   = classes_[a].value;  // shares a's analysis value
  u.members = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, uint32_t{classes_[a].members} + classes_[b].members));
  classes_.push_back(std::move(u));
  return static_cast<Class_id>(classes_.size() - 1);
}

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

}  // namespace

std::optional<Dlop> eval_op(Ntype_op op, std::span<const Eval_arg> args) {
  // fold_node wants the operands in ascending sink-port order and reads a class kind only to spot a
  // whole '?' literal, so mirror the arguments into a scratch e-graph.
  std::vector<size_t> order(args.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::ranges::stable_sort(order, [&](size_t a, size_t b) { return args[a].port < args[b].port; });
  Egraph                    scratch;
  std::vector<Eval_operand> ops;
  ops.reserve(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    const auto cls = args[i].value.has_unknowns() ? scratch.add_literal(args[i].value) : scratch.add_leaf();
    I(cls == i);
  }
  for (const auto i : order) {
    ops.push_back({args[i].port, args[i].bank, static_cast<Class_id>(i), &args[i].value});
  }
  const Fold f = fold_node(op, ops, scratch);
  if (f.kind == Fold::Kind::value) {
    return f.value;
  }
  if (f.kind == Fold::Kind::forward) {
    return args[f.fwd].value;
  }
  return std::nullopt;
}

namespace {

// ---------------------------------------------------------------------------
// check=unions|full: value evidence for the rewrites
// ---------------------------------------------------------------------------

// Evaluates a cone of e-graph classes on random assignments with the SAME Dlop
// semantics the constant analysis folds with (fold_node). A leaf draws a value
// from a small deterministic set (0, 1, -1 and small magnitudes: shift amounts
// stay cheap); a '?' literal has no value, so a cone that reaches one is skipped,
// like a vector whose result carries '?' bits.
class Checker {
public:
  Checker(const Egraph& eg, const std::vector<Class_id>* best) : eg_(eg), best_(best) {}

  // The cone below `roots`, ascending by class id (operands are always older than
  // their reader, so that is a topological order). false when it holds more than
  // `fuel` classes.
  bool gather(std::span<const Class_id> roots, size_t fuel) {
    cone_.clear();
    absl::flat_hash_set<Class_id> seen;
    std::vector<Class_id>         stack(roots.begin(), roots.end());
    while (!stack.empty()) {
      const auto c = stack.back();
      stack.pop_back();
      if (!seen.insert(c).second) {
        continue;
      }
      if (seen.size() > fuel) {
        return false;
      }
      cone_.push_back(c);
      const auto& cls = eg_.cls(c);
      if (cls.kind == Egraph::Kind::uni) {
        stack.push_back(member_of(c));
      } else if (cls.kind == Egraph::Kind::node) {
        for (const auto& o : eg_.operands(eg_.enode(cls.first))) {
          stack.push_back(o.cls);
        }
      }
    }
    std::ranges::sort(cone_);
    return true;
  }

  void evaluate(uint64_t vec) {
    vals_.clear();
    for (const auto c : cone_) {
      const auto& cls = eg_.cls(c);
      switch (cls.kind) {
        case Egraph::Kind::konst  : vals_[c] = *eg_.value_of(c); break;
        case Egraph::Kind::literal: vals_[c] = std::nullopt; break;
        case Egraph::Kind::leaf   : vals_[c] = leaf_value(eg_, vec, c); break;
        case Egraph::Kind::uni    : {
          auto v   = vals_[member_of(c)];
          vals_[c] = std::move(v);
          break;
        }
        case Egraph::Kind::node   : {
          const auto& n = eg_.enode(cls.first);
          vals_[c]      = eval_node(n.op, eg_.operands(n));
          break;
        }
      }
    }
  }

  // The value of a gathered class under the last assignment (null: none).
  [[nodiscard]] const Dlop* value(Class_id c) const {
    const auto it = vals_.find(c);
    return it == vals_.end() || !it->second ? nullptr : &*it->second;
  }

  // `op` applied to the (gathered) operand classes.
  [[nodiscard]] std::optional<Dlop> eval_node(Ntype_op op, std::span<const Operand> ops) const {
    absl::InlinedVector<Dlop, 8> vals;
    for (const auto& o : ops) {
      const auto* v = value(o.cls);
      if (v == nullptr) {
        return std::nullopt;
      }
      vals.push_back(*v);
    }
    absl::InlinedVector<Eval_operand, 8> ev;
    for (size_t i = 0; i < ops.size(); ++i) {
      const auto port = Ntype::is_banked_sink_op(op) ? static_cast<hhds::Port_id>(i) : static_cast<hhds::Port_id>(ops[i].bank);
      ev.push_back(Eval_operand{port, ops[i].bank, ops[i].cls, &vals[i]});
    }
    if (!sane(op, ev)) {
      return std::nullopt;
    }
    const auto fold = fold_node(op, ev, eg_);
    if (fold.kind == Fold::Kind::value && !fold.value.has_unknowns()) {
      return fold.value;
    }
    if (fold.kind == Fold::Kind::forward) {
      for (size_t i = 0; i < ev.size(); ++i) {
        if (ev[i].cls == fold.fwd) {
          return vals[i];
        }
      }
    }
    return std::nullopt;
  }

  // Equal as numbers; nullopt when either side is not a plain known value.
  [[nodiscard]] static std::optional<bool> same(const Dlop* a, const Dlop* b) {
    if (a == nullptr || b == nullptr || a->has_unknowns() || b->has_unknowns() || !a->is_numeric() || !b->is_numeric()) {
      return std::nullopt;
    }
    return a->eq_op(*b)->is_known_true();
  }

private:
  [[nodiscard]] Class_id member_of(Class_id u) const {
    if (best_ != nullptr) {
      return (*best_)[u];
    }
    while (eg_.cls(u).kind == Egraph::Kind::uni) {
      u = eg_.cls(u).alt_a;
    }
    return u;
  }

  [[nodiscard]] static Dlop leaf_value(const Egraph& eg, uint64_t vec, Class_id c) {
    uint64_t h = vec * 0x9e3779b97f4a7c15ULL + c * 0xbf58476d1ce4e5b9ULL + 0x1234567ULL;
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    static constexpr int64_t kSpecial[] = {0, 1, -1, 2, 3, -2, 7, 8, 15, 16, 255, -256};
    int64_t                  v          = 0;
    switch (h & 3) {
      case 0 : v = kSpecial[(h >> 8) % std::size(kSpecial)]; break;
      case 1 : v = static_cast<int64_t>((h >> 8) % 64) - 32; break;
      case 2 : v = static_cast<int64_t>((h >> 8) % 8192) - 4096; break;
      default: v = static_cast<int64_t>((h >> 8) & 1); break;  // a mix of booleans keeps the 0/1 rules honest
    }
    // A leaf with a declared width (a graph input, a register output) only takes values of that width.
    if (const auto it = eg.analysis_cache().seeds.find(c); it != eg.analysis_cache().seeds.end()) {
      const rt::Wide lo = it->second.lo, hi = it->second.hi;
      if (v < lo || v > hi) {
        const rt::Wide span = hi - lo + 1;
        rt::Wide       m    = (static_cast<rt::Wide>(v) - lo) % span;
        if (m < 0) {
          m += span;
        }
        v = static_cast<int64_t>(lo + m);
      }
    }
    Dlop d;
    d = Dlop::create_integer(v);
    return d;
  }

  // Refuse a vector whose operands would make a shift or a window absurd (a
  // product can feed a shift amount): the evaluation must stay cheap.
  [[nodiscard]] static bool sane(Ntype_op op, std::span<const Eval_operand> ev) {
    constexpr int64_t kBig = int64_t{1} << 16;
    for (const auto& o : ev) {
      if (o.val->get_payload_bits() > 1 << 16) {
        return false;
      }
      const bool amount = ((op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::Sext || op == Ntype_op::Rxor
                            || op == Ntype_op::Popcount)
                           && o.port == 1)
                          || ((op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) && (o.port == 2 || o.port == 3))
                          || (op == Ntype_op::Concat && o.port % 2 == 1);
      if (amount
          && (o.val->has_unknowns() || !o.val->is_just_i64() || o.val->to_just_i64() > kBig || o.val->to_just_i64() < -kBig)) {
        return false;
      }
    }
    return true;
  }

  const Egraph&                                          eg_;
  const std::vector<Class_id>*                           best_;
  std::vector<Class_id>                                  cone_;
  absl::flat_hash_map<Class_id, std::optional<Dlop>>     vals_;
};

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

// The commutative banks the flatten rule merges.
[[nodiscard]] bool flat_chain_op(Ntype_op op) {
  return op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor;
}

class Engine final : public rt::Builder {
public:
  Engine(hhds::Graph& graph, const Cycle_cutter& cut_cycles, const Options& options, std::string name);

  Stats run();

  // ---- rt::Builder: what the rules may do ----
  Class_id make(Ntype_op op, std::span<const Operand> ops) override;
  Class_id konst(const Dlop& value) override { return eg_.add_const(value); }
  [[nodiscard]] bool single_use(Class_id cls) const override { return cls < readers_.size() && readers_[cls] == 1; }
  [[nodiscard]] bool chain_inner(Class_id cls) const override { return cls < chain_inner_.size() && chain_inner_[cls] != 0; }
  [[nodiscard]] bool guard_scope() const override { return opt_.guard_scope && opt_.unstamped_ok && !opt_.preflight; }

private:
  // How a pure node entered the e-graph.
  enum class How : uint8_t { created, merged, constant, forward, rewritten };
  struct Built {
    Class_id cls = kNoClass;
    How      how = How::created;
  };
  struct Member {
    hhds::Node_class      node;
    Class_id              cls;
    How                   how;
    Ntype_op              spec_op = Ntype_op::Invalid;  // check=full: the node as the graph had it,
    std::vector<Operand>  spec_ops;                     // over the final operand classes
  };
  enum class Replaced : uint8_t { done, dead, declined };

  void             collect_nodes();
  struct Component {
    std::vector<hhds::Node_class> nodes;
    bool                          cyclic = false;
  };
  [[nodiscard]] std::vector<Component> components(const std::vector<hhds::Node_class>& subset) const;
  [[nodiscard]] const Dlop*            known_value(const hhds::Pin_class& pin) const;
  // The declared physical width of a graph input or a register output: (bits, unsigned).
  [[nodiscard]] std::optional<std::pair<int, bool>> declared_bits(const hhds::Pin_class& pin);
  // The one data port a Mux with a known selector reads, if any.
  [[nodiscard]] std::optional<hhds::Port_id> live_mux_port(const hhds::Node_class& mux) const;
  void                                 build_all(const std::vector<hhds::Node_class>& producers_first);
  void                                 resolve_false_cycles();
  void                                 precount_twins(const std::vector<hhds::Node_class>& producers_first);
  void                                 pin_latch_holds();
  void                                 mark_dead(const std::vector<hhds::Node_class>& producers_first);
  [[nodiscard]] size_t                 live_readers(const hhds::Node_class& node) const;
  [[nodiscard]] bool is_pure_candidate(const hhds::Node_class& node) const;
  Class_id         class_of(const hhds::Pin_class& pin);
  void             ensure(Class_id cls);
  std::optional<Built> build(const hhds::Node_class& node);
  // The rewrite rules of one new e-node (depth = rule-built nesting, 0 for an
  // original node). Returns the class that stands for the node afterwards.
  Class_id         rewrite(Class_id cls, uint32_t depth);
  void             check_pair(Class_id a, Class_id b, rt::Rule_id rule);
  void             check_full();
  void             compute_best();
  // The expression cost of a class's cheapest realization (compute_best), and the width its price is taken at.
  uint32_t         class_cost(Class_id root);
  [[nodiscard]] uint32_t readers_under_selection(Class_id cls) const;
  [[nodiscard]] int64_t price_width(Class_id cls) const;
  hhds::Pin_class  realize(Class_id cls);
  // preflight: the width stamp a new cell of `cls` gets (unsigned?, bits), from the range analysis.
  [[nodiscard]] std::optional<std::pair<bool, int>> stamp_for(Class_id cls) const;
  // preflight: true when every cell elaborate() would create can be stamped.
  [[nodiscard]] bool preflight_ok();
  [[nodiscard]] bool adopt(const Member& member, Class_id target);
  void             create_node(Class_id cls);
  void             rewire(const hhds::Node_class& node, Ntype_op op, std::span<const Operand> ops);
  void             elaborate();
  Replaced         replace(const Member& member, const hhds::Pin_class& target);
  void             sweep_dead();
  [[nodiscard]] std::string describe(Class_id cls) const;

  hhds::Graph&         g_;
  const Cycle_cutter&  cut_cycles_;
  Options              opt_;
  std::string          name_;
  Egraph               eg_;
  Stats        stats_;

  std::vector<hhds::Node_class>                      nodes_;
  std::vector<hhds::Node_class>                      cyclic_nodes_;  // members of the remaining cyclic components
  absl::flat_hash_set<hhds::Class_index>             cyclic_;        // the same, by node
  absl::flat_hash_map<hhds::Class_index, Class_id>   pin_class_;
  std::vector<hhds::Pin_class>                       rep_pin_;  // class -> the pin that realizes it (invalid until known)
  std::vector<Member>                                members_;

  // ---- rewrite rules ----
  std::vector<uint8_t>   enabled_;       // per Rule_id
  bool                   rules_active_ = false;
  rt::Rule_id            current_rule_ = rt::kNoRule;  // the rule whose body is running
  Enode_attrs            cur_attrs_;     // attrs the rule's new e-nodes inherit
  uint32_t               depth_        = 0;
  uint64_t               rewrites_     = 0;
  bool                   cutoff_       = false;  // max_rewrites reached
  uint64_t               work_used_    = 0;
  uint64_t               work_budget_  = 0;
  bool                   budget_hit_   = false;
  std::vector<uint32_t>  readers_;       // per class: readers in the graph being built
  std::vector<uint32_t>  rule_readers_;  // per class: the part of readers_ that are rule-built e-nodes (an alt may lose)
  std::vector<uint32_t>  sel_readers_;   // compute_best: readers under the current selection (empty = readers_)
  absl::flat_hash_set<hhds::Class_index>           dead_;          // mark_dead: pure nodes nothing live reads
  absl::flat_hash_set<hhds::Class_index>           latch_holds_;   // pin_latch_holds: Mux nodes that are a latch's own hold mux
  // precount_twins: node -> readers of its whole twin group (0 for a later twin)
  absl::flat_hash_map<hhds::Class_index, uint32_t> twin_readers_;
  std::vector<uint8_t>   chain_inner_;   // per class: its one reader is a same-op, same-attrs cell (absorbs it)
  std::vector<Operand>   spec_ops_;      // check=full: the operands build() last saw (final classes)
  std::vector<Class_id>  best_;          // per class: the non-union class that realizes it (once unions exist)
  std::vector<uint32_t>  cost_;          // per class: the cost of its cheapest realization (compute_best, lazy)
  const cost::Profile*   profile_ = nullptr;  // the extraction cost profile (opt_.cost)
  static constexpr uint32_t kCostUnset = UINT32_MAX;
  absl::flat_hash_map<std::string, std::pair<int, bool>> input_decls_;  // declared input widths (declared_bits)
  Class_id               owner_cls_ = kNoClass;  // preflight: the class being realized for a member (its stamp is the member's)
  hhds::Pin_class        owner_pin_;             // preflight: that member's output pin
  bool                   has_best_     = false;
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

void Engine::ensure(Class_id cls) {
  if (rep_pin_.size() <= cls) {
    rep_pin_.resize(cls + 1);
    readers_.resize(cls + 1, 0);
    rule_readers_.resize(cls + 1, 0);
    chain_inner_.resize(cls + 1, 0);
  }
}

std::optional<std::pair<int, bool>> Engine::declared_bits(const hhds::Pin_class& pin) {
  if (gu::is_graph_input_pin(pin)) {
    // A declared port's width and sign live on the IO declaration, not on the pin (every front end stamps
    // pin_signed on every IO pin by convention).
    if (input_decls_.empty()) {
      if (const auto gio = g_.get_io(); gio != nullptr) {
        for (const auto& d : gio->get_input_pin_decls()) {
          input_decls_.emplace(std::string{d.name}, std::pair<int, bool>{static_cast<int>(d.bits), d.unsign});
        }
      }
      input_decls_.emplace(std::string{}, std::pair<int, bool>{0, true});  // never empty again
    }
    const auto it = input_decls_.find(std::string{gu::pin_name_of(pin)});
    if (it != input_decls_.end() && it->second.first > 0) {
      return it->second;
    }
    return std::nullopt;
  }
  const auto master = pin.get_master_node();
  if (!master.is_invalid()) {
    const auto op = gu::type_op_of(master);
    if ((op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) && gu::bits_of(pin) > 0) {
      return std::pair<int, bool>{gu::bits_of(pin), gu::is_unsign(pin)};
    }
  }
  return std::nullopt;
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
    if (!gu::is_graph_input_pin(pin) && !pin.get_master_node().is_invalid()
        && Ntype::is_loop_last(gu::type_op_of(pin.get_master_node()))) {
      eg_.mark_state(cls);
    }
    if (const auto declared = declared_bits(pin); declared) {
      rt::seed_leaf(eg_, cls, declared->first, declared->second);
    }
  }
  ensure(cls);
  if (rep_pin_[cls].is_invalid()) {
    rep_pin_[cls] = pin;
  }
  pin_class_.emplace(key, cls);
  return cls;
}

// The Eval_operand view of a normalized operand list. A positional operand's
// bank is its port; a banked operand has no port role, so its index stands in.
namespace {
void eval_view(Ntype_op op, std::span<const Operand> operands, const Egraph& eg, absl::InlinedVector<Eval_operand, 8>& eval) {
  const bool banked = Ntype::is_banked_sink_op(op);
  for (size_t i = 0; i < operands.size(); ++i) {
    eval.push_back(Eval_operand{banked ? static_cast<hhds::Port_id>(i) : static_cast<hhds::Port_id>(operands[i].bank),
                                operands[i].bank,
                                operands[i].cls,
                                eg.value_of(operands[i].cls)});
  }
}
}  // namespace

// A rule-built e-node: fold, normalize, hash-cons and (up to rewrite_depth) run
// the rules of its own root op. The attrs are the rewritten node's.
Class_id Engine::make(Ntype_op op, std::span<const Operand> in) {
  I(op != Ntype_op::Hotmux, "rules do not build Hotmux");
  absl::InlinedVector<Operand, 8> operands(in.begin(), in.end());
  std::ranges::sort(operands);

  absl::InlinedVector<Eval_operand, 8> eval;
  eval_view(op, operands, eg_, eval);
  const Fold fold = fold_node(op, eval, eg_);
  if (fold.kind == Fold::Kind::forward) {
    return fold.fwd;
  }
  if (fold.kind == Fold::Kind::value && !fold.value.has_unknowns()) {
    return eg_.add_const(fold.value);
  }

  bool       created = false;
  const auto cls     = eg_.intern(op, cur_attrs_, operands, created);
  ensure(cls);
  if (!created) {
    return cls;
  }
  eg_.set_rule(cls, current_rule_);
  if (fold.kind == Fold::Kind::value) {
    eg_.set_value(cls, fold.value);
  }
  for (const auto& operand : operands) {
    ensure(operand.cls);  // a constant the rule just minted has no slot yet
    ++readers_[operand.cls];
    ++rule_readers_[operand.cls];
  }
  return rewrite(cls, depth_ + 1);
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

  if (opt_.check == Options::Check::full) {
    spec_ops_.assign(operands.begin(), operands.end());
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

  size_t readers = 0;
  if (const auto it = twin_readers_.find(node.get_class_index()); it != twin_readers_.end()) {
    readers = it->second;  // the readers of every structural twin, counted before the walk
  } else {
    readers = live_readers(node);
  }

  bool       created = false;
  Class_id   raw     = kNoClass;
  const auto cls     = eg_.intern(op, attrs, operands, created, &raw);
  ensure(cls);
  ensure(raw);
  readers_[raw] += static_cast<uint32_t>(readers);
  if (!created) {
    // A hit on an e-node only a rule built: this original node is its first real
    // realization, so it becomes the class's pin (no new node is created).
    if (rep_pin_[raw].is_invalid() && eg_.cls(raw).kind == Egraph::Kind::node) {
      rep_pin_[raw] = out;
    }
    return Built{cls, How::merged};
  }
  rep_pin_[raw] = out;
  if (readers == 1 && flat_chain_op(op)) {
    for (const auto& edge : node.out_edges()) {
      const auto reader = edge.sink.get_master_node();
      if (!reader.is_invalid() && dead_.contains(reader.get_class_index())) {
        continue;
      }
      if (reader.is_invalid() || gu::type_op_of(reader) != op || !is_pure_candidate(reader)) {
        break;
      }
      const bool same = gu::has_color(reader) == attrs.has_color && (!attrs.has_color || gu::color_of(reader) == attrs.color)
                        && gu::has_proven(reader) == attrs.has_proven
                        && (!attrs.has_proven || gu::proven_of(reader) == attrs.proven)
                        && reader.attr(livehd::attrs::synth_policy).has() == (attrs.policy != 0);
      chain_inner_[raw] = same ? 1 : 0;
    }
  }
  if (fold.kind == Fold::Kind::value) {
    eg_.set_value(cls, fold.value);  // a three-valued value; never materialized (P7)
  }

  Class_id final_cls = cls;
  // A latch's own hold mux is a root (the latch contract): rules never rewrite it.
  if (op != Ntype_op::Hotmux && !latch_holds_.contains(node.get_class_index())) {
    cur_attrs_            = attrs;
    cur_attrs_.has_proven = false;
    cur_attrs_.proven     = 0;
    current_rule_         = rt::kNoRule;
    depth_                = 0;
    final_cls             = rewrite(cls, 0);
  }
  if (final_cls == cls) {
    return Built{cls, How::created};
  }
  return Built{final_cls, eg_.cls(final_cls).kind == Egraph::Kind::konst ? How::constant : How::rewritten};
}

// ---------------------------------------------------------------------------
// Rewriting
// ---------------------------------------------------------------------------

namespace {
[[nodiscard]] bool name_listed(std::string_view list, std::string_view name, std::string_view group) {
  size_t i = 0;
  while (i <= list.size()) {
    const auto j = std::min(list.find('+', i), list.size());
    const auto t = list.substr(i, j - i);
    if (!t.empty() && (t == name || (t.starts_with("group:") && t.substr(6) == group))) {
      return true;
    }
    i = j + 1;
  }
  return false;
}
}  // namespace

std::string Engine::describe(Class_id cls) const {
  const auto& c = eg_.cls(cls);
  switch (c.kind) {
    case Egraph::Kind::leaf   : return std::format("c{}:leaf", cls);
    case Egraph::Kind::literal: return std::format("c{}:literal", cls);
    case Egraph::Kind::konst  : return std::format("c{}:const({})", cls, eg_.value_of(cls)->to_pyrope());
    case Egraph::Kind::uni    : return std::format("c{}:union(c{},c{})", cls, c.alt_a, c.alt_b);
    case Egraph::Kind::node   : {
      const auto& n = eg_.enode(c.first);
      std::string s = std::format("c{}:{}", cls, Ntype::get_name(n.op));
      if (n.rule != rt::kNoRule) {
        s += std::format("[{}]", rt::Registry::instance().rule(n.rule).name);
      }
      s += "(";
      for (size_t i = 0; i < n.n_operands; ++i) {
        const auto& o = eg_.operands(n)[i];
        s += std::format("{}{}:c{}", i == 0 ? "" : ",", o.bank, o.cls);
      }
      return s + ")";
    }
  }
  return {};
}

Class_id Engine::rewrite(Class_id cls, uint32_t depth) {
  if (!rules_active_ || cutoff_ || eg_.cls(cls).kind != Egraph::Kind::node) {
    return cls;
  }
  // Copies: a rule that builds e-nodes grows the pool the originals live in.
  const Enode                           node = eg_.enode(eg_.cls(cls).first);
  const absl::InlinedVector<Operand, 8> ops(eg_.operands(node).begin(), eg_.operands(node).end());
  const Enode_attrs                     attrs = eg_.attrs_of(node);
  auto&                                 reg   = rt::Registry::instance();
  const auto                            ids   = reg.for_op(node.op);
  if (ids.empty()) {
    return cls;
  }
  if (depth >= opt_.rewrite_depth) {
    ++stats_.depth_limited;
    return cls;
  }

  std::optional<livehd::aegraph_profile::Timer> timer;  // rule time is nested in build; only the outermost call times
  if (depth == 0) {
    timer.emplace(livehd::aegraph_profile::rules);
  }
  const auto saved_rule  = current_rule_;
  const auto saved_attrs = cur_attrs_;
  const auto saved_depth = depth_;
  Class_id   current     = cls;
  uint32_t   alts        = 0;
  const rt::Root root{node.op, attrs, std::span<const Operand>(ops.data(), ops.size()), cls};

  for (const auto id : ids) {
    if (!enabled_[id]) {
      continue;
    }
    const auto& info = reg.rule(id);
    if (info.tag == rt::Tag::choice && !opt_.choices) {
      continue;
    }
    if (work_used_ + ops.size() + 1 > work_budget_) {
      if (!budget_hit_) {
        budget_hit_ = true;
        ++stats_.budget_hit;
      }
      break;
    }
    work_used_ += ops.size() + 1;
    auto& counters = stats_.rule_counts[id];
    ++counters.tried;

    rt::Results results;
    rt::Ctx     ctx(eg_, *this, size_t{opt_.eclass_limit} * 16);
    current_rule_         = id;
    cur_attrs_            = attrs;
    cur_attrs_.has_proven = false;
    cur_attrs_.proven     = 0;
    depth_                = depth;
    info.fn(ctx, root, results);
    if (results.empty()) {
      continue;
    }
    ++counters.matched;

    bool subsumed = false;
    for (const auto& item : results.items()) {
      if (item.cls == kNoClass || item.cls == cls || item.cls == current) {
        continue;  // the rule rebuilt what is already there
      }
      if (item.tag == rt::Tag::choice) {
        continue;  // choices=off: nothing assumes a '?' value
      }
      if (opt_.max_rewrites != 0 && rewrites_ >= opt_.max_rewrites) {
        ++counters.limit_rejected;
        cutoff_ = true;
        continue;
      }
      if (item.tag == rt::Tag::simplify) {
        if (opt_.check != Options::Check::off) {
          check_pair(cls, item.cls, id);
        }
        ++rewrites_;
        ++counters.applied;
        ++counters.subsumed;
        ++stats_.rules_fired;
        current  = item.cls;
        subsumed = true;
      } else {
        // alt: a union with what the class is so far, within the limits.
        const bool duplicate = eg_.for_each_member(item.cls, 32, [&](Class_id m) {
          return eg_.for_each_member(current, 32, [&](Class_id c) { return c == m; });
        });
        if (duplicate) {
          continue;
        }
        if (alts >= opt_.matches_limit || uint32_t{eg_.cls(current).members} + eg_.cls(item.cls).members > opt_.eclass_limit) {
          ++counters.limit_rejected;
          continue;
        }
        if (opt_.check != Options::Check::off) {
          check_pair(current, item.cls, id);
        }
        ++rewrites_;
        ++counters.applied;
        ++stats_.rules_fired;
        ++stats_.unions;
        ++alts;
        current = eg_.add_union(current, item.cls);
        ensure(current);
      }
      if (opt_.max_rewrites != 0 && rewrites_ == opt_.max_rewrites && (opt_.bisect_graph.empty() || opt_.bisect_graph == name_)) {
        std::fprintf(stderr,
                     "aegraph-bisect graph=%s rewrite=%llu rule=%s enode=c%u op=%.*s\n",
                     name_.c_str(),
                     static_cast<unsigned long long>(rewrites_),
                     info.name,
                     cls,
                     static_cast<int>(Ntype::get_name(node.op).size()),
                     Ntype::get_name(node.op).data());
      }
      if (subsumed) {
        break;
      }
    }
    if (subsumed) {
      break;
    }
  }
  current_rule_ = saved_rule;
  cur_attrs_    = saved_attrs;
  depth_        = saved_depth;
  if (current != cls) {
    ensure(current);
    readers_[current] += readers_[cls];
    rule_readers_[current] += rule_readers_[cls];
    eg_.set_alias(cls, current);
  }
  return current;
}

// check=unions (and check=full for each rewrite): the two classes must agree on
// 8 random assignments.
void Engine::check_pair(Class_id a, Class_id b, rt::Rule_id rule) {
  constexpr size_t kConeFuel = 4096;
  Checker          ck(eg_, nullptr);
  const Class_id   roots[2] = {a, b};
  if (!ck.gather(roots, kConeFuel)) {
    ++stats_.check_skipped;
    return;
  }
  for (uint64_t vec = 1; vec <= 8; ++vec) {
    ck.evaluate(vec);
    const auto same = Checker::same(ck.value(a), ck.value(b));
    if (!same) {
      ++stats_.check_skipped;
      continue;
    }
    ++stats_.checks;
    if (!*same) {
      const auto& info = rt::Registry::instance().rule(rule);
      throw std::logic_error(std::format("aegraph check: rule '{}' ({}:{}) is not value-preserving in graph '{}' on vector {}: "
                                         "{} = {} but {} = {}",
                                         info.name,
                                         info.file,
                                         info.line,
                                         name_,
                                         vec,
                                         describe(a),
                                         ck.value(a)->to_pyrope(),
                                         describe(b),
                                         ck.value(b)->to_pyrope()));
    }
  }
}

// check=full: every original node, applied to its final operand classes, must
// equal the class the extraction keeps for it (64 assignments, whole cones).
void Engine::check_full() {
  std::vector<Class_id> roots;
  for (const auto& m : members_) {
    roots.push_back(m.cls);
    for (const auto& o : m.spec_ops) {
      roots.push_back(o.cls);
    }
  }
  Checker ck(eg_, has_best_ ? &best_ : nullptr);
  if (!ck.gather(roots, std::numeric_limits<size_t>::max())) {
    return;
  }
  for (uint64_t vec = 1; vec <= 64; ++vec) {
    ck.evaluate(vec);
    for (const auto& m : members_) {
      if (m.spec_op == Ntype_op::Invalid) {
        continue;
      }
      const auto spec = ck.eval_node(m.spec_op, m.spec_ops);
      const auto same = Checker::same(spec ? &*spec : nullptr, ck.value(m.cls));
      if (!same) {
        ++stats_.check_skipped;
        continue;
      }
      ++stats_.checks;
      if (!*same) {
        const auto* n    = eg_.cls(m.cls).kind == Egraph::Kind::node ? &eg_.enode(eg_.cls(m.cls).first) : nullptr;
        const char* rule = n != nullptr && n->rule != rt::kNoRule ? rt::Registry::instance().rule(n->rule).name : "(none)";
        throw std::logic_error(std::format("aegraph check=full: graph '{}' node {} ({}) is {} but its class {} is {} on vector {} "
                                           "(creating rule {})",
                                           name_,
                                           gu::debug_name(m.node),
                                           Ntype::get_name(m.spec_op),
                                           spec->to_pyrope(),
                                           describe(m.cls),
                                           ck.value(m.cls)->to_pyrope(),
                                           vec,
                                           rule));
      }
    }
  }
}

// Extraction: the cheapest member of each union class under the cost profile (cost/<profile>.cost, milestone I).
//
//   * a node's own price is the profile's price of (op, result width, operand widths and constants); the width is
//     the class's range-analysis bound in bits (a declared input or register width is a bound), else the profile's
//     default width;
//   * SHARING-AWARE: an operand class that has other readers costs nothing here (it is needed whichever member is
//     taken, so it cannot make one member dearer than the other); a single-reader operand costs its whole subtree.
//     Without this the extraction would undo low-lane narrowing, mux sharing and operator sharing, which add
//     nodes on purpose, and a lane Mux shared by a run of lanes would never beat the scalar Mux;
//   * a union keeps its original side (the older, lower class id) unless the alternative is STRICTLY cheaper, so a
//     tie never changes the graph and the choice is a pure function of class order (deterministic);
//   * costs saturate at extractor_fuel * 1024.
//
// Classes are numbered operands-first, so unions are visited in ascending order and every union a subtree meets has
// already been decided. The cost of a subtree is computed lazily and memoized: a graph with few unions pays little.
int64_t Engine::price_width(Class_id cls) const {
  const int64_t dflt = profile_ != nullptr ? profile_->default_width : 32;
  if (const auto* v = eg_.value_of(cls); v != nullptr && eg_.cls(cls).kind == Egraph::Kind::konst) {
    if (v->is_just_i64()) {
      const auto x = v->to_just_i64();
      int64_t    n = 1;
      for (uint64_t m = x < 0 ? ~static_cast<uint64_t>(x) : static_cast<uint64_t>(x); m > 1; m >>= 1) {
        ++n;
      }
      return n + (x < 0 ? 1 : 0);
    }
    return dflt;
  }
  if (const auto stamp = stamp_for(cls); stamp) {
    return std::min<int64_t>(stamp->second, 4096);
  }
  return dflt;
}

uint32_t Engine::class_cost(Class_id root) {
  const uint64_t fuel = std::max<uint32_t>(1, opt_.extractor_fuel);
  const auto     cap  = static_cast<uint32_t>(std::min<uint64_t>(fuel * 1024U, kCostUnset - 1));
  std::vector<Class_id> stack{root};
  while (!stack.empty()) {
    const Class_id c = stack.back();
    if (cost_[c] != kCostUnset) {
      stack.pop_back();
      continue;
    }
    const auto& cls = eg_.cls(c);
    if (cls.kind != Egraph::Kind::node) {
      // A leaf, a constant, a literal: free. A union is decided in ascending class order by compute_best; one
      // reached out of order (never expected) is decided on demand.
      if (cls.kind == Egraph::Kind::uni) {
        const auto a = cls.alt_a, b = cls.alt_b;
        if (cost_[a] == kCostUnset || cost_[b] == kCostUnset) {
          if (cost_[a] == kCostUnset) {
            stack.push_back(a);
          }
          if (cost_[b] == kCostUnset) {
            stack.push_back(b);
          }
          continue;
        }
        const bool take_b = cost_[b] < cost_[a];
        cost_[c]          = take_b ? cost_[b] : cost_[a];
        best_[c]          = take_b ? best_[b] : best_[a];
      } else {
        cost_[c] = 0;
      }
      stack.pop_back();
      continue;
    }
    const Enode& n       = eg_.enode(cls.first);
    bool         pending = false;
    for (const auto& o : eg_.operands(n)) {
      if (cost_[o.cls] == kCostUnset) {
        stack.push_back(o.cls);
        pending = true;
      }
    }
    if (pending) {
      continue;
    }
    absl::InlinedVector<cost::Operand, 8> ops;
    uint64_t                               sum = 0;
    for (const auto& o : eg_.operands(n)) {
      cost::Operand operand;
      operand.bank  = o.bank;
      operand.width = price_width(o.cls);
      if (const auto* v = eg_.value_of(o.cls); v != nullptr && eg_.cls(o.cls).kind == Egraph::Kind::konst && v->is_just_i64()) {
        operand.is_const = true;
        operand.value    = v->to_just_i64();
      }
      ops.push_back(operand);
      if (readers_under_selection(o.cls) <= 1) {
        sum += cost_[o.cls];
      }
    }
    sum += static_cast<uint64_t>(cost::price(*profile_, n.op, price_width(c), ops));
    cost_[c] = static_cast<uint32_t>(std::min<uint64_t>(sum, cap));
    stack.pop_back();
  }
  return cost_[root];
}

// Readers of a class, counted for sharing. The first pass is optimistic (every e-node that reads the class counts,
// including those of alternatives that may lose); later passes drop the rule-built readers the previous selection
// did not keep (readers_under_selection), so two alternatives that only "share" an operand with each other do not
// look shared.
uint32_t Engine::readers_under_selection(Class_id cls) const {
  const auto r = eg_.resolve(cls);
  if (r >= readers_.size()) {
    return 0;
  }
  return sel_readers_.empty() ? readers_[r] : sel_readers_[r];
}

void Engine::compute_best() {
  has_best_ = stats_.unions != 0;
  if (!has_best_) {
    return;
  }
  const size_t n = eg_.num_classes();
  best_.resize(n);
  for (Class_id c = 0; c < n; ++c) {
    best_[c] = c;
  }
  std::vector<Class_id> unions;
  for (Class_id c = 0; c < n; ++c) {
    if (eg_.cls(c).kind == Egraph::Kind::uni) {
      unions.push_back(c);
    }
  }
  sel_readers_.clear();
  std::vector<Class_id> previous;
  for (int pass = 0; pass < 3; ++pass) {
    cost_.assign(n, kCostUnset);
    for (const Class_id c : unions) {
      const auto& cls = eg_.cls(c);
      const auto  ca  = class_cost(cls.alt_a);
      const auto  cb  = class_cost(cls.alt_b);
      if (cb < ca) {
        cost_[c] = cb;
        best_[c] = best_[cls.alt_b];
      } else {
        cost_[c] = ca;
        best_[c] = best_[cls.alt_a];
      }
    }
    std::vector<Class_id> chosen;
    chosen.reserve(unions.size());
    for (const Class_id c : unions) {
      chosen.push_back(best_[c]);
    }
    if (pass != 0 && chosen == previous) {
      break;  // the selection is a fixpoint of its own sharing
    }
    previous = std::move(chosen);
    if (pass == 2) {
      break;
    }
    // Recount the readers under this selection: every original reader stays, a rule-built reader counts only
    // when the selection reaches its e-node.
    std::vector<uint32_t> kept_rule_reads(n, 0);
    std::vector<uint8_t>  seen(n, 0);
    std::vector<Class_id> stack;
    for (const auto& member : members_) {
      stack.push_back(member.cls);
    }
    while (!stack.empty()) {
      Class_id c = stack.back();
      stack.pop_back();
      if (c >= n || seen[c] != 0) {
        continue;
      }
      seen[c]         = 1;
      const auto& cls = eg_.cls(c);
      if (cls.kind == Egraph::Kind::uni) {
        stack.push_back(best_[c]);
        continue;
      }
      if (cls.kind != Egraph::Kind::node) {
        continue;
      }
      const Enode& e = eg_.enode(cls.first);
      for (const auto& o : eg_.operands(e)) {
        if (e.rule != rt::kNoRule) {
          const auto r = eg_.resolve(o.cls);
          if (r < n) {
            ++kept_rule_reads[r];
          }
        }
        stack.push_back(o.cls);
      }
    }
    sel_readers_.assign(n, 0);
    for (Class_id c = 0; c < n && c < readers_.size(); ++c) {
      const uint32_t original = readers_[c] > rule_readers_[c] ? readers_[c] - rule_readers_[c] : 0;
      sel_readers_[c]         = original + kept_rule_reads[c];
    }
  }
  sel_readers_.clear();
}

// ---------------------------------------------------------------------------
// Elaboration
// ---------------------------------------------------------------------------

// (Re)connect `node` as an `op` cell over the pins realizing `ops`: the node
// keeps its identity (nid, name, srcid, output pin and consumers).
void Engine::rewire(const hhds::Node_class& node, Ntype_op op, std::span<const Operand> ops) {
  // Same cell over the same drivers per bank: keep the sinks exactly as the front end ordered them. The e-node
  // sorts a commutative bank by class, so rebuilding would swap e.g. `eq(x, 0)` into `eq(0, x)`; the graph is
  // structurally identical, but pin order is the order every later pass (and the ABC AIG it feeds) walks, and
  // classic cprop leaves it alone.
  if (gu::type_op_of(node) == op) {
    absl::flat_hash_map<std::pair<uint32_t, hhds::Class_index>, int> balance;
    size_t                                                           have = 0;
    bool                                                             same = true;
    for (const auto& spin : node.inp_sorted_pins()) {
      const auto drivers = spin.get_driver_pins();
      if (drivers.size() != 1 || drivers.front().is_invalid()) {
        same = false;
        break;
      }
      ++balance[{static_cast<uint32_t>(Ntype::sink_bank(op, spin.get_port_id())), drivers.front().get_class_index()}];
      ++have;
    }
    if (same && have == ops.size()) {
      for (const auto& operand : ops) {
        if (rep_pin_[operand.cls].is_invalid()) {
          same = false;
          break;
        }
        --balance[{operand.bank, rep_pin_[operand.cls].get_class_index()}];
      }
      if (same && std::ranges::all_of(balance, [](const auto& kv) { return kv.second == 0; })) {
        return;
      }
    }
  }
  for (const auto& spin : node.inp_pins_snapshot()) {
    spin.del_sink();
  }
  if (gu::type_op_of(node) != op) {
    gu::set_type_op(node, op);
  }
  // Every sink was just deleted, so a banked cell's bank slots fill in order
  // bank, bank+stride, ...: count them here instead of rescanning the pin list
  // per operand (gu::append_sink_operand), which is quadratic in the width of a
  // flattened bank.
  const bool                 banked = Ntype::is_banked_sink_op(op);
  const auto                 stride = banked ? static_cast<hhds::Port_id>(Ntype::sink_bank_count(op)) : hhds::Port_id{0};
  std::vector<hhds::Port_id> next(stride);
  for (hhds::Port_id b = 0; b < stride; ++b) {
    next[b] = b;
  }
  // The e-node keeps a commutative bank sorted by class, an order that depends on when each class was interned and
  // that puts a constant (interned first) in front. Front ends and classic cprop spell `a ^ b ^ c` / `x == 0` in source
  // order with constants last, and pin order is what every later pass and the AIG ABC builds from it walk. So emit each
  // bank in the order its drivers were created (storage order of the driver cell, then its pin), constants last. The
  // class order is only the tie-break for operands driven by the same pin.
  absl::InlinedVector<Operand, 8> ordered(ops.begin(), ops.end());
  if (op != Ntype_op::Hotmux) {  // a Hotmux bank is an ordered (control, value) association, not a multiset
    const auto key = [&](const Operand& x) {
      const auto pin = rep_pin_[x.cls];
      if (pin.is_const()) {
        return std::tuple<uint32_t, bool, uint64_t, uint32_t>{x.bank, true, 0, 0};
      }
      return std::tuple<uint32_t, bool, uint64_t, uint32_t>{
          x.bank, false, static_cast<uint64_t>(pin.get_master_node().get_debug_nid()), static_cast<uint32_t>(pin.get_port_id())};
    };
    // Classic flattens a nested And/Or by putting the outer operand in front of the inner chain (so a chain reads
    // newest first) and a nested Xor/Sum/Mult the other way round (oldest first); match both.
    const bool newest_first = op == Ntype_op::And || op == Ntype_op::Or;
    std::ranges::stable_sort(ordered, [&](const Operand& x, const Operand& y) {
      const auto kx = key(x), ky = key(y);
      if (newest_first && std::get<0>(kx) == std::get<0>(ky) && !std::get<1>(kx) && !std::get<1>(ky)) {
        return std::pair{std::get<2>(kx), std::get<3>(kx)} > std::pair{std::get<2>(ky), std::get<3>(ky)};
      }
      return kx < ky;
    });
  }
  for (const auto& operand : ordered) {
    const auto      driver = rep_pin_[operand.cls];
    hhds::Pin_class sink;
    if (banked) {
      I(operand.bank < stride);
      sink               = node.create_sink_pin(next[operand.bank]);
      next[operand.bank] = static_cast<hhds::Port_id>(next[operand.bank] + stride);
    } else {
      sink = node.create_sink_pin(static_cast<hhds::Port_id>(operand.bank));
    }
    driver.connect_sink(sink);
  }
}

// A rule-built e-node no original node realizes: a new cell.
void Engine::create_node(Class_id cls) {
  const Enode& n    = eg_.enode(eg_.cls(cls).first);
  auto         node = gu::create_typed_node(g_, n.op);
  rewire(node, n.op, eg_.operands(n));
  rep_pin_[cls] = node.create_driver_pin(0);
  if (opt_.preflight && n.op != Ntype_op::Mux) {
    // No bitwidth follows: the cell is stamped like every cell classic Cprop{false} makes. The class a member
    // is realized for is equal to that member as an unbounded integer, so the member's own stamp is valid.
    if (cls == owner_cls_ && !owner_pin_.is_invalid() && gu::bits_of(owner_pin_) != 0) {
      gu::set_bits(rep_pin_[cls], gu::bits_of(owner_pin_));
      if (!gu::is_unsign(owner_pin_)) {
        gu::set_sign(rep_pin_[cls]);
      }
    } else if (const auto stamp = stamp_for(cls); stamp) {
      if (stamp->first) {
        gu::set_ubits(rep_pin_[cls], stamp->second);
      } else {
        gu::set_sbits(rep_pin_[cls], stamp->second);
      }
      ++stats_.stamped;
    }
  }
  const auto& attrs = eg_.attrs_of(n);
  if (attrs.has_color) {
    gu::set_color(node, attrs.color);
  }
  if (attrs.policy != 0) {
    node.attr(livehd::attrs::synth_policy).set(eg_.policies()[attrs.policy]);
  }
  if (n.rule != rt::kNoRule) {
    ++stats_.rule_counts[n.rule].extracted;
    if (!opt_.explain.empty() && opt_.explain == rt::Registry::instance().rule(n.rule).name) {
      std::fprintf(stderr,
                   "aegraph-explain graph=%s rule=%s node=%s\n",
                   name_.c_str(),
                   opt_.explain.c_str(),
                   describe(cls).c_str());
    }
  }
}

// The pin that realizes `cls` (extracting a union's cheapest member). Iterative:
// a chain of rule-built e-nodes can be deeper than any stack.
hhds::Pin_class Engine::realize(Class_id cls) {
  std::vector<Class_id> stack{cls};
  while (!stack.empty()) {
    const auto c = stack.back();
    ensure(c);
    if (!rep_pin_[c].is_invalid()) {
      stack.pop_back();
      continue;
    }
    const auto& k = eg_.cls(c);
    switch (k.kind) {
      case Egraph::Kind::konst: {
        rep_pin_[c] = gu::create_const(g_, *eg_.value_of(c));
        ++stats_.consts_created;
        stack.pop_back();
        break;
      }
      case Egraph::Kind::uni: {
        const auto member = has_best_ ? best_[c] : k.alt_a;
        ensure(member);
        if (rep_pin_[member].is_invalid()) {
          stack.push_back(member);
          break;
        }
        rep_pin_[c] = rep_pin_[member];
        stack.pop_back();
        break;
      }
      case Egraph::Kind::node: {
        bool ready = true;
        for (const auto& o : eg_.operands(eg_.enode(k.first))) {
          ensure(o.cls);
          if (rep_pin_[o.cls].is_invalid()) {
            stack.push_back(o.cls);
            ready = false;
          }
        }
        if (ready) {
          create_node(c);
          stack.pop_back();
        }
        break;
      }
      default: I(false, "a leaf or literal class always has its pin");
    }
  }
  return rep_pin_[cls];
}

std::optional<std::pair<bool, int>> Engine::stamp_for(Class_id cls) const {
  const auto f = rt::facts_of(eg_, cls);
  if (!f.bounded()) {
    return std::nullopt;
  }
  const auto bits_of_nonneg = [](rt::Wide v) {
    int n = 0;
    for (; v > 0; v >>= 1) {
      ++n;
    }
    return n;
  };
  if (f.lo >= 0) {
    return std::pair{true, std::max(1, bits_of_nonneg(f.hi))};
  }
  const int width = 1 + std::max(bits_of_nonneg(std::max<rt::Wide>(f.hi, 0)), bits_of_nonneg(-f.lo - 1));
  if (width > (1 << 16)) {
    return std::nullopt;
  }
  return std::pair{false, width};
}

// Walk what elaborate() is about to realize and check every cell it would create can carry a stamp: a member's
// own top class takes the member's stamp (if the member has one), every other new cell needs a bounded range.
bool Engine::preflight_ok() {
  absl::flat_hash_set<Class_id> seen;
  std::vector<std::pair<Class_id, const Member*>> stack;  // (class, the member it is the top of; null = interior)
  for (const auto& member : members_) {
    Class_id kept = member.cls;
    if (has_best_ && eg_.cls(kept).kind == Egraph::Kind::uni) {
      kept = best_[kept];
    }
    stack.emplace_back(kept, &member);
  }
  while (!stack.empty()) {
    auto [c, top] = stack.back();
    stack.pop_back();
    ensure(c);
    if (!seen.insert(c).second) {
      continue;
    }
    const auto& k = eg_.cls(c);
    if (k.kind == Egraph::Kind::uni) {
      stack.emplace_back(has_best_ ? best_[c] : k.alt_a, top);
      continue;
    }
    if (k.kind != Egraph::Kind::node || !rep_pin_[c].is_invalid()) {
      continue;  // a leaf, a constant, or a class an original node realizes
    }
    const Enode n = eg_.enode(k.first);
    if (n.op != Ntype_op::Mux) {
      const bool member_stamp = top != nullptr && gu::bits_of(top->node.get_driver_pin(0)) != 0;
      if (!member_stamp && !stamp_for(c)) {
        return false;
      }
    }
    for (const auto& o : eg_.operands(n)) {
      stack.emplace_back(o.cls, nullptr);
    }
  }
  return true;
}

// Adopt: `member.node` becomes the cell of the rule-built e-node `target` (same
// nid, name, output pin and consumers; only its operands, and its op, change).
// This is what keeps a rewritten node's identity, as the classic engine's
// in-place rewrites do.
bool Engine::adopt(const Member& member, Class_id target) {
  const auto& k = eg_.cls(target);
  if (k.kind != Egraph::Kind::node || !rep_pin_[target].is_invalid() || eg_.enode(k.first).rule == rt::kNoRule) {
    return false;
  }
  for (const auto& o : eg_.operands(eg_.enode(k.first))) {
    const auto pin = realize(o.cls);
    // The node must not feed itself.
    if (!pin.is_const() && pin.get_master_node() == member.node) {
      return false;
    }
  }
  const Enode  n = eg_.enode(k.first);  // realize() may not grow the e-graph, but copy anyway
  const auto   ops = eg_.operands(n);
  rewire(member.node, n.op, ops);
  rep_pin_[target] = member.node.get_driver_pin(0);
  ++stats_.rule_counts[n.rule].extracted;
  return true;
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
  gu::carry_signal_roles(source, target);
  gu::carry_preroute_exclusion(source, target);
  if (!target.is_const() && node.attr(livehd::attrs::preroute_exclude).has()) {
    target.attr(livehd::attrs::preroute_net_exclude).set();
  }
  for (const auto& out : outs) {
    target.connect_sink(out.sink);
  }
  node.del_node();
  return Replaced::done;
}

void Engine::elaborate() {
  for (const auto& member : members_) {
    // The class the extraction keeps (a union's cheapest side).
    Class_id kept = member.cls;
    if (has_best_ && eg_.cls(kept).kind == Egraph::Kind::uni) {
      kept = best_[kept];
    }
    ensure(kept);
    if (rep_pin_[kept].is_invalid() && adopt(member, kept)) {
      ++stats_.rewritten;
      continue;
    }
    owner_cls_        = kept;
    owner_pin_        = member.node.get_driver_pin(0);
    const auto target = realize(member.cls);
    owner_cls_        = kNoClass;
    owner_pin_        = {};
    if (target == member.node.get_driver_pin(0)) {
      continue;  // the class representative is the original node: reused as is
    }
    const auto result = replace(member, target);
    if (result == Replaced::declined) {
      ++stats_.kept_twin;
    } else if (result == Replaced::done) {
      switch (member.how) {
        case How::constant : ++stats_.const_folded; break;
        case How::forward  : ++stats_.forwarded; break;
        case How::rewritten: ++stats_.rewritten; break;
        default            : ++stats_.cse_merged; break;
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
    if (!node.has_out_edges() || dead_.contains(node.get_class_index())) {
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
    Member member{node, cls->cls, cls->how, Ntype_op::Invalid, {}};
    if (opt_.check == Options::Check::full) {
      member.spec_op  = gu::type_op_of(node);
      member.spec_ops = spec_ops_;
    }
    members_.push_back(std::move(member));
  }
}

// A pure node that nothing live reads (every reader is a dead pure node, or it has none) is dead on arrival: the
// sweep deletes it, so it must not count as a reader of its operands (a dead Mux over a live Or would make the Or
// look shared). Consumers are visited before producers.
void Engine::mark_dead(const std::vector<hhds::Node_class>& producers_first) {
  for (auto it = producers_first.rbegin(); it != producers_first.rend(); ++it) {
    const auto& node = *it;
    if (node.is_invalid() || gu::is_builtin_node(node) || !is_pure_candidate(node)) {
      continue;
    }
    // A reader is live unless it is itself a dead pure node. Every pure reader was decided already (consumers come
    // first) and every other reader (IO, state, an opaque cycle member) is not in dead_: no purity test per edge,
    // which for a Hotmux of thousands of inputs would re-run its exclusivity scan once per input.
    bool live = false;
    for (const auto& edge : node.out_edges()) {
      const auto reader = edge.sink.get_master_node();
      if (reader.is_invalid() || !dead_.contains(reader.get_class_index())) {
        live = true;
        break;
      }
    }
    if (!live) {
      dead_.insert(node.get_class_index());
    }
  }
}

// Edges from `node` to a reader that is not dead.
size_t Engine::live_readers(const hhds::Node_class& node) const {
  size_t readers = 0;
  for (const auto& edge : node.out_edges()) {
    const auto reader = edge.sink.get_master_node();
    if (reader.is_invalid() || !dead_.contains(reader.get_class_index())) {
      ++readers;
    }
  }
  return readers;
}

// The latch contract (graph/latch_contract.hpp, rule B) lets a latch's D depend on its own Q only through its
// own hold mux: a Mux that feeds the latch's din and takes the latch's Q as a DIRECT data arm. That Mux is a root
// of the extraction. A rule that rebuilt it (lane factoring, a boolean spelling of a constant arm) would put logic
// on the Q -> D path and the contract check rejects the design. Its operands are free to simplify.
void Engine::pin_latch_holds() {
  for (const auto& latch : nodes_) {
    if (gu::is_builtin_node(latch) || gu::type_op_of(latch) != Ntype_op::Latch) {
      continue;
    }
    const auto din = gu::get_driver_of_sink_name(latch, "din");
    if (din.is_invalid() || din.is_const() || gu::is_graph_input_pin(din)) {
      continue;
    }
    const auto mux = din.get_master_node();
    if (mux.is_invalid() || gu::type_op_of(mux) != Ntype_op::Mux) {
      continue;
    }
    const auto q = latch.get_driver_pin(0);
    for (const auto& sink : mux.inp_sorted_pins()) {
      if (sink.get_port_id() != 0 && sink.get_driver_pin() == q) {
        latch_holds_.insert(mux.get_class_index());
        break;
      }
    }
  }
}

// Readers are counted at insertion, but a structural twin can appear AFTER its first reader was built: A = x+y
// feeding C = A+z, with the twin A' = x+y (another reader) built later. A rule that asked "does A have one reader?"
// at C's insertion (flatten) would absorb A and leave A' as a second x+y. Count first: nodes that will hash-cons to
// one class (same op, attrs and operand groups, in walk order) share one reader total, credited to the first.
// Over-counting is impossible (equal keys intern to one class); a twin the engine merges by a fold stays uncounted
// and behaves as before.
void Engine::precount_twins(const std::vector<hhds::Node_class>& producers_first) {
  // The key is a 64-bit mix of (op, attrs, sorted (bank, operand group)): two distinct nodes sharing a key would only
  // make a reader total too high for one of them, which blocks a flatten and never allows a wrong one.
  absl::flat_hash_map<hhds::Class_index, uint64_t>       group_of_pin;  // driver pin -> group (pure nodes only)
  absl::flat_hash_map<uint64_t, uint32_t>                groups;        // key -> group id
  std::vector<hhds::Class_index>                         first;         // per group: the first node (it carries the total)
  std::vector<uint32_t>                                  total;         // per group: readers of all its nodes
  std::vector<std::pair<hhds::Class_index, uint32_t>>    node_group;
  absl::InlinedVector<std::pair<uint64_t, uint64_t>, 8>  ops;
  for (const auto& node : producers_first) {
    if (node.is_invalid() || gu::is_builtin_node(node) || !is_pure_candidate(node) || !node.has_out_edges()
        || dead_.contains(node.get_class_index())) {
      continue;
    }
    const auto op = gu::type_op_of(node);
    ops.clear();
    bool ok = true;
    for (const auto& sink : node.inp_sorted_pins()) {
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1 || drivers.front().is_invalid()) {
        ok = false;
        break;
      }
      const auto index = drivers.front().get_class_index();
      const auto it    = group_of_pin.find(index);
      const auto who   = it != group_of_pin.end() ? it->second : (uint64_t{1} << 62) | static_cast<uint64_t>(index.value);
      ops.emplace_back(static_cast<uint64_t>(Ntype::sink_bank(op, sink.get_port_id())), who);
    }
    if (!ok || ops.empty()) {
      continue;
    }
    std::ranges::sort(ops);
    uint64_t key = mix(0x7477696eULL, static_cast<uint64_t>(op));
    key          = mix(key, gu::has_color(node) ? (uint64_t{1} << 32) | gu::color_of(node) : 0);
    key          = mix(key, gu::has_proven(node) ? (uint64_t{1} << 32) | gu::proven_of(node) : 0);
    if (const auto policy = node.attr(livehd::attrs::synth_policy); policy.has()) {
      key = mix(key, std::hash<std::string>{}(std::string{policy.get()}) | 1);
    }
    for (const auto& [bank, who] : ops) {
      key = mix(mix(key, bank), who);
    }
    const auto [it, inserted] = groups.try_emplace(key, static_cast<uint32_t>(first.size()));
    if (inserted) {
      first.push_back(node.get_class_index());
      total.push_back(0);
    }
    total[it->second] += static_cast<uint32_t>(live_readers(node));
    node_group.emplace_back(node.get_class_index(), it->second);
    group_of_pin[node.get_driver_pin(0).get_class_index()] = it->second;
  }
  for (const auto& [index, gid] : node_group) {
    twin_readers_[index] = first[gid] == index ? total[gid] : 0;
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

Engine::Engine(hhds::Graph& graph, const Cycle_cutter& cut_cycles, const Options& options, std::string name)
    : g_(graph), cut_cycles_(cut_cycles), opt_(options), name_(std::move(name)) {
  auto& reg = rt::Registry::instance();
  stats_.rule_counts.assign(reg.size() + 1, Rule_counters{});
  enabled_.assign(reg.size() + 1, 0);
  for (rt::Rule_id id = 1; id <= reg.size(); ++id) {
    const auto& info = reg.rule(id);
    const bool  only = opt_.rules_only.empty() || name_listed(opt_.rules_only, info.name, info.group);
    enabled_[id]     = only && !name_listed(opt_.rules_disable, info.name, info.group);
    rules_active_    = rules_active_ || enabled_[id] != 0;
  }
  profile_ = rt::find_cost_profile(profile_name(opt_.cost));
  I(profile_ != nullptr, "cost profile is not registered (is gen/cost_*.gen.cpp linked into pass_aegraph?)");
}

Stats Engine::run() {
  std::optional<livehd::aegraph_profile::Timer> phase;
  phase.emplace(livehd::aegraph_profile::prepass);
  collect_nodes();
  stats_.nodes_in = nodes_.size();
  if (opt_.edge_budget == 0) {
    work_budget_ = std::numeric_limits<uint64_t>::max();
  } else {
    // Rule work is O(input edges): the cprop contract.
    uint64_t edges = nodes_.size();
    for (const auto& node : nodes_) {
      for ([[maybe_unused]] const auto& sink : node.inp_sorted_pins()) {
        ++edges;
      }
    }
    work_budget_ = edges * opt_.edge_budget;
  }

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
  phase.emplace(livehd::aegraph_profile::build);
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
  pin_latch_holds();
  mark_dead(producers_first);
  precount_twins(producers_first);
  build_all(producers_first);
  resolve_false_cycles();
  stats_.opaque = cyclic_nodes_.size();
  stats_.skeleton += cyclic_nodes_.size();
  stats_.classes   = eg_.num_classes();
  stats_.aeg_bytes     = eg_.bytes();
  stats_.aeg_cap_bytes = eg_.capacity_bytes();

  // Extract: a class without unions is its first member (the original node), a
  // constant class its constant, a forward the operand class; a union keeps its
  // cheapest side. realize() resolves them all.
  phase.emplace(livehd::aegraph_profile::extract);
  compute_best();
  if (opt_.check == Options::Check::full) {
    check_full();
  }
  phase.emplace(livehd::aegraph_profile::elaborate);
  if (opt_.preflight && !preflight_ok()) {
    stats_.refused = 1;  // a new cell could not be stamped: leave the body as the pre-pass made it
  } else {
    elaborate();
    sweep_dead();
  }
  phase.reset();

  stats_.nodes_out = 0;
  for ([[maybe_unused]] auto node : g_.body().nodes()) {
    ++stats_.nodes_out;
  }
  return stats_;
}

}  // namespace

Stats optimize(const std::shared_ptr<hhds::Graph>& graph, const Cycle_cutter& cut_cycles, const Options& options) {
  if (!graph) {
    return {};
  }
  const auto name = graph->get_io() ? std::string{graph->get_io()->get_name()} : std::string{};
  Engine     engine(*graph, cut_cycles, options, name);
  return engine.run();
}

Options make_options(std::string_view rules_disable, std::string_view rules_only, std::string_view max_rewrites,
                     std::string_view bisect_graph, std::string_view explain, std::string_view check,
                     std::string_view rewrite_depth, std::string_view matches_limit, std::string_view eclass_limit,
                     std::string_view extractor_fuel, std::string_view edge_budget, std::string_view cost,
                     std::string_view choices) {
  const auto bad = [](std::string_view label, std::string_view value, std::string_view want) {
    livehd::diag::err("pass.aegraph", "bad-option", "io").msg("compile.aegraph.{}={} is not {}", label, value, want).fatal();
  };
  const auto number = [&](std::string_view label, std::string_view text, uint64_t lo, uint64_t hi, uint64_t keep) -> uint64_t {
    if (text.empty()) {
      return keep;
    }
    uint64_t v   = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || v < lo || v > hi) {
      bad(label, text, std::format("an integer in [{}..{}]", lo, hi));
    }
    return v;
  };
  const auto rule_list = [&](std::string_view label, std::string_view list) {
    size_t i = 0;
    while (i <= list.size()) {
      const auto j = std::min(list.find('+', i), list.size());
      const auto t = list.substr(i, j - i);
      if (!t.empty()) {
        auto& reg = rt::Registry::instance();
        if (t.starts_with("group:") ? !reg.has_group(t.substr(6)) : !reg.find(t).has_value()) {
          bad(label, t, "a rule name or group:NAME of a registered rule (join several with '+')");
        }
      }
      i = j + 1;
    }
  };

  Options o;
  rule_list("rules_disable", rules_disable);
  rule_list("rules_only", rules_only);
  o.rules_disable = std::string{rules_disable};
  o.rules_only    = std::string{rules_only};
  o.max_rewrites  = number("max_rewrites", max_rewrites, 0, UINT32_MAX, o.max_rewrites);
  o.bisect_graph  = std::string{bisect_graph};
  if (!explain.empty() && !rt::Registry::instance().find(explain)) {
    bad("explain", explain, "the name of a registered rule");
  }
  o.explain = std::string{explain};
  if (check == "off") {
    o.check = Options::Check::off;
  } else if (check == "unions") {
    o.check = Options::Check::unions;
  } else if (check == "full") {
    o.check = Options::Check::full;
  } else if (!check.empty()) {
    bad("check", check, "off|unions|full");
  }
  o.rewrite_depth  = static_cast<uint32_t>(number("rewrite_depth", rewrite_depth, 0, 64, o.rewrite_depth));
  o.matches_limit  = static_cast<uint32_t>(number("matches_limit", matches_limit, 0, 32, o.matches_limit));
  o.eclass_limit   = static_cast<uint32_t>(number("eclass_limit", eclass_limit, 1, 32, o.eclass_limit));
  o.extractor_fuel = static_cast<uint32_t>(number("extractor_fuel", extractor_fuel, 1, UINT32_MAX, o.extractor_fuel));
  o.edge_budget    = static_cast<uint32_t>(number("edge_budget", edge_budget, 0, UINT32_MAX, o.edge_budget));
  if (cost == "sim") {
    o.cost = Options::Cost::sim;
  } else if (!cost.empty() && cost != "synth" && cost != "auto") {
    bad("cost", cost, "synth|sim|auto");  // the kernel resolves auto; a bare pass.aegraph reads it as synth
  }
  if (!choices.empty() && choices != "off" && choices != "const" && choices != "expr") {
    bad("choices", choices, "off|const|expr");
  }
  o.choices = !choices.empty() && choices != "off";
  return o;
}

}  // namespace livehd::aegraph
