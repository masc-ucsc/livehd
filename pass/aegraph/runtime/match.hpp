//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass/aegraph/runtime: the rule runtime (milestone D).
//
// A rule is a C++ function over the e-graph. It never sees an LGraph handle:
// it reads classes through a Ctx, builds new e-nodes through Ctx::make (which
// folds, normalizes, hash-conses and runs the rules of the new node, up to
// rewrite_depth), and answers through Results:
//
//   subsume(c)   `simplify`: c replaces the class of the e-node being rewritten
//                (Cranelift's subsume); the first one in priority order wins
//   add(c)       `alt`: c is unioned with the class; extraction picks one
//   choice(...)  `choice`: an alt tagged with a `?` literal assignment (off
//                until the choice milestone; ignored while choices=off)
//
// Rules are declared with AEG_RULE / AEG_NATIVE_RULE and register themselves
// at startup. The registry fixes Rule_id and the dispatch table per root op.
//
//   AEG_RULE_GROUP(arith);
//   AEG_RULE(not_not, simplify, 10, Ntype_op::Not) {
//     Class_id x = kNoClass;
//     if (match_root(ctx, root, Ntype_op::Not, m_op(Ntype_op::Not, m_bind{&x}))) {
//       out.subsume(x);
//     }
//   }
//
// The combinators follow LLVM's PatternMatch: a pattern is a small value with
// `bool match(Ctx&, Class_id, Cont)`. Matching a class tries each of its
// members (a union has several, bounded by eclass_limit) and calls the
// continuation once a pattern matched, so a later failure backtracks into the
// next member. `Cont` returning true stops the search.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include "absl/functional/function_ref.h"
#include "aegraph.hpp"

namespace livehd::aegraph::rt {

using Rule_id = uint16_t;  // 1-based index into the registry; 0 = no rule (an original node)
inline constexpr Rule_id kNoRule = 0;

enum class Tag : uint8_t { simplify, alt, choice };

[[nodiscard]] constexpr std::string_view tag_name(Tag t) {
  switch (t) {
    case Tag::simplify: return "simplify";
    case Tag::alt     : return "alt";
    default           : return "choice";
  }
}

// The e-node a rule rewrites (already normalized: operands sorted by bank, then
// class). `cls` is its class.
struct Root {
  Ntype_op                 op;
  const Enode_attrs&       attrs;
  std::span<const Operand> ops;
  Class_id                 cls;
};

// What Ctx needs from the engine. Implemented by the Engine in aegraph.cpp so a
// rule stays free of the LGraph.
class Builder {
public:
  virtual ~Builder() = default;
  // Fold, normalize, hash-cons and rewrite one new e-node (attrs inherited from
  // the e-node being rewritten). The returned class is final: any rule that
  // replaced or extended it is already applied.
  virtual Class_id make(Ntype_op op, std::span<const Operand> ops) = 0;
  // A fully known constant class (P7: never a `?` spelling).
  virtual Class_id konst(const Dlop& value) = 0;
  // True when `cls` has exactly one reader in the graph being built, so
  // absorbing it into its reader cannot duplicate logic.
  [[nodiscard]] virtual bool single_use(Class_id cls) const = 0;
  // True when the one reader of the original node behind `cls` is a cell of the
  // same op and attrs: that reader will absorb `cls` (see the flatten rule), so
  // absorbing a chain link by link would only leave dead intermediate banks.
  [[nodiscard]] virtual bool chain_inner(Class_id /*cls*/) const { return false; }
  // True when the graph is a lifted loop body whose new cells bitwidth will size (Options::guard_scope).
  [[nodiscard]] virtual bool guard_scope() const { return false; }
};

class Ctx {
public:
  Ctx(const Egraph& eg, Builder& builder, size_t member_fuel) : eg_(eg), builder_(builder), fuel_(member_fuel) {}

  [[nodiscard]] const Egraph& eg() const { return eg_; }

