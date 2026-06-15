//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Rvalue expression lowering (todo/ 2s subtask B). Two-tier constant
// evaluation: slang's own expr.eval() first (parameters, genvars, loop-var
// locals bound in eval_ctx_ fold here), structural LNAST lowering only for
// runtime values. Every lowered value satisfies the invariant that it sits
// in the integer range of its slang type; conversions go through the single
// materialize_conversion seam (slang_types.cpp).

#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::BinaryOperator;
using slang::ast::ExpressionKind;
using slang::ast::UnaryOperator;

std::string Slang_context::lower_rvalue(const slang::ast::Expression& expr) {
  // Tier 1: compile-time constant (parameters, localparams, genvars, unrolled
  // loop variables, sized literals, $clog2/$bits/... system calls).
  if (expr.kind != ExpressionKind::Assignment && expr.kind != ExpressionKind::LValueReference) {
    if (auto cv = try_eval(expr); cv && cv->isInteger()) {
      return const_text(cv->integer());
    }
  }

  switch (expr.kind) {
    case ExpressionKind::NamedValue: {
      const auto& nv = expr.as<slang::ast::NamedValueExpression>();
      return read_symbol(nv.symbol, expr.sourceRange);
    }
    case ExpressionKind::UnaryOp: return lower_unary(expr.as<slang::ast::UnaryExpression>());
    case ExpressionKind::BinaryOp: return lower_binary(expr.as<slang::ast::BinaryExpression>());
    case ExpressionKind::ConditionalOp: return lower_conditional_expr(expr.as<slang::ast::ConditionalExpression>());
    case ExpressionKind::Conversion: {
      const auto& conv = expr.as<slang::ast::ConversionExpression>();
      const auto& from = *conv.operand().type;
      const auto& to   = *conv.type;
      if (!to.isIntegral() || !from.isIntegral()) {
        emit_unsupported(expr.sourceRange, "unsupported-conversion", "only integral conversions are supported by --reader slang");
        return "0";
      }
      auto v  = to_int_value(lower_rvalue(conv.operand()));
      auto fi = tinfo(from);
      auto ti = tinfo(to);
      return materialize_conversion(v, fi.bits, fi.is_signed, ti.bits, ti.is_signed);
    }
    case ExpressionKind::Concatenation: return lower_concat(expr.as<slang::ast::ConcatenationExpression>());
    case ExpressionKind::Replication: {
      const auto& rep   = expr.as<slang::ast::ReplicationExpression>();
      auto        count = try_eval_int(rep.count());
      if (!count || *count < 0) {
        emit_error(expr.sourceRange, "non-const-replication", "syntax", "replication count must be a compile-time constant");
        return "0";
      }
      auto oi = tinfo(*rep.concat().type);
      auto v  = to_pattern(to_int_value(lower_rvalue(rep.concat())), oi.bits, oi.is_signed);
      if (*count == 0) {
        return "0";
      }
      std::vector<std::string> parts;
      for (int64_t i = 0; i < *count; ++i) {
        auto off = static_cast<int64_t>(oi.bits) * i;
        parts.emplace_back(off == 0 ? v : builder_.create_shl_stmts(v, std::to_string(off)));
      }
      return builder_.create_bit_or_stmts(parts);
    }
    case ExpressionKind::ElementSelect:
    case ExpressionKind::RangeSelect:
    case ExpressionKind::MemberAccess: return lower_select(expr);
    case ExpressionKind::Call: return lower_call(expr.as<slang::ast::CallExpression>());
    case ExpressionKind::SimpleAssignmentPattern:
      return lower_assignment_pattern(expr, expr.as<slang::ast::SimpleAssignmentPatternExpression>().elements());
    case ExpressionKind::StructuredAssignmentPattern:
      return lower_assignment_pattern(expr, expr.as<slang::ast::StructuredAssignmentPatternExpression>().elements());
    case ExpressionKind::ReplicatedAssignmentPattern:
      return lower_assignment_pattern(expr, expr.as<slang::ast::ReplicatedAssignmentPatternExpression>().elements());
    case ExpressionKind::Inside: {
      const auto& in = expr.as<slang::ast::InsideExpression>();
      auto        li = tinfo(*in.left().type);
      auto        l  = to_int_value(lower_rvalue(in.left()));
      std::string acc;
      for (const auto* item : in.rangeList()) {
        std::string match;
        if (item->kind == ExpressionKind::ValueRange) {
          const auto& vr = item->as<slang::ast::ValueRangeExpression>();
          auto        lo = mark_bool(builder_.create_ge_stmts(l, to_int_value(lower_rvalue(vr.left()))));
          auto        hi = mark_bool(builder_.create_le_stmts(l, to_int_value(lower_rvalue(vr.right()))));
          match          = mark_bool(builder_.create_log_and_stmts(lo, hi));
        } else if (auto cv = try_eval(*item); cv && cv->isInteger() && cv->integer().hasUnknown()) {
          // wildcard set-membership item: compare only the known bits
          const auto& sv    = cv->integer();
          auto        nbits = static_cast<int>(sv.getBitWidth());
          uint64_t    mask = 0, val = 0;
          bool        ok = nbits <= 64;
          for (int i = 0; ok && i < nbits; ++i) {
            auto b = sv[i];  // logic_t
            if (b.isUnknown()) {
              continue;
            }
            mask |= 1ULL << i;
            if (b.value != 0) {
              val |= 1ULL << i;
            }
          }
          if (!ok) {
            emit_unsupported(item->sourceRange, "wide-wildcard-inside", "wildcard inside items wider than 64 bits");
            return "0";
          }
          auto lp     = to_pattern(l, li.bits, li.is_signed);
          auto masked = builder_.create_bit_and_stmts(lp, std::to_string(mask));
          match       = mark_bool(builder_.create_eq_stmts(masked, std::to_string(val)));
        } else {
          match = mark_bool(builder_.create_eq_stmts(l, to_int_value(lower_rvalue(*item))));
        }
        acc = acc.empty() ? match : mark_bool(builder_.create_log_or_stmts(acc, match));
      }
      return acc.empty() ? std::string{"0"} : acc;
    }
    case ExpressionKind::LValueReference: {
      // compound assign / increment reads the in-flight assignment target
      if (!compound_read_.empty()) {
        return compound_read_;
      }
      emit_unsupported(expr.sourceRange, "lvalue-reference", "lvalue self-reference outside a compound assignment");
      return "0";
    }
    case ExpressionKind::StringLiteral: {
      const auto& sl = expr.as<slang::ast::StringLiteral>();
      const auto& iv = sl.getIntValue();
      if (iv.isInteger()) {
        return const_text(iv.integer());
      }
      emit_unsupported(expr.sourceRange, "unsupported-string", "string literals are only supported as packed integer values");
      return "0";
    }
    case ExpressionKind::Assignment:
      emit_unsupported(expr.sourceRange, "expression-assignment",
                       "assignments inside expressions are not supported by --reader slang");
      return "0";
    case ExpressionKind::Streaming:
      emit_unsupported(expr.sourceRange, "unsupported-streaming",
                       "streaming concatenation is not supported by --reader slang yet");
      return "0";
    default: break;
  }

  // CIRCT-style default fallback: nothing slips through silently.
  emit_unsupported(expr.sourceRange, "unsupported-expression",
                   std::string("expression kind '") + std::string(slang::ast::toString(expr.kind))
                       + "' is not supported by --reader slang yet");
  return "0";
}

