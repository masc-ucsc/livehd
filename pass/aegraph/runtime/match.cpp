//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "runtime/match.hpp"
#include "runtime/analyses.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace livehd::aegraph::rt {

// ---------------------------------------------------------------------------
// Ctx
// ---------------------------------------------------------------------------

Class_id Ctx::make_bank(Ntype_op op, std::vector<Operand> ops) {
  if (ops.empty()) {
    return konst(op == Ntype_op::And ? -1 : (op == Ntype_op::Mult ? 1 : 0));
  }
  // One operand is itself; a lone SUBTRACTED Sum operand is a negation and stays.
  if (ops.size() == 1 && (op != Ntype_op::Sum || ops.front().bank == 0)) {
    return ops.front().cls;
  }
  return make(op, std::span<const Operand>(ops.data(), ops.size()));
}

bool Ctx::bool01(Class_id cls, int depth) {
  if (depth <= 0) {
    return false;
  }
  if (const auto* k = known(cls); k != nullptr) {
    return k->is_integer() && k->is_just_i64() && (k->to_just_i64() == 0 || k->to_just_i64() == 1);
  }
  if (const auto f = facts_of(eg_, cls); f.lo >= 0 && f.hi <= 1) {
    return true;  // the interval [0, 1]
  }
  return each_member(cls, [&](Class_id member) {
    const Enode* n = node_of(member);
    if (n == nullptr) {
      return false;
    }
    const auto ops = operands_of(*n);
    switch (n->op) {
      case Ntype_op::EQ:
      case Ntype_op::LT:
      case Ntype_op::GT:
      case Ntype_op::Ror:
      case Ntype_op::Rxor: return true;
      case Ntype_op::Get_mask: {
        // ports 0 (value), 2 (lo), 3 (hi): a one-bit window
        if (ops.size() != 3 || ops[1].bank != 2 || ops[2].bank != 3) {
          return false;
        }
        const auto lo = small_int(ops[1].cls), hi = small_int(ops[2].cls);
        return lo && hi && *hi - *lo == 1;
      }
      case Ntype_op::And:
        // a & b for a boolean b is 0 or 1 whatever a is (even negative)
        return std::ranges::any_of(ops, [&](const Operand& o) { return bool01(o.cls, depth - 1); });
      case Ntype_op::Or:
      case Ntype_op::Xor: return std::ranges::all_of(ops, [&](const Operand& o) { return bool01(o.cls, depth - 1); });
      case Ntype_op::Mux:
        return ops.size() == 3 && ops[0].bank == 0 && ops[1].bank == 1 && ops[2].bank == 2 && bool01(ops[1].cls, depth - 1)
               && bool01(ops[2].cls, depth - 1);
      default: return false;
    }
  });
}

bool Ctx::admits(Class_id literal, int64_t v) const {
  const auto* value = eg_.value_of(literal);
  if (value == nullptr) {
    return false;
  }
  return !value->eq_op(*Dlop::create_integer(v))->is_known_false();
}

bool Ctx::admits_nonzero(Class_id literal) const {
  const auto* value = eg_.value_of(literal);
  return value != nullptr && !value->is_known_zero();
}

std::optional<Class_id> Ctx::eq_against(Class_id cls, int64_t k) {
  std::optional<Class_id> other;
  each_member(cls, [&](Class_id member) {
    const Enode* n = node_of(member);
    if (n == nullptr || n->op != Ntype_op::EQ) {
      return false;
    }
    const auto ops = operands_of(*n);
    if (ops.size() != 2) {
      return false;
    }
    if (is_int(ops[0].cls, k) && !is_int(ops[1].cls, k)) {
      other = ops[1].cls;
    } else if (is_int(ops[1].cls, k) && !is_int(ops[0].cls, k)) {
      other = ops[0].cls;
    }
    return other.has_value();
  });
  return other;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

Registry& Registry::instance() {
  static Registry registry;
  return registry;
}

void Registry::add(Rule_info info) {
  I(!frozen_, "aegraph rule registered after the registry was frozen");
  pending_.push_back(std::move(info));
}

void Registry::freeze() {
  std::call_once(once_, [this] { freeze_once(); });
}

void Registry::freeze_once() {
  frozen_ = true;
  table_  = std::move(pending_);
  // A pure function of the rule set: static-initialization order never shows.
  std::ranges::sort(table_, [](const Rule_info& a, const Rule_info& b) {
    if (a.prio != b.prio) {
      return a.prio > b.prio;
    }
    if (const int g = std::strcmp(a.group, b.group); g != 0) {
      return g < 0;
    }
    return std::strcmp(a.name, b.name) < 0;
  });
  I(table_.size() < UINT16_MAX);
  by_op_.assign(256, {});
  for (size_t i = 0; i < table_.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      I(std::strcmp(table_[i].name, table_[j].name) != 0, "duplicate aegraph rule name");
    }
    for (const auto op : table_[i].roots) {
      by_op_[static_cast<size_t>(op)].push_back(static_cast<Rule_id>(i + 1));
    }
  }
}

size_t Registry::size() {
  freeze();
  return table_.size();
}

const Rule_info& Registry::rule(Rule_id id) {
  freeze();
  I(id >= 1 && id <= table_.size());
  return table_[id - 1];
}

std::span<const Rule_id> Registry::for_op(Ntype_op op) {
  freeze();
  return by_op_[static_cast<size_t>(op)];
}

std::optional<Rule_id> Registry::find(std::string_view name) {
  freeze();
  for (size_t i = 0; i < table_.size(); ++i) {
    if (name == table_[i].name) {
      return static_cast<Rule_id>(i + 1);
    }
  }
  return std::nullopt;
}

bool Registry::has_group(std::string_view group) {
  freeze();
  return std::ranges::any_of(table_, [&](const Rule_info& r) { return group == r.group; });
}

}  // namespace livehd::aegraph::rt