  // ---- building ----
  Class_id make(Ntype_op op, std::span<const Operand> ops) { return builder_.make(op, ops); }
  Class_id make(Ntype_op op, std::initializer_list<Operand> ops) {
    return builder_.make(op, std::span<const Operand>(ops.begin(), ops.size()));
  }
  Class_id konst(int64_t v) { return builder_.konst(*Dlop::create_integer(v)); }
  Class_id konst(const Dlop& v) { return builder_.konst(v); }
  // An n-ary commutative op over `ops`, with the degenerate cases folded: no
  // operand is the op's identity, one operand is itself (a lone SUBTRACTED Sum
  // operand stays a Sum).
  Class_id make_bank(Ntype_op op, std::vector<Operand> ops);
  [[nodiscard]] bool single_use(Class_id cls) const { return builder_.single_use(cls); }
  [[nodiscard]] bool chain_inner(Class_id cls) const { return builder_.chain_inner(cls); }
  [[nodiscard]] bool guard_scope() const { return builder_.guard_scope(); }

  // ---- reading ----
  // Calls fn(member_class) for each non-union member of `cls` until fn returns
  // true. The walk shares one fuel budget per rule run.
  template <typename Fn>
  bool each_member(Class_id cls, Fn&& fn) {
    size_t     visited = 0;
    const bool stopped = eg_.for_each_member(cls, std::min<size_t>(fuel_, 64), [&](Class_id m) {
      ++visited;
      return fn(m);
    });
    fuel_ -= std::min(fuel_, visited);
    return stopped;
  }
  // The e-node of a member class (null for a leaf, a constant, a literal).
  [[nodiscard]] const Enode* node_of(Class_id member) const { return eg_.enode_of(member); }
  [[nodiscard]] std::span<const Operand> operands_of(const Enode& n) const { return eg_.operands(n); }
  // The value of a FULLY KNOWN constant class (null for everything else: a `?`
  // literal, a node with a three-valued value, a leaf).
  [[nodiscard]] const Dlop* known(Class_id cls) const {
    const auto& c = eg_.cls(cls);
    return c.kind == Egraph::Kind::konst ? eg_.value_of(cls) : nullptr;
  }
  [[nodiscard]] bool is_int(Class_id cls, int64_t v) const {
    const auto* k = known(cls);
    return k != nullptr && k->is_numeric() && k->is_just_i64() && k->to_just_i64() == v;
  }
  [[nodiscard]] std::optional<int64_t> small_int(Class_id cls) const {
    const auto* k = known(cls);
    if (k == nullptr || !k->is_integer() || !k->is_just_i64()) {
      return std::nullopt;
    }
    return k->to_just_i64();
  }
  // Provably 0 or 1: a known 0/1, a comparison or 1-bit reduction, a 1-bit
  // Get_mask, And with a boolean operand, Or/Xor of booleans, a Mux of booleans.
  // Leaves (inputs, state) are never boolean here: the class carries no width.
  [[nodiscard]] bool bool01(Class_id cls, int depth = 3);
  // True when the `?` literal class can take the value `v` (its known bits agree), and when it can be nonzero
  // (the generated `choice` guard `(admits ?q C)`).
  [[nodiscard]] bool admits(Class_id literal, int64_t v) const;
  [[nodiscard]] bool admits_nonzero(Class_id literal) const;
  // The operand of a two-operand EQ that is not the known constant `k`, if the
  // class has such an EQ member.
  [[nodiscard]] std::optional<Class_id> eq_against(Class_id cls, int64_t k);

private:
  const Egraph& eg_;
  Builder&      builder_;
  size_t        fuel_;
};

// How many operands sit in operand bank `bank` (generated pattern code).
[[nodiscard]] inline size_t bank_count(std::span<const Operand> ops, uint32_t bank) {
  return static_cast<size_t>(std::ranges::count_if(ops, [bank](const Operand& o) { return o.bank == bank; }));
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

class Results {
public:
  struct Item {
    Tag      tag;
    Class_id cls;
    Class_id literal = kNoClass;  // choice: the `?` literal class the member assumes
    int64_t  value   = 0;         // choice: the value it assumes
  };

  void subsume(Class_id c) { items_.push_back({Tag::simplify, c}); }
  void add(Class_id c) { items_.push_back({Tag::alt, c}); }
  void choice(Class_id c, Class_id literal, int64_t value) { items_.push_back({Tag::choice, c, literal, value}); }

  [[nodiscard]] bool                      empty() const { return items_.empty(); }
  [[nodiscard]] const std::vector<Item>&  items() const { return items_; }

private:
  std::vector<Item> items_;
};

// ---------------------------------------------------------------------------
// Pattern combinators (PatternMatch style)
// ---------------------------------------------------------------------------

using Cont = absl::FunctionRef<bool()>;

struct m_any {
  bool match(Ctx&, Class_id, Cont k) const { return k(); }
};

// Binds the class (any kind).
struct m_bind {
  Class_id* out;
  bool      match(Ctx&, Class_id c, Cont k) const {
    *out = c;
    return k();
  }
};

// The class must equal an earlier binding.
struct m_same {
  const Class_id* ref;
  bool            match(Ctx&, Class_id c, Cont k) const { return c == *ref ? k() : false; }
};

// A fully known constant equal to `v`.
struct m_int {
  int64_t v;
  bool    match(Ctx& ctx, Class_id c, Cont k) const { return ctx.is_int(c, v) ? k() : false; }
};

// A fully known integer constant; binds its value and class.
struct m_k {
  int64_t*  value;
  Class_id* cls = nullptr;
  bool      match(Ctx& ctx, Class_id c, Cont k) const {
    const auto v = ctx.small_int(c);
    if (!v) {
      return false;
    }
    *value = *v;
    if (cls != nullptr) {
      *cls = c;
    }
    return k();
  }
};

namespace detail {
template <size_t I, typename Tuple>
bool match_ops(Ctx& ctx, std::span<const Operand> ops, const Tuple& subs, Cont k) {
  if constexpr (I == std::tuple_size_v<Tuple>) {
    return k();
  } else {
    return std::get<I>(subs).match(ctx, ops[I].cls, [&] { return match_ops<I + 1>(ctx, ops, subs, k); });
  }
}

// Positional: exactly sizeof...(P) operands on banks 0..n-1.
template <typename Tuple>
bool positional(std::span<const Operand> ops, const Tuple&) {
  if (ops.size() != std::tuple_size_v<Tuple>) {
    return false;
  }
  for (size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].bank != i) {
      return false;
    }
  }
  return true;
}
}  // namespace detail