std::string Slang_context::read_symbol(const slang::ast::ValueSymbol& sym, slang::SourceRange range) {
  // Parameters / enum values / genvars should have folded in tier 1; if eval
  // failed (e.g. inside an uninstantiated context) report cleanly.
  if (sym.kind == slang::ast::SymbolKind::Parameter) {
    const auto& cv = sym.as<slang::ast::ParameterSymbol>().getValue(range);
    if (cv.isInteger()) {
      return const_text(cv.integer());
    }
    emit_error(range, "non-integer-parameter", "type", std::string("parameter '") + std::string(sym.name) + "' is not integral");
    return "0";
  }
  if (sym.kind == slang::ast::SymbolKind::EnumValue) {
    const auto& cv = sym.as<slang::ast::EnumValueSymbol>().getValue(range);
    if (cv.isInteger()) {
      return const_text(cv.integer());
    }
  }

  auto name = lname_of(sym);

  if (input_syms_.contains(&sym) || reg_syms_.contains(&sym) || proc_blocking_written_.contains(&sym)) {
    return name;
  }

  if (!declared_.contains(&sym)) {
    declare_value_symbol(sym, /*force_reg=*/false);
  }

  // Drivers emit in dataflow dependency order (lower_members), so wire reads
  // are plain. A combinational-cycle read uses a 2f-defer end-of-cycle read
  // (attr_get .[defer]): tolg connects it to the wire's final driver as a
  // delay-free feedback edge. This is LEC-exact, unlike the old settled read
  // (delay_assign +1, LNAST-tier only, no tolg lowering) — important because
  // most such "cycles" are false positives (e.g. a ready/valid handshake whose
  // dataflow loops through a submodule instance but is not a true comb loop).
  if (in_comb_cycle_) {
    const auto* scope = sym.getParentScope();
    const bool  module_level
        = scope != nullptr && (&scope->asSymbol() == body_ || scope->asSymbol().kind == slang::ast::SymbolKind::GenerateBlock);
    if (module_level) {
      return builder_.create_defer_read_stmts(name);
    }
  }

  return name;
}

