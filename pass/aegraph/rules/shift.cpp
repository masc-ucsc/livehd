//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Hand-written rules, milestone D: shift composition. All are exact under the
// unlimited-precision signed semantics (SHL never drops bits, SRA is floor
// division). The Sext algebra (sext_sext, sext_getmask) is pattern-shaped and
// lives in shift.rules.

#include <optional>

#include "runtime/match.hpp"

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;

AEG_RULE_GROUP(shift);

namespace {

constexpr int64_t kAmountCap = int64_t{1} << 28;  // the bound classic's const_shl_amount enforces
constexpr int64_t kMaskCap   = int64_t{1} << 16;  // the biggest -(2^a) mask worth minting

[[nodiscard]] bool two_banks(std::span<const Operand> ops) { return ops.size() == 2 && ops[0].bank == 0 && ops[1].bank == 1; }

[[nodiscard]] std::optional<int64_t> amount_of(Ctx& ctx, Class_id c) {
  const auto v = ctx.small_int(c);
  if (v && *v >= 0 && *v <= kAmountCap) {
    return v;
  }
  return std::nullopt;
}

}  // namespace

// x << 0 = x >> 0 = x.
AEG_NATIVE_RULE(shift_zero, simplify, 40, Ntype_op::SHL, Ntype_op::SRA) {
  if (two_banks(root.ops) && ctx.is_int(root.ops[1].cls, 0)) {
    out.subsume(root.ops[0].cls);
  }
}

// Compose the MIXED constant shifts (the same-direction chains SRA(SRA) and
// SHL(SHL) are the pattern rules shl_shl and sra_sra in shift.rules):
//   SRA(SHL(x,a),b) = SHL(x,a-b) | x | SRA(x,b-a)   (by the sign of a-b)
//   SHL(SRA(x,a),a) = And(x, -2^a)    (clears the low a bits in place)
AEG_NATIVE_RULE(shift_compose, simplify, 30, Ntype_op::SHL, Ntype_op::SRA) {
  if (!two_banks(root.ops)) {
    return;
  }
  const auto outer = amount_of(ctx, root.ops[1].cls);
  if (!outer) {
    return;
  }
  ctx.each_member(root.ops[0].cls, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n == nullptr || (n->op != Ntype_op::SHL && n->op != Ntype_op::SRA)) {
      return false;
    }
    const auto in_ops = ctx.operands_of(*n);
    if (!two_banks(in_ops)) {
      return false;
    }
    const auto inner = amount_of(ctx, in_ops[1].cls);
    if (!inner) {
      return false;
    }
    const Class_id x = in_ops[0].cls;
    const auto     shift = [&](Ntype_op op, int64_t amount) {
      return ctx.make(op, {Operand{0, x}, Operand{1, ctx.konst(amount)}});
    };
    if (n->op == root.op) {  // SRA(SRA) and SHL(SHL): the amounts add; shl_shl / sra_sra in shift.rules
      return false;
    }
    if (root.op == Ntype_op::SRA) {  // SRA(SHL(x, inner), outer)
      out.subsume(*outer >= *inner ? shift(Ntype_op::SRA, *outer - *inner) : shift(Ntype_op::SHL, *inner - *outer));
      return true;
    }
    // SHL(SRA(x, inner), outer): the SRA drops the low `inner` bits, so only the
    // equal case is a pure mask.
    if (*outer != *inner || *inner > kMaskCap) {
      return false;
    }
    if (*inner == 0) {
      out.subsume(x);
      return true;
    }
    Dlop mask;
    mask = Dlop::get_mask_value(static_cast<int>(*inner))->not_op();  // ~(2^a - 1) == -(2^a)
    out.subsume(ctx.make(Ntype_op::And, {Operand{0, x}, Operand{0, ctx.konst(mask)}}));
    return true;
  });
}