// A member of the class that is an e-node of `op` with exactly one operand per
// positional pattern (bank i = pattern i).
template <typename... P>
struct m_op_t {
  Ntype_op        op;
  std::tuple<P...> subs;

  bool match(Ctx& ctx, Class_id c, Cont k) const {
    return ctx.each_member(c, [&](Class_id member) {
      const Enode* n = ctx.node_of(member);
      if (n == nullptr || n->op != op) {
        return false;
      }
      const auto ops = ctx.operands_of(*n);
      if (!detail::positional(ops, subs)) {
        return false;
      }
      return detail::match_ops<0>(ctx, ops, subs, k);
    });
  }
};

template <typename... P>
[[nodiscard]] m_op_t<P...> m_op(Ntype_op op, P... p) {
  return {op, std::tuple<P...>(p...)};
}

// The same, over the operands of the e-node being rewritten. The continuation
// form lets a rule collect every match; the plain form takes the first.
template <typename... P>
bool match_root_each(Ctx& ctx, const Root& root, Ntype_op op, Cont k, const P&... p) {
  if (root.op != op) {
    return false;
  }
  const std::tuple<P...> subs(p...);
  if (!detail::positional(root.ops, subs)) {
    return false;
  }
  return detail::match_ops<0>(ctx, root.ops, subs, k);
}