std::string Slang_context::booleanize(std::string v) {
  if (is_bool_value(v)) {
    return v;
  }
  // A condition must be a real LNAST bool. A raw 1-bit net read is already 0/1
  // but is still integer-typed, so it cannot be returned verbatim (typecheck
  // rejects an integer condition); `v != 0` yields the bool (and cprop folds
  // the compare away for a 1-bit operand).
  return mark_bool(builder_.create_ne_stmts(v, "0"));
}

std::string Slang_context::lower_unary(const slang::ast::UnaryExpression& expr) {
  const auto& operand = expr.operand();
  auto        oi      = tinfo(*operand.type);
  auto        ti      = tinfo(*expr.type);

  switch (expr.op) {
    case UnaryOperator::Plus: return lower_rvalue(operand);
    case UnaryOperator::Minus: {
      auto v = to_int_value(lower_rvalue(operand));
      return fit_wrap(builder_.create_minus_stmts("0", v), ti.bits, ti.is_signed);
    }
    case UnaryOperator::BitwiseNot: {
      auto v   = to_int_value(lower_rvalue(operand));
      auto neg = builder_.create_bit_not_stmts(v);
      // signed stays in range (~v == -v-1); unsigned needs the pattern wrap
      return ti.is_signed ? neg : trunc_to(neg, ti.bits);
    }
    case UnaryOperator::LogicalNot: {
      auto v = lower_rvalue(operand);
      if (is_bool_value(v)) {
        return mark_bool(builder_.create_log_not_stmts(v));
      }
      return mark_bool(builder_.create_eq_stmts(v, "0"));
    }
    // Reductions: expanded here (operand width is known) instead of relying
    // on tolg lowering for red_* nodes.
    case UnaryOperator::BitwiseOr:  // |v
    case UnaryOperator::BitwiseNor: {
      auto v = to_int_value(lower_rvalue(operand));
      return mark_bool(expr.op == UnaryOperator::BitwiseOr ? builder_.create_ne_stmts(v, "0")
                                                           : builder_.create_eq_stmts(v, "0"));
    }
    case UnaryOperator::BitwiseAnd:  // &v
    case UnaryOperator::BitwiseNand: {
      auto v   = to_pattern(to_int_value(lower_rvalue(operand)), oi.bits, oi.is_signed);
      auto all = mask_text(oi.bits);
      return mark_bool(expr.op == UnaryOperator::BitwiseAnd ? builder_.create_eq_stmts(v, all)
                                                            : builder_.create_ne_stmts(v, all));
    }
    case UnaryOperator::BitwiseXor:  // ^v - parity via shift-halving
    case UnaryOperator::BitwiseXnor: {
      auto v = to_pattern(to_int_value(lower_rvalue(operand)), oi.bits, oi.is_signed);
      for (int k = 32; k >= 1; k /= 2) {
        if (k < oi.bits) {
          v = builder_.create_bit_xor_stmts(v, builder_.create_sra_stmts(v, std::to_string(k)));
        }
      }
      auto parity = builder_.create_bit_and_stmts(v, "1");
      if (expr.op == UnaryOperator::BitwiseXnor) {
        return mark_bool(builder_.create_eq_stmts(parity, "0"));
      }
      return parity;
    }
    case UnaryOperator::Preincrement:
    case UnaryOperator::Predecrement:
    case UnaryOperator::Postincrement:
    case UnaryOperator::Postdecrement: {
      // `x++`/`++x`/`x--`/`--x`: read-modify-write the target. Pre returns the
      // new value, post returns the snapshot of the old value (blocking semantics).
      const bool is_inc = expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Postincrement;
      const bool is_pre = expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Predecrement;
      auto       cur    = to_pattern(to_int_value(lower_rvalue(operand)), oi.bits, oi.is_signed);
      // post-inc/dec returns the OLD value, but the write below re-versions the
      // operand, so snapshot it into a fresh temp first (cprop folds the +0).
      std::string old_snap = is_pre ? std::string{} : builder_.create_plus_stmts(cur, "0");
      auto        nv       = trunc_to(is_inc ? builder_.create_plus_stmts(cur, "1") : builder_.create_minus_stmts(cur, "1"),
                                      oi.bits);
      assign_to(operand, nv);
      return is_pre ? nv : old_snap;
    }
    default:
      emit_unsupported(expr.sourceRange, "unsupported-unary-op",
                       std::string("unary operator '") + std::string(slang::ast::toString(expr.op)) + "' is not supported");
      return "0";
  }
}

