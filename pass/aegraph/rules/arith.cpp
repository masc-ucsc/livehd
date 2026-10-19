//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Hand-written rules, milestone D, in the order of the parity-gap list
// (scripts/cprop_engine_sweep.py: what classic cprop still removes after the
// aegraph): the identities, cancellation and flattening of the commutative
// banks (Xor, And, Or, Sum, Mult), the comparison and Not folds.
//
// Every rule is exact in cprop's unlimited-integer algebra, and none creates a
// '?' spelling (P7): constants are combined only from FULLY KNOWN classes, and
// a '?' literal is an opaque operand like a signal.
//
// The rules whose left-hand side is a fixed pattern (not_not, not_xor_alt,
// mult_pow2_shl, xor_bool_one) live in arith.rules; what stays here needs a loop
// over a bank or a region walk.

#include <algorithm>
#include <vector>

#include "node_util.hpp"
#include "runtime/match.hpp"

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;

AEG_RULE_GROUP(arith);

namespace {

[[nodiscard]] bool is_identity(Ctx& ctx, Ntype_op op, Class_id c) {
  switch (op) {
    case Ntype_op::Mult: return ctx.is_int(c, 1);
    case Ntype_op::And : return ctx.is_int(c, -1);
    default            : return ctx.is_int(c, 0);  // Sum, Or, Xor
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Constants of a bank
// ---------------------------------------------------------------------------

// Sum(x, 1, 2) -> Sum(x, 3); Xor(x, 5, 5) -> x; Sum(x, 1, -4) -> Sum(x) - 3.
// A negative folded Sum constant goes to the SUBTRACTED bank as its magnitude.
AEG_NATIVE_RULE(const_fold_bank, simplify, 100, Ntype_op::Sum, Ntype_op::Mult, Ntype_op::And, Ntype_op::Or, Ntype_op::Xor) {
  std::vector<size_t> ks;
  for (size_t i = 0; i < root.ops.size(); ++i) {
    if (const auto* k = ctx.known(root.ops[i].cls); k != nullptr) {
      if (!k->is_numeric()) {
        return;  // a non-numeric constant folds to nil: leave the node
      }
      ks.push_back(i);
    }
  }
  if (ks.size() < 2) {
    return;
  }
  Dlop acc;
  acc = Dlop::create_integer(root.op == Ntype_op::And ? -1 : (root.op == Ntype_op::Mult ? 1 : 0));
  for (const auto i : ks) {
    const auto& v = *ctx.known(root.ops[i].cls);
    switch (root.op) {
      case Ntype_op::Sum : acc = root.ops[i].bank == 0 ? acc.add_op(v) : acc.sub_op(v); break;
      case Ntype_op::Mult: acc = acc.mult_op(v); break;
      case Ntype_op::And : acc = acc.and_op(v); break;
      case Ntype_op::Or  : acc = acc.or_op(v); break;
      default            : acc = acc.xor_op(v); break;
    }
  }
  if (acc.is_invalid() || acc.is_nil() || acc.has_unknowns()) {
    return;
  }
  if ((root.op == Ntype_op::Mult || root.op == Ntype_op::And) && acc.is_known_zero()) {
    out.subsume(ctx.konst(0));
    return;
  }
  if (root.op == Ntype_op::Or && acc.is_just_i64() && acc.to_just_i64() == -1) {
    out.subsume(ctx.konst(acc));
    return;
  }
  std::vector<Operand> rest;
  for (size_t i = 0; i < root.ops.size(); ++i) {
    if (!std::ranges::binary_search(ks, i)) {
      rest.push_back(root.ops[i]);
    }
  }
  const bool identity = (root.op == Ntype_op::Mult || root.op == Ntype_op::And)
                            ? acc.is_just_i64() && acc.to_just_i64() == (root.op == Ntype_op::Mult ? 1 : -1)
                            : acc.is_known_zero();
  if (!identity) {
    if (root.op == Ntype_op::Sum && acc.is_negative()) {
      Dlop zero;
      zero = Dlop::create_integer(0);
      Dlop magnitude;
      magnitude = zero.sub_op(acc);
      rest.push_back({1, ctx.konst(magnitude)});
    } else {
      rest.push_back({0, ctx.konst(acc)});
    }
  }
  out.subsume(ctx.make_bank(root.op, std::move(rest)));
}

// x * 0 = 0, x & 0 = 0, x | -1 = -1 for EVERY x (a '?' or unknown x included).
AEG_NATIVE_RULE(annihilate, simplify, 90, Ntype_op::Mult, Ntype_op::And, Ntype_op::Or) {
  const int64_t zap = root.op == Ntype_op::Or ? -1 : 0;
  for (const auto& o : root.ops) {
    if (ctx.is_int(o.cls, zap)) {
      out.subsume(ctx.konst(zap));
      return;
    }
  }
}

// Sum(x, 0) = x, Mult(x, 1) = x, And(x, -1) = x, Or/Xor(x, 0) = x.
AEG_NATIVE_RULE(ident_drop, simplify, 80, Ntype_op::Sum, Ntype_op::Mult, Ntype_op::And, Ntype_op::Or, Ntype_op::Xor) {
  if (root.ops.size() < 2) {
    return;
  }
  std::vector<Operand> rest;
  for (const auto& o : root.ops) {
    if (!is_identity(ctx, root.op, o.cls)) {
      rest.push_back(o);
    }
  }
  if (rest.size() != root.ops.size()) {
    out.subsume(ctx.make_bank(root.op, std::move(rest)));
  }
}

// A one-operand bank is its operand (a lone SUBTRACTED Sum operand is a negation).
AEG_NATIVE_RULE(single_operand, simplify, 70, Ntype_op::Sum, Ntype_op::Mult, Ntype_op::And, Ntype_op::Or, Ntype_op::Xor) {
  if (root.ops.size() == 1 && (root.op != Ntype_op::Sum || root.ops[0].bank == 0)) {
    out.subsume(root.ops[0].cls);
  }
}

// ---------------------------------------------------------------------------
// Duplicates and cancellation
// ---------------------------------------------------------------------------

// And(x, x, y) = And(x, y); Or likewise. One '?' literal has one value at all
// its uses, so a repeated literal is a repeated operand too.
AEG_NATIVE_RULE(idempotent, simplify, 60, Ntype_op::And, Ntype_op::Or) {
  std::vector<Operand> rest;
  for (const auto& o : root.ops) {
    if (rest.empty() || rest.back().cls != o.cls) {
      rest.push_back(o);
    }
  }
  if (rest.size() != root.ops.size()) {
    out.subsume(ctx.make_bank(root.op, std::move(rest)));
  }
}

// Xor(x, x, y) = y: pairs cancel, the parity of each operand survives.
AEG_NATIVE_RULE(xor_pair_cancel, simplify, 60, Ntype_op::Xor) {
  std::vector<Operand> rest;
  size_t               i = 0;
  while (i < root.ops.size()) {
    size_t j = i;
    while (j < root.ops.size() && root.ops[j].cls == root.ops[i].cls) {
      ++j;
    }
    if ((j - i) % 2 == 1) {
      rest.push_back(root.ops[i]);
    }
    i = j;
  }
  if (rest.size() != root.ops.size()) {
    out.subsume(ctx.make_bank(root.op, std::move(rest)));
  }
}

// Sum(x, y) - x = y: an operand in both banks cancels against itself.
AEG_NATIVE_RULE(sum_cancel, simplify, 60, Ntype_op::Sum) {
  std::vector<Operand> added, subtracted;
  for (const auto& o : root.ops) {
    (o.bank == 0 ? added : subtracted).push_back(o);
  }
  if (added.empty() || subtracted.empty()) {
    return;
  }
  std::vector<Operand> rest;
  std::vector<bool>    used(subtracted.size(), false);
  bool                 changed = false;
  for (const auto& a : added) {
    bool cancelled = false;
    for (size_t j = 0; j < subtracted.size() && !cancelled; ++j) {
      if (!used[j] && subtracted[j].cls == a.cls) {
        used[j]   = true;
        cancelled = true;
      }
    }
    if (cancelled) {
      changed = true;
    } else {
      rest.push_back(a);
    }
  }
  if (!changed) {
    return;
  }
  for (size_t j = 0; j < subtracted.size(); ++j) {
    if (!used[j]) {
      rest.push_back(subtracted[j]);
    }
  }
  out.subsume(ctx.make_bank(root.op, std::move(rest)));
}

// And(And(a, b), c) = And(a, b, c) when the inner cell has no other reader
// (absorbing a shared cell would only copy its operands). A SUBTRACTED inner
// Sum flips the banks of what it brings in. An aegraph never frees an e-node, so
// flattening a k-long chain one link at a time would leave O(k^2) dead operands
// behind (a 900-wide Or cost 400k pool entries). Instead a link whose only reader
// is a cell of the same op (Ctx::chain_inner) leaves the work to that reader, and
// the top of the chain absorbs every single-use link below it in ONE firing:
// linear, and a wide chain ends as one banked cell, as in classic cprop.
AEG_NATIVE_RULE(flatten, simplify, 50, Ntype_op::Sum, Ntype_op::Mult, Ntype_op::And, Ntype_op::Or, Ntype_op::Xor) {
  if (ctx.chain_inner(root.cls)) {
    return;
  }
  const auto absorbable = [&](const Operand& o) -> const Enode* {
    const Enode* inner = ctx.eg().enode_of(o.cls);
    if (inner != nullptr && inner->op == root.op && ctx.eg().attrs_of(*inner) == root.attrs && o.cls != root.cls
        && ctx.single_use(o.cls)) {
      return inner;
    }
    return nullptr;
  };
  std::vector<Operand> merged;
  std::vector<Operand> pending(root.ops.begin(), root.ops.end());
  bool                 any = false;
  while (!pending.empty()) {
    const Operand o = pending.back();
    pending.pop_back();
    if (const Enode* inner = absorbable(o)) {
      any = true;
      for (const auto& io : ctx.operands_of(*inner)) {
        pending.push_back({root.op == Ntype_op::Sum ? (o.bank ^ io.bank) : 0U, io.cls});
      }
    } else {
      merged.push_back(o);
    }
  }
  if (any) {
    out.subsume(ctx.make_bank(root.op, std::move(merged)));
  }
}

// ---------------------------------------------------------------------------
// Boolean hygiene (the sound subset of classic scalar_bool)
// ---------------------------------------------------------------------------

namespace {
// Drop operand `i` of `root` when some other operand of the bank is an `inner_op`
// e-node that lists an operand of the bank: b | (b & r) = b and b & (b | r) = b,
// exact at any width.
void absorb(Ctx& ctx, const Root& root, Ntype_op inner_op, Results& out) {
  if (root.ops.size() < 2 || root.ops.size() > 64) {
    return;
  }
  std::vector<Class_id> classes;
  for (const auto& o : root.ops) {
    classes.push_back(o.cls);
  }
  std::ranges::sort(classes);
  std::vector<Operand> rest;
  for (const auto& o : root.ops) {
    bool absorbed = ctx.each_member(o.cls, [&](Class_id member) {
      const Enode* n = ctx.node_of(member);
      if (n == nullptr || n->op != inner_op) {
        return false;
      }
      return std::ranges::any_of(ctx.operands_of(*n), [&](const Operand& io) {
        return io.cls != o.cls && std::ranges::binary_search(classes, io.cls);
      });
    });
    if (!absorbed) {
      rest.push_back(o);
    }
  }
  if (rest.size() != root.ops.size()) {
    out.subsume(ctx.make_bank(root.op, std::move(rest)));
  }
}
}  // namespace

AEG_NATIVE_RULE(or_absorb, simplify, 40, Ntype_op::Or) { absorb(ctx, root, Ntype_op::And, out); }
AEG_NATIVE_RULE(and_absorb, simplify, 40, Ntype_op::And) { absorb(ctx, root, Ntype_op::Or, out); }

// And(c, EQ(c, 0), ..) = 0 for any c (one of the pair is 0); Or(c, EQ(c, 0), ..) = 1
// when every operand is a 0/1 value.
AEG_NATIVE_RULE(bool_complement, simplify, 40, Ntype_op::And, Ntype_op::Or) {
  if (root.ops.size() > 64) {
    return;
  }
  std::vector<Class_id> classes;
  for (const auto& o : root.ops) {
    classes.push_back(o.cls);
  }
  std::ranges::sort(classes);
  bool found = false;
  for (const auto& o : root.ops) {
    const auto c = ctx.eq_against(o.cls, 0);
    if (c && std::ranges::binary_search(classes, *c)) {
      found = true;
      break;
    }
  }
  if (!found) {
    return;
  }
  if (root.op == Ntype_op::And) {
    out.subsume(ctx.konst(0));
  } else if (std::ranges::all_of(root.ops, [&](const Operand& o) { return ctx.bool01(o.cls); })) {
    out.subsume(ctx.konst(1));
  }
}

// And(x, 2^n - 1) = Get_mask(x, 0, n) for any sign of x: both keep the low n bits
// as a non-negative value. Every low-mask truncation gets the ONE canonical
// Get_mask shape that the slice and width folds key on (classic's
// canonicalize_and_mask). Only a binary And with exactly one constant operand.
AEG_NATIVE_RULE(and_mask_get_mask, simplify, 20, Ntype_op::And) {
  if (root.ops.size() != 2) {
    return;
  }
  for (size_t i = 0; i < 2; ++i) {
    const auto* mask = ctx.known(root.ops[i].cls);
    if (mask == nullptr || ctx.eg().cls(root.ops[i ^ 1].cls).kind == Egraph::Kind::konst
        || ctx.eg().cls(root.ops[i ^ 1].cls).kind == Egraph::Kind::literal) {
      continue;
    }
    const auto window = livehd::graph_util::mask_window_of(*mask);
    if (!window || window->first != 0) {
      continue;
    }
    const auto x  = root.ops[i ^ 1].cls;
    const auto hi = window->second;
    out.subsume(ctx.make(Ntype_op::Get_mask, {Operand{0, x}, Operand{2, ctx.konst(0)}, Operand{3, ctx.konst(hi)}}));
    return;
  }
}

// ---------------------------------------------------------------------------
// Not and comparisons
// ---------------------------------------------------------------------------

// P8: one driver is one value ('?' literals included), so EQ(x, x, .., x) = 1
// and LT(x, x) = GT(x, x) = 0 on exactly the banks {x} vs {x}.
AEG_NATIVE_RULE(cmp_same, simplify, 30, Ntype_op::EQ, Ntype_op::LT, Ntype_op::GT) {
  if (root.ops.size() < 2) {
    return;
  }
  if (root.op == Ntype_op::EQ) {
    if (std::ranges::all_of(root.ops, [&](const Operand& o) { return o.cls == root.ops[0].cls; })) {
      out.subsume(ctx.konst(1));
    }
    return;
  }
  if (root.ops.size() == 2 && root.ops[0].bank != root.ops[1].bank && root.ops[0].cls == root.ops[1].cls) {
    out.subsume(ctx.konst(0));
  }
}

// EQ(b, 1) = b and EQ(EQ(y, 0), 0) = y for 0/1 values: the boolean EQ chains a
// lowered `!x` and to-bool leave behind.
AEG_NATIVE_RULE(eq_bool_chain, simplify, 30, Ntype_op::EQ) {
  if (root.ops.size() != 2) {
    return;
  }
  for (size_t i = 0; i < 2; ++i) {
    const Class_id other = root.ops[i ^ 1].cls;
    if (ctx.is_int(root.ops[i].cls, 1) && !ctx.known(other) && ctx.bool01(other)) {
      out.subsume(other);
      return;
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    if (!ctx.is_int(root.ops[i].cls, 0)) {
      continue;
    }
    const auto y = ctx.eq_against(root.ops[i ^ 1].cls, 0);  // EQ(y, 0) feeding this EQ(_, 0)
    if (y && ctx.bool01(*y)) {
      out.subsume(*y);
      return;
    }
  }
}