template <typename... P>
bool match_root(Ctx& ctx, const Root& root, Ntype_op op, const P&... p) {
  return match_root_each(ctx, root, op, Cont([] { return true; }), p...);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

using Rule_fn = void (*)(Ctx&, const Root&, Results&);

struct Rule_info {
  const char*           name  = "";
  const char*           group = "";
  Tag                   tag   = Tag::simplify;
  int                   prio  = 0;      // higher is tried first
  bool                  native = false; // AEG_NATIVE_RULE: no pattern, no proof obligation
  std::vector<Ntype_op> roots;
  Rule_fn               fn    = nullptr;
  const char*           file  = "";
  int                   line  = 0;
};

// All rules, sorted by (priority descending, group, name): the order is a pure
// function of the rule set, not of static-initialization order. Rule_id i is
// table()[i - 1]. Frozen on first use; a rule registered afterwards is a bug.
class Registry {
public:
  static Registry&       instance();
  void                   add(Rule_info info);
  [[nodiscard]] size_t   size();
  [[nodiscard]] const Rule_info& rule(Rule_id id);
  // Rule ids rooted at `op`, in the order they are tried.
  [[nodiscard]] std::span<const Rule_id> for_op(Ntype_op op);
  [[nodiscard]] std::optional<Rule_id>   find(std::string_view name);
  [[nodiscard]] bool                     has_group(std::string_view group);

private:
  void freeze();
  void freeze_once();

  std::once_flag                               once_;
  std::vector<Rule_info>                       pending_;
  std::vector<Rule_info>                       table_;
  std::vector<std::vector<Rule_id>>            by_op_;  // indexed by Ntype_op
  bool                                         frozen_ = false;
};

struct Registrar {
  Registrar(const char* name, const char* group, Tag tag, int prio, bool native, std::initializer_list<Ntype_op> roots, Rule_fn fn,
            const char* file, int line) {
    Rule_info info;
    info.name   = name;
    info.group  = group;
    info.tag    = tag;
    info.prio   = prio;
    info.native = native;
    info.roots  = roots;
    info.fn     = fn;
    info.file   = file;
    info.line   = line;
    Registry::instance().add(std::move(info));
  }
};

}  // namespace livehd::aegraph::rt

// One group per translation unit (the rule file name): `rules_disable=group:arith`.
#define AEG_RULE_GROUP(NAME) static constexpr const char* aeg_rule_group = #NAME

// AEG_RULE(name, priority, root ops...) with the tag first:
//   AEG_RULE(name, simplify|alt|choice, priority, root ops...) { body }
// In the body: `ctx` (Ctx&), `root` (const Root&), `out` (Results&).
#define AEG_RULE_IMPL(NAME, TAG, PRIO, NATIVE, ...)                                                            \
  static void aeg_rule_fn_##NAME(::livehd::aegraph::rt::Ctx&, const ::livehd::aegraph::rt::Root&,              \
                                 ::livehd::aegraph::rt::Results&);                                              \
  static const ::livehd::aegraph::rt::Registrar aeg_rule_reg_##NAME{                                           \
      #NAME, aeg_rule_group, ::livehd::aegraph::rt::Tag::TAG, PRIO, NATIVE, {__VA_ARGS__}, &aeg_rule_fn_##NAME, \
      __FILE__, __LINE__};                                                                                      \
  static void aeg_rule_fn_##NAME([[maybe_unused]] ::livehd::aegraph::rt::Ctx&           ctx,                    \
                                 [[maybe_unused]] const ::livehd::aegraph::rt::Root&    root,                   \
                                 [[maybe_unused]] ::livehd::aegraph::rt::Results&       out)

#define AEG_RULE(NAME, TAG, PRIO, ...) AEG_RULE_IMPL(NAME, TAG, PRIO, false, __VA_ARGS__)
// A rule no pattern can express (region producers, bank algebra): the same
// runtime, counters and knobs, but no generated proof obligation.
#define AEG_NATIVE_RULE(NAME, TAG, PRIO, ...) AEG_RULE_IMPL(NAME, TAG, PRIO, true, __VA_ARGS__)