std::string Slang_context::lower_binary(const slang::ast::BinaryExpression& expr) {
  const auto& le = expr.left();
  const auto& re = expr.right();
  auto        ti = tinfo(*expr.type);
  auto        li = tinfo(*le.type);
  auto        ri = tinfo(*re.type);

  // Logical ops booleanize their self-determined operands.
  switch (expr.op) {
    case BinaryOperator::LogicalAnd:
      return mark_bool(builder_.create_log_and_stmts(booleanize(lower_rvalue(le)), booleanize(lower_rvalue(re))));
    case BinaryOperator::LogicalOr:
      return mark_bool(builder_.create_log_or_stmts(booleanize(lower_rvalue(le)), booleanize(lower_rvalue(re))));
    case BinaryOperator::LogicalImplication: {
      auto a = booleanize(lower_rvalue(le));
      auto b = booleanize(lower_rvalue(re));
      return mark_bool(builder_.create_log_or_stmts(mark_bool(builder_.create_log_not_stmts(a)), b));
    }
    case BinaryOperator::LogicalEquivalence: {
      auto a = booleanize(lower_rvalue(le));
      auto b = booleanize(lower_rvalue(re));
      return mark_bool(builder_.create_eq_stmts(a, b));
    }
    default: break;
  }

  auto lhs = lower_rvalue(le);
  auto rhs = lower_rvalue(re);

  // Comparisons accept same-kind operands; everything else is integer-only.
  switch (expr.op) {
    case BinaryOperator::Equality:
    case BinaryOperator::Inequality:
    case BinaryOperator::CaseEquality:
    case BinaryOperator::CaseInequality:
      if (is_bool_value(lhs) != is_bool_value(rhs)) {
        lhs = to_int_value(lhs);
        rhs = to_int_value(rhs);
      }
      break;
    default:
      lhs = to_int_value(lhs);
      rhs = to_int_value(rhs);
  }

  switch (expr.op) {
    case BinaryOperator::Add: return fit_wrap(builder_.create_plus_stmts(lhs, rhs), ti.bits, ti.is_signed);
    case BinaryOperator::Subtract: return fit_wrap(builder_.create_minus_stmts(lhs, rhs), ti.bits, ti.is_signed);
    case BinaryOperator::Multiply: return fit_wrap(builder_.create_mult_stmts(lhs, rhs), ti.bits, ti.is_signed);
    case BinaryOperator::Divide:
      if (ti.is_signed) {
        return fit_wrap(builder_.create_div_stmts(lhs, rhs), ti.bits, ti.is_signed);
      }
      return builder_.create_div_stmts(lhs, rhs);
    case BinaryOperator::Mod: {
      // a % b == a - (a/b)*b (LNAST mod has no LGraph lowering)
      auto q = builder_.create_div_stmts(lhs, rhs);
      auto p = builder_.create_mult_stmts(q, rhs);
      return builder_.create_minus_stmts(lhs, p);  // |a%b| < |b| so it fits the type
    }
    case BinaryOperator::BinaryAnd: return builder_.create_bit_and_stmts(lhs, rhs);
    case BinaryOperator::BinaryOr: return builder_.create_bit_or_stmts({lhs, rhs});
    case BinaryOperator::BinaryXor: return builder_.create_bit_xor_stmts(lhs, rhs);
    case BinaryOperator::BinaryXnor: {
      auto x = builder_.create_bit_not_stmts(builder_.create_bit_xor_stmts(lhs, rhs));
      return ti.is_signed ? x : trunc_to(x, ti.bits);
    }
    case BinaryOperator::Equality: return mark_bool(builder_.create_eq_stmts(lhs, rhs));
    case BinaryOperator::Inequality: return mark_bool(builder_.create_ne_stmts(lhs, rhs));
    case BinaryOperator::CaseEquality:
      emit_warning(expr.sourceRange, "case-eq-two-state", "unsupported", "=== is lowered as == (two-state)");
      return mark_bool(builder_.create_eq_stmts(lhs, rhs));
    case BinaryOperator::CaseInequality:
      emit_warning(expr.sourceRange, "case-eq-two-state", "unsupported", "!== is lowered as != (two-state)");
      return mark_bool(builder_.create_ne_stmts(lhs, rhs));
    case BinaryOperator::GreaterThan: return mark_bool(builder_.create_gt_stmts(lhs, rhs));
    case BinaryOperator::GreaterThanEqual: return mark_bool(builder_.create_ge_stmts(lhs, rhs));
    case BinaryOperator::LessThan: return mark_bool(builder_.create_lt_stmts(lhs, rhs));
    case BinaryOperator::LessThanEqual: return mark_bool(builder_.create_le_stmts(lhs, rhs));
    case BinaryOperator::LogicalShiftLeft:
    case BinaryOperator::ArithmeticShiftLeft: {
      auto amount = to_pattern(rhs, ri.bits, ri.is_signed);  // shift amounts are unsigned
      return fit_wrap(builder_.create_shl_stmts(lhs, amount), ti.bits, ti.is_signed);
    }
    case BinaryOperator::LogicalShiftRight: {
      auto amount = to_pattern(rhs, ri.bits, ri.is_signed);
      auto p      = to_pattern(lhs, li.bits, li.is_signed);
      auto r      = builder_.create_sra_stmts(p, amount);
      // k>=1 clears the sign position so the sext is an identity then; it only
      // reinterprets the k==0 passthrough of a signed lhs.
      return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
    }
    case BinaryOperator::ArithmeticShiftRight: {
      auto amount = to_pattern(rhs, ri.bits, ri.is_signed);
      if (li.is_signed) {
        return builder_.create_sra_stmts(lhs, amount);  // arithmetic on the signed value, stays in range
      }
      return builder_.create_sra_stmts(lhs, amount);  // unsigned >>> == >>
    }
    case BinaryOperator::Power:
      emit_unsupported(expr.sourceRange, "unsupported-power", "non-constant ** is not supported by --reader slang");
      return "0";
    case BinaryOperator::WildcardEquality:
    case BinaryOperator::WildcardInequality: {
      auto cv = try_eval(re);
      if (cv && cv->isInteger()) {
        const auto& sv    = cv->integer();
        auto        nbits = static_cast<int>(sv.getBitWidth());
        if (nbits <= 64) {
          uint64_t mask = 0, val = 0;
          for (int i = 0; i < nbits; ++i) {
            auto b = sv[i];
            if (b.isUnknown()) {
              continue;
            }
            mask |= 1ULL << i;
            if (b.value != 0) {
              val |= 1ULL << i;
            }
          }
          auto lp     = to_pattern(lhs, li.bits, li.is_signed);
          auto masked = builder_.create_bit_and_stmts(lp, std::to_string(mask));
          auto m = mark_bool(builder_.create_eq_stmts(masked, std::to_string(val)));
          return mark_bool(expr.op == BinaryOperator::WildcardEquality ? m : builder_.create_log_not_stmts(m));
        }
      }
      emit_unsupported(expr.sourceRange, "unsupported-wildcard-eq", "==?/!=? needs a constant pattern of <= 64 bits");
      return "0";
    }
    default:
      emit_unsupported(expr.sourceRange, "unsupported-binary-op",
                       std::string("binary operator '") + std::string(slang::ast::toString(expr.op)) + "' is not supported");
      return "0";
  }
}

std::string Slang_context::lower_conditional_expr(const slang::ast::ConditionalExpression& expr) {
  std::string cond;
  for (const auto& c : expr.conditions) {
    if (c.pattern != nullptr) {
      emit_unsupported(expr.sourceRange, "unsupported-pattern", "pattern matching in ?: is not supported");
      return "0";
    }
    auto v = booleanize(lower_rvalue(*c.expr));
    cond   = cond.empty() ? v : builder_.create_log_and_stmts(cond, v);
  }

  // Hoist both arm values above the if (hardware evaluates both anyway) and
  // fit them to the expression type: the merge mux's width comes from the
  // variable's first store, so the seed must already be type-wide.
  auto ti = tinfo(*expr.type);
  auto a  = fit_wrap(to_int_value(lower_rvalue(expr.left())), ti.bits, ti.is_signed);
  auto b  = fit_wrap(to_int_value(lower_rvalue(expr.right())), ti.bits, ti.is_signed);

  // a fresh non-`___` local: the `___` namespace is single-write SSA, and
  // this temp is written more than once.
  auto tmp = fresh_local("mux");
  builder_.create_declare_stmts(tmp, "mut", "", "");  // rangeless: arms may carry x
  builder_.create_assign_stmts(tmp, b);               // else value seeds the width

  auto if_nid = builder_.create_if_stmt(false);
  builder_.add_if_cond(if_nid, cond);
  auto then_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(then_stmts);
  builder_.create_assign_stmts(tmp, a);
  builder_.pop_stmts();

  return tmp;
}

std::string Slang_context::lower_assignment_pattern(const slang::ast::Expression&                     expr,
                                                    std::span<const slang::ast::Expression* const> elems) {
  // `T'{...}` for a packed (integral) struct/array: slang resolves `elements()`
  // positionally MSB-first, so the value is just the fields concatenated — same
  // bit layout as a `{...}` concat of those fields. Unpacked targets (memories /
  // unpacked-array vars) are a different lowering and stay unsupported here.
  if (!expr.type->isIntegral()) {
    emit_unsupported(expr.sourceRange, "unsupported-assignment-pattern",
                     "only packed (integral) '{...} assignment patterns are supported by --reader slang yet");
    return "0";
  }
  std::vector<std::string> parts;
  int64_t                  offset = 0;
  for (auto it = elems.rbegin(); it != elems.rend(); ++it) {  // LSB-first accumulation
    const auto& e  = **it;
    auto        oi = tinfo(*e.type);
    auto        v  = to_pattern(to_int_value(lower_rvalue(e)), oi.bits, oi.is_signed);
    parts.emplace_back(offset == 0 ? v : builder_.create_shl_stmts(v, std::to_string(offset)));
    offset += oi.bits;
  }
  if (parts.empty()) {
    return "0";
  }
  return builder_.create_bit_or_stmts(parts);
}

std::string Slang_context::lower_concat(const slang::ast::ConcatenationExpression& expr) {
  // {a, b, c}: the FIRST operand is the MSB block.
  auto                     ops = expr.operands();
  std::vector<std::string> parts;
  int64_t                  offset = 0;
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    const auto& e  = **it;
    auto        oi = tinfo(*e.type);
    auto        v  = to_pattern(to_int_value(lower_rvalue(e)), oi.bits, oi.is_signed);
    parts.emplace_back(offset == 0 ? v : builder_.create_shl_stmts(v, std::to_string(offset)));
    offset += oi.bits;
  }
  if (parts.empty()) {
    return "0";
  }
  return builder_.create_bit_or_stmts(parts);
}

// Shared base+offset math for packed element/range selects and member access.
// Returns the lowered value with the selected field shifted down to bit 0.
std::string Slang_context::lower_select(const slang::ast::Expression& expr) {
  auto ti = tinfo(*expr.type);

  if (expr.kind == ExpressionKind::MemberAccess) {
    const auto& ma = expr.as<slang::ast::MemberAccessExpression>();
    if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
      emit_unsupported(expr.sourceRange, "unsupported-member-access", "only packed-struct field access is supported");
      return "0";
    }
    const auto& field = ma.member.as<slang::ast::FieldSymbol>();
    auto        bi    = tinfo(*ma.value().type);
    auto        p     = to_pattern(to_int_value(lower_rvalue(ma.value())), bi.bits, bi.is_signed);
    auto lo = static_cast<int64_t>(field.bitOffset);
    auto r  = extract_field(p, lo, ti.bits);
    return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
  }

  const auto& base    = expr.kind == ExpressionKind::ElementSelect ? expr.as<slang::ast::ElementSelectExpression>().value()
                                                                   : expr.as<slang::ast::RangeSelectExpression>().value();
  const auto& base_ty = base.type->getCanonicalType();

  if (base_ty.isUnpackedArray()) {
    return lower_unpacked_read(expr);
  }

  if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
    emit_unsupported(expr.sourceRange, "unsupported-select-base", "selects are only supported on packed integral values");
    return "0";
  }

  auto range  = base_ty.getFixedRange();
  auto bi     = tinfo(base_ty);
  int  stride = 1;
  if (base_ty.isPackedArray()) {
    stride = static_cast<int>(base_ty.getArrayElementType()->getBitWidth());
  }

  auto p = to_pattern(to_int_value(lower_rvalue(base)), bi.bits, bi.is_signed);

  // (selected width in bits, low element index normalized to 0-based)
  int                    sel_bits = ti.bits;
  std::optional<int64_t> const_low;
  std::string            dyn_low;  // 0-based element index expression

  auto normalize = [&](const slang::ast::Expression& idx, int64_t width_down,
                       int64_t width_up) -> std::pair<std::optional<int64_t>, std::string> {
    // bottom element of the selection, 0-based from the LSB end
    if (auto ci = try_eval_int(idx)) {
      int64_t bottom = range.isDescending() ? (*ci - range.lower() - (width_down - 1))
                                              : (range.upper() - *ci - (width_up - 1));
      return {bottom, {}};
    }
    auto v = to_int_value(lower_rvalue(idx));  // selector value (settled rules apply)
    if (range.isDescending()) {
      int64_t bias = range.lower() + (width_down - 1);
      return {std::nullopt, bias == 0 ? v : builder_.create_minus_stmts(v, std::to_string(bias))};
    }
    int64_t bias = range.upper() - (width_up - 1);
    return {std::nullopt, builder_.create_minus_stmts(std::to_string(bias), v)};
  };

  if (expr.kind == ExpressionKind::ElementSelect) {
    const auto& es           = expr.as<slang::ast::ElementSelectExpression>();
    std::tie(const_low, dyn_low) = normalize(es.selector(), 1, 1);
  } else {
    const auto& rs = expr.as<slang::ast::RangeSelectExpression>();
    using slang::ast::RangeSelectionKind;
    auto kind = rs.getSelectionKind();
    if (kind == RangeSelectionKind::Simple) {
      auto l = try_eval_int(rs.left());
      auto r = try_eval_int(rs.right());
      if (!l || !r) {
        emit_error(expr.sourceRange, "non-const-range", "syntax", "simple range bounds must be compile-time constants");
        return "0";
      }
      int64_t lo_idx = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
      const_low      = lo_idx;
    } else {
      int64_t w = sel_bits / stride;  // element count of the slice
      if (kind == RangeSelectionKind::IndexedUp) {
        // [e +: W]: e is the low (little-endian) / first-declared (big) element
        std::tie(const_low, dyn_low) = normalize(rs.left(), 1, w);
      } else {  // IndexedDown [e -: W]: e is the high element
        std::tie(const_low, dyn_low) = normalize(rs.left(), w, 1);
      }
    }
  }

  if (const_low) {
    int64_t lo_bit = *const_low * stride;
    if (lo_bit < 0 || lo_bit + sel_bits > bi.bits) {
      emit_warning(expr.sourceRange, "select-out-of-range", "bitwidth", "constant select is out of the declared range");
      lo_bit = std::max<int64_t>(lo_bit, 0);
    }
    auto r = extract_field(p, lo_bit, sel_bits);
    return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
  }

  // dynamic: shift the value down, then constant-mask. An indexed select can
  // reach below the declared range (x territory); bias both sides so the
  // shift amount stays non-negative for any in-or-near-range index.
  std::string shamt = dyn_low;
  if (stride != 1) {
    shamt = builder_.create_mult_stmts(shamt, std::to_string(stride));
  }
  const int bias = sel_bits;
  shamt          = builder_.create_plus_stmts(shamt, std::to_string(bias));
  auto shifted = builder_.create_sra_stmts(builder_.create_shl_stmts(p, std::to_string(bias)), shamt);
  auto r       = trunc_to(shifted, sel_bits);
  return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
}

std::string Slang_context::lower_call(const slang::ast::CallExpression& expr) {
  if (expr.isSystemCall()) {
    auto name = expr.getSubroutineName();
    auto args = expr.arguments();
    if ((name == "$signed" || name == "$unsigned") && args.size() == 1) {
      const auto& a  = *args[0];
      auto        ai = tinfo(*a.type);
      auto        v  = to_int_value(lower_rvalue(a));
      if (name == "$signed") {
        auto p = to_pattern(v, ai.bits, ai.is_signed);
        return builder_.create_sext_stmts(p, std::to_string(ai.bits - 1));
      }
      return to_pattern(v, ai.bits, ai.is_signed);
    }
    if (name == "$countones" && args.size() == 1) {
      const auto& a  = *args[0];
      auto        ai = tinfo(*a.type);
      auto        v  = to_pattern(to_int_value(lower_rvalue(a)), ai.bits, ai.is_signed);
      // popcount via per-bit sum is wasteful for wide values; cap it.
      if (ai.bits > 64) {
        emit_unsupported(expr.sourceRange, "wide-countones", "$countones on values wider than 64 bits");
        return "0";
      }
      std::string acc = builder_.create_bit_and_stmts(v, "1");
      for (int i = 1; i < ai.bits; ++i) {
        auto bit = builder_.create_bit_and_stmts(builder_.create_sra_stmts(v, std::to_string(i)), "1");
        acc      = builder_.create_plus_stmts(acc, bit);
      }
      return acc;
    }
    // constant system calls ($clog2, $bits, ...) fold in tier 1
    emit_unsupported(expr.sourceRange, "unsupported-system-call",
                     std::string("system call '") + std::string(name) + "' is not supported by --reader slang");
    return "0";
  }

  // User function: inline the body for synthesizable, input-only functions.
  const auto* sub = std::get<const slang::ast::SubroutineSymbol*>(expr.subroutine);
  if (sub != nullptr && sub->subroutineKind == slang::ast::SubroutineKind::Function && sub->returnValVar != nullptr
      && !expr.hasOutputArgs() && !sub->flags.has(slang::ast::MethodFlags::DPIImport)) {
    return inline_call(expr, *sub);
  }

  emit_unsupported(expr.sourceRange, "unsupported-function-call",
                   std::string("call to '") + std::string(expr.getSubroutineName())
                       + "' is not supported by --reader slang yet (only compile-time evaluable functions fold)");
  return "0";
}

std::string Slang_context::inline_call(const slang::ast::CallExpression& expr, const slang::ast::SubroutineSymbol& sub) {
  if (inline_depth_ > 32) {
    emit_unsupported(expr.sourceRange, "unsupported-function-call",
                     std::string("call to '") + std::string(sub.name) + "' exceeds the inline-recursion limit");
    return "0";
  }
  auto formals = sub.getArguments();
  auto actuals = expr.arguments();
  if (formals.size() != actuals.size()) {
    emit_unsupported(expr.sourceRange, "unsupported-function-call", "function argument count mismatch");
    return "0";
  }

  // Snapshot all actual-argument values BEFORE binding the formals (the formals
  // are shared symbols across call sites, so binding first could alias).
  std::vector<std::string> argv;
  argv.reserve(actuals.size());
  for (const auto* a : actuals) {
    argv.push_back(lower_rvalue(*a));
  }
  for (size_t i = 0; i < formals.size(); ++i) {
    const auto& fa = *formals[i];
    if (fa.direction != slang::ast::ArgumentDirection::In) {
      emit_unsupported(expr.sourceRange, "unsupported-function-call",
                       "only pure input-argument functions can be inlined by --reader slang");
      return "0";
    }
    declare_value_symbol(fa, /*force_reg=*/false);
    note_write(fa, /*nonblocking=*/false, expr.sourceRange.start());
    builder_.create_assign_stmts(lname_of(fa), argv[i]);
  }

  const auto& rv = *sub.returnValVar;
  declare_value_symbol(rv, /*force_reg=*/false);

  // Lower the body with a function-return context active (Return assigns `rv`).
  // Returns that are terminal per branch merge correctly through the existing
  // branch machinery; mid-block early returns are not modeled.
  bool        saved_in  = in_function_call_;
  const auto* saved_ret = func_ret_sym_;
  in_function_call_     = true;
  func_ret_sym_         = &rv;
  ++inline_depth_;
  lower_statement(sub.getBody());
  --inline_depth_;
  in_function_call_ = saved_in;
  func_ret_sym_     = saved_ret;

  return read_symbol(rv, expr.sourceRange);
}
