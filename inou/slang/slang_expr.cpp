//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Rvalue expression lowering (todo/ 2s subtask B). Two-tier constant
// evaluation: slang's own expr.eval() first (parameters, genvars, loop-var
// locals bound in eval_ctx_ fold here), structural LNAST lowering only for
// runtime values. Every lowered value satisfies the invariant that it sits
// in the integer range of its slang type; conversions go through the single
// materialize_conversion seam (slang_types.cpp).

#include "absl/strings/str_cat.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::BinaryOperator;
using slang::ast::ExpressionKind;
using slang::ast::UnaryOperator;

const slang::ast::PackageSymbol* Slang_context::owning_package(const slang::ast::Symbol& sym) {
  // Walk out to the owning PACKAGE (a package localparam/parameter/enum member).
  // A module-local symbol has no stable package home, so callers keep folding it.
  const slang::ast::Scope* sc = sym.getParentScope();
  while (sc != nullptr) {
    const auto& ssym = sc->asSymbol();
    if (ssym.kind == slang::ast::SymbolKind::Package) {
      return &ssym.as<slang::ast::PackageSymbol>();
    }
    sc = ssym.getParentScope();
  }
  return nullptr;
}

const slang::ConstantValue* Slang_context::package_const_value(const slang::ast::Symbol& sym) {
  if (sym.kind == slang::ast::SymbolKind::Parameter) {
    return &sym.as<slang::ast::ParameterSymbol>().getValue();
  }
  if (sym.kind == slang::ast::SymbolKind::EnumValue) {
    return &sym.as<slang::ast::EnumValueSymbol>().getValue();
  }
  return nullptr;
}

std::optional<std::pair<std::string, int64_t>> Slang_context::render_const_expr(
    const slang::ast::Expression& e, const slang::ast::PackageSymbol* home, std::set<std::string>& imports_out,
    std::vector<std::pair<const slang::ast::PackageSymbol*, std::string>>& refs_out) {
  switch (e.kind) {
    case ExpressionKind::IntegerLiteral: {
      auto cv = try_eval(e);
      if (!cv || !cv->isInteger()) {
        return std::nullopt;
      }
      auto v = cv->integer().as<int64_t>();
      if (!v) {
        return std::nullopt;
      }
      return std::make_pair(const_text(cv->integer()), *v);
    }
    case ExpressionKind::NamedValue:
    case ExpressionKind::HierarchicalValue: {
      const auto& sym = e.as<slang::ast::ValueExpressionBase>().symbol;
      const auto* cv  = package_const_value(sym);
      if (cv == nullptr || !cv->isInteger()) {
        return std::nullopt;
      }
      auto v = cv->integer().as<int64_t>();
      if (!v) {
        return std::nullopt;
      }
      const auto* pkg = owning_package(sym);
      if (pkg == nullptr) {
        return std::nullopt;
      }
      refs_out.emplace_back(pkg, std::string(sym.name));
      if (pkg == home) {
        return std::make_pair(std::string(sym.name), *v);
      }
      imports_out.insert(std::string(pkg->name));
      return std::make_pair(absl::StrCat(pkg->name, ".", sym.name), *v);
    }
    case ExpressionKind::UnaryOp: {
      const auto& u = e.as<slang::ast::UnaryExpression>();
      auto        r = render_const_expr(u.operand(), home, imports_out, refs_out);
      if (!r) {
        return std::nullopt;
      }
      if (u.op == UnaryOperator::Plus) {
        return r;
      }
      if (u.op == UnaryOperator::Minus && r->second != INT64_MIN) {
        return std::make_pair(absl::StrCat("(-", r->first, ")"), -r->second);
      }
      return std::nullopt;  // ~ is width-bound in SV — not value-faithful unbounded
    }
    case ExpressionKind::BinaryOp: {
      const auto& b = e.as<slang::ast::BinaryExpression>();
      auto        l = render_const_expr(b.left(), home, imports_out, refs_out);
      if (!l) {
        return std::nullopt;
      }
      auto r = render_const_expr(b.right(), home, imports_out, refs_out);
      if (!r) {
        return std::nullopt;
      }
      __int128    wide = 0;
      const char* op   = nullptr;
      switch (b.op) {
        case BinaryOperator::Add:
          op   = "+";
          wide = static_cast<__int128>(l->second) + r->second;
          break;
        case BinaryOperator::Subtract:
          op   = "-";
          wide = static_cast<__int128>(l->second) - r->second;
          break;
        case BinaryOperator::Multiply:
          op   = "*";
          wide = static_cast<__int128>(l->second) * r->second;
          break;
        case BinaryOperator::LogicalShiftLeft:
          if (r->second < 0 || r->second > 62) {
            return std::nullopt;
          }
          op   = "<<";
          wide = static_cast<__int128>(l->second) << r->second;
          break;
        case BinaryOperator::LogicalShiftRight:
        case BinaryOperator::ArithmeticShiftRight:
          // pyrope >> is arithmetic; a logical shift of a NEGATIVE fixed-width
          // value differs, so only pass non-negative lhs through as logical.
          if (r->second < 0 || r->second > 62 || (b.op == BinaryOperator::LogicalShiftRight && l->second < 0)) {
            return std::nullopt;
          }
          op   = ">>";
          wide = l->second >> r->second;
          break;
        default: return std::nullopt;  // /, %, &, |, … : width/sign semantics not value-faithful
      }
      if (wide < INT64_MIN || wide > INT64_MAX) {
        return std::nullopt;
      }
      return std::make_pair(absl::StrCat("(", l->first, " ", op, " ", r->first, ")"), static_cast<int64_t>(wide));
    }
    case ExpressionKind::Conversion: {
      auto inner = render_const_expr(e.as<slang::ast::ConversionExpression>().operand(), home, imports_out, refs_out);
      if (!inner) {
        return std::nullopt;
      }
      auto whole = try_eval(e);
      if (!whole || !whole->isInteger()) {
        return std::nullopt;
      }
      auto wv = whole->integer().as<int64_t>();
      if (!wv || *wv != inner->second) {
        return std::nullopt;  // value-changing cast — fold instead
      }
      return inner;
    }
    default: return std::nullopt;  // $clog2, concat, replication, … : fold
  }
}

std::optional<std::string> Slang_context::package_symbol_ref(const slang::ast::Symbol& sym) {
  if (!options_.preserve_param_provenance) {
    return std::nullopt;  // WIP feature; default OFF keeps folding (no regression)
  }
  // A MODULE-LOCAL param preserved as a body-level `comptime const` keeps its
  // bare name (emit_local_param_consts declared it at body top).
  if (auto lit = local_param_lname_.find(&sym); lit != local_param_lname_.end()) {
    return lit->second;
  }
  const auto* cv = package_const_value(sym);
  if (cv == nullptr || !cv->isInteger()) {
    return std::nullopt;  // only integral consts carry a scalar pyrope value
  }
  if (cv->integer().hasUnknown()) {
    // A parameter with X/Z don't-care bits (an instruction-encoding mask):
    // fold the 4-state literal inline instead of a symbolic `pkg.PARAM` — the
    // recompile's import machinery cannot materialize an unknown-bit pub
    // const as a hardware driver.
    return std::nullopt;
  }
  const auto* pkg = owning_package(sym);
  if (pkg == nullptr) {
    return std::nullopt;
  }
  std::string pkg_name(pkg->name);
  std::string param_name(sym.name);
  referenced_pkg_params_[pkg_name][param_name] = const_text(cv->integer());
  referenced_pkg_syms_[pkg_name]               = pkg;
  builder_.lnast->add_imported_package(pkg_name);
  return absl::StrCat(pkg_name, ".", param_name);
}

std::optional<std::string> Slang_context::package_param_ref(const slang::ast::Expression& expr) {
  if (!options_.preserve_param_provenance) {
    return std::nullopt;
  }
  // Peel conversions (an implicit width cast around the bare ref); whether they
  // were value-preserving is checked against the whole expression's value below.
  const slang::ast::Expression* e      = &expr;
  bool                          peeled = false;
  while (e->kind == ExpressionKind::Conversion) {
    e      = &e->as<slang::ast::ConversionExpression>().operand();
    peeled = true;
  }
  if (e->kind != ExpressionKind::NamedValue && e->kind != ExpressionKind::HierarchicalValue) {
    return std::nullopt;
  }
  const auto& sym = e->as<slang::ast::ValueExpressionBase>().symbol;
  if (peeled) {
    // Peeling is only sound for VALUE-PRESERVING conversions: a narrowing cast
    // like 4'(P) with P=300 evaluates to 12, and emitting the symbolic ref
    // would read back as 300 — a miscompile. Compare the whole expression's
    // folded value against the bare symbol's; any mismatch keeps the fold (the
    // structural Conversion lowering preserves those symbolically instead).
    const auto* cv = package_const_value(sym);
    if (cv == nullptr || !cv->isInteger()) {
      return std::nullopt;
    }
    auto whole = try_eval(expr);
    if (!whole || !whole->isInteger() || const_text(whole->integer()) != const_text(cv->integer())) {
      return std::nullopt;
    }
  }
  return package_symbol_ref(sym);
}

bool Slang_context::contains_package_param(const slang::ast::Expression& expr) {
  bool found = false;
  auto check = [&found](const slang::ast::Symbol& sym) {
    if (!found && (sym.kind == slang::ast::SymbolKind::Parameter || sym.kind == slang::ast::SymbolKind::EnumValue)
        && owning_package(sym) != nullptr) {
      found = true;
    }
  };
  auto v = slang::ast::makeVisitor([&](auto&, const slang::ast::NamedValueExpression& e) { check(e.symbol); },
                                   [&](auto&, const slang::ast::HierarchicalValueExpression& e) { check(e.symbol); });
  expr.visit(v);
  return found;
}

bool Slang_context::structural_preserve_ok(const slang::ast::Expression& expr) {
  switch (expr.kind) {
    case ExpressionKind::NamedValue:
    case ExpressionKind::HierarchicalValue: return true;  // read_symbol preserves (or folds cleanly)
    case ExpressionKind::UnaryOp          : {
      using slang::ast::UnaryOperator;
      switch (expr.as<slang::ast::UnaryExpression>().op) {
        case UnaryOperator::Plus:
        case UnaryOperator::Minus:
        case UnaryOperator::BitwiseNot:
        case UnaryOperator::BitwiseAnd:
        case UnaryOperator::BitwiseOr:
        case UnaryOperator::BitwiseXor:
        case UnaryOperator::BitwiseNand:
        case UnaryOperator::BitwiseNor:
        case UnaryOperator::BitwiseXnor:
        case UnaryOperator::LogicalNot : return true;
        default                        : return false;  // ++/-- cannot be const anyway
      }
    }
    case ExpressionKind::BinaryOp:
      // Every binop lowers structurally EXCEPT `**` (const power only exists
      // via the tier-1 fold).
      return expr.as<slang::ast::BinaryExpression>().op != BinaryOperator::Power;
    case ExpressionKind::ConditionalOp: return true;
    case ExpressionKind::Conversion   : {
      const auto& conv = expr.as<slang::ast::ConversionExpression>();
      return conv.type->isIntegral() && conv.operand().type->isIntegral();
    }
    case ExpressionKind::Concatenation:
    case ExpressionKind::Replication  : return true;  // a const replication has a const count
    case ExpressionKind::ElementSelect: return expr.as<slang::ast::ElementSelectExpression>().value().type->isIntegral();
    case ExpressionKind::RangeSelect  : return expr.as<slang::ast::RangeSelectExpression>().value().type->isIntegral();
    case ExpressionKind::MemberAccess:
      // only packed-struct member access lowers; packed structs are integral
      return expr.as<slang::ast::MemberAccessExpression>().value().type->isIntegral();
    case ExpressionKind::Call: {
      const auto& call = expr.as<slang::ast::CallExpression>();
      if (!call.isSystemCall()) {
        return false;
      }
      auto name = call.getSubroutineName();
      return name == "$signed" || name == "$unsigned";  // $clog2/$bits/… need the fold
    }
    default: return false;  // assignment patterns, inside, streaming, … keep the fold
  }
}

std::string Slang_context::lower_rvalue(const slang::ast::Expression& expr) {
  // Provenance: a bare PACKAGE-parameter reference keeps its name (`pkg.PARAM`)
  // instead of folding to a literal — BEFORE the tier-1 fold that would erase it.
  if (auto pref = package_param_ref(expr)) {
    return *pref;
  }
  // Tier 1: compile-time constant (parameters, localparams, genvars, unrolled
  // loop variables, sized literals, $clog2/$bits/... system calls).
  if (expr.kind != ExpressionKind::Assignment && expr.kind != ExpressionKind::LValueReference) {
    if (auto cv = try_eval(expr); cv && cv->isInteger()) {
      // Provenance: a CONST composite containing a package-param leaf (e.g.
      // `PKG_A + PKG_B`, `{5'b0, $unsigned(PKG_P)}`) skips the fold and lowers
      // structurally, so the recursion reaches the bare leaves where
      // package_param_ref keeps the names. Only when every dispatched
      // sub-lowering is supported (structural_preserve_ok) — a kind the
      // structural path cannot lower ($clog2, `**`, …) keeps the fold, and so
      // does any such sub-expression on its own recursion step.
      if (!(options_.preserve_param_provenance && contains_package_param(expr) && structural_preserve_ok(expr))) {
        return const_text(cv->integer());
      }
    }
  }

  switch (expr.kind) {
    case ExpressionKind::NamedValue:
    case ExpressionKind::HierarchicalValue: {
      // A HierarchicalValue (e.g. `stage[i-1].acc` into a named generate block)
      // is, after unrolling/const-folding, just a ValueExpressionBase whose
      // .symbol slang already resolved to the target instance's member. Its
      // lname is cached (lname_of) at the prefix where the member was declared,
      // and Dep_collector records the cross-block read, so the dependency sort
      // emits the producing block first and the read is a plain wire.
      const auto& nv = expr.as<slang::ast::ValueExpressionBase>();
      return read_symbol(nv.symbol, expr.sourceRange);
    }
    case ExpressionKind::UnaryOp      : return lower_unary(expr.as<slang::ast::UnaryExpression>());
    case ExpressionKind::BinaryOp     : return lower_binary(expr.as<slang::ast::BinaryExpression>());
    case ExpressionKind::ConditionalOp: return lower_conditional_expr(expr.as<slang::ast::ConditionalExpression>());
    case ExpressionKind::Conversion   : {
      const auto& conv = expr.as<slang::ast::ConversionExpression>();
      const auto& from = *conv.operand().type;
      const auto& to   = *conv.type;
      // A streaming concatenation is not an integral TYPE in slang's model, but
      // it lowers to a plain integral value here (`{<<8{x}}` is a byte swap of
      // x), so it must not be refused as a non-integral conversion.
      if (conv.operand().kind == ExpressionKind::Streaming) {
        const auto& sc = conv.operand().as<slang::ast::StreamingConcatenationExpression>();
        auto        sv = lower_streaming(sc);
        auto        ti = tinfo(to);
        return materialize_conversion(sv, static_cast<int>(sc.getBitstreamWidth()), false, ti.bits, ti.is_signed);
      }
      if (!to.isIntegral() || !from.isIntegral()) {
        emit_unsupported(expr.sourceRange, "unsupported-conversion", "only integral conversions are supported by --reader slang");
        return "0";
      }
      auto v  = to_int_value(lower_rvalue(conv.operand()));
      auto fi = tinfo(from);
      auto ti = tinfo(to);
      return materialize_conversion(v, fi.bits, fi.is_signed, ti.bits, ti.is_signed, value_width(conv.operand()));
    }
    case ExpressionKind::Concatenation: return lower_concat(expr.as<slang::ast::ConcatenationExpression>());
    case ExpressionKind::Replication  : {
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
      // `{N{bit}}` is a ubiquitous mask idiom. Keep it as one constant-select
      // mux instead of N shifted copies joined by an OR tower. Besides being a
      // much smaller LNAST, this preserves the simple control/data split that
      // cprop and formal cone solvers exploit (Minion's divider uses several
      // 65/66-bit masks in one accumulator transition).
      if (oi.bits == 1 && *count > 1) {
        const int  bits = static_cast<int>(*count);
        const auto out  = fresh_local("rep");
        const auto cond = booleanize(v);  // materialize before the if node so it is in scope in the condition
        builder_.create_declare_stmts(out, "mut", int_max_str(bits, false), int_min_str(bits, false));
        builder_.create_assign_stmts(out, "0");
        auto if_nid = builder_.create_if_stmt(false);
        builder_.add_if_cond(if_nid, cond);
        auto then_stmts = builder_.add_if_stmts(if_nid);
        builder_.push_stmts(then_stmts);
        builder_.create_assign_stmts(out, Dlop::get_mask_value(bits)->to_pyrope());
        builder_.pop_stmts();
        return out;
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
    case ExpressionKind::MemberAccess : return lower_select(expr);
    case ExpressionKind::Call         : return lower_call(expr.as<slang::ast::CallExpression>());
    case ExpressionKind::SimpleAssignmentPattern:
      return lower_assignment_pattern(expr, expr.as<slang::ast::SimpleAssignmentPatternExpression>().elements());
    case ExpressionKind::StructuredAssignmentPattern: {
      const auto& sap = expr.as<slang::ast::StructuredAssignmentPatternExpression>();
      // slang's forFixedArray fills elements() ASCENDING (range.lower() ->
      // range.upper()) while everything that consumes them — its own evalImpl's
      // SVInt::concat, and lower_assignment_pattern below — reads elements()[0]
      // as the MSB. The two only disagree when the elements DIFFER, i.e. when an
      // `index:` key is present on a descending array: slang folds
      // `logic [3:0][7:0] p = '{2: 8'hAA, default: '0}` to 0x0000_aa00, where
      // verilator and the LRM (p[2] is bits [23:16]) say 0x00aa_0000. Refuse that
      // shape instead of emitting the plausible-looking wrong bus — and refuse it
      // here rather than "correcting" the order, since slang const-folds the same
      // pattern in a localparam and the two spellings would then disagree.
      // `default:`/`type:`-only patterns make every element identical, so their
      // order cannot matter and they lower fine.
      if (!sap.indexSetters.empty()) {
        const auto& ct = expr.type->getCanonicalType();
        if (ct.hasFixedRange() && ct.getFixedRange().isDescending() && sap.elements().size() > 1) {
          emit_unsupported(expr.sourceRange,
                           "unsupported-assignment-pattern",
                           "`index:` keys in a '{...} pattern over a descending packed array are not supported by --reader slang");
          return "0";
        }
      }
      return lower_assignment_pattern(expr, sap.elements());
    }
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
      emit_unsupported(expr.sourceRange,
                       "expression-assignment",
                       "assignments inside expressions are not supported by --reader slang");
      return "0";
    case ExpressionKind::Streaming: return lower_streaming(expr.as<slang::ast::StreamingConcatenationExpression>());
    default                       : break;
  }

  // CIRCT-style default fallback: nothing slips through silently.
  emit_unsupported(
      expr.sourceRange,
      "unsupported-expression",
      std::string("expression kind '") + std::string(slang::ast::toString(expr.kind)) + "' is not supported by --reader slang yet");
  return "0";
}

std::string Slang_context::read_symbol(const slang::ast::ValueSymbol& sym, slang::SourceRange range) {
  // Parameters / enum values / genvars should have folded in tier 1; if eval
  // failed (e.g. inside an uninstantiated context) report cleanly.
  if (sym.kind == slang::ast::SymbolKind::Parameter) {
    if (auto pref = package_symbol_ref(sym)) {
      return *pref;  // provenance: direct read_symbol callers bypass lower_rvalue's hook
    }
    const auto& cv = sym.as<slang::ast::ParameterSymbol>().getValue(range);
    if (cv.isInteger()) {
      return const_text(cv.integer());
    }
    emit_error(range, "non-integer-parameter", "type", std::string("parameter '") + std::string(sym.name) + "' is not integral");
    return "0";
  }
  if (sym.kind == slang::ast::SymbolKind::EnumValue) {
    if (auto pref = package_symbol_ref(sym)) {
      return *pref;
    }
    const auto& cv = sym.as<slang::ast::EnumValueSymbol>().getValue(range);
    if (cv.isInteger()) {
      return const_text(cv.integer());
    }
  }

  // A whole read of a per-field bundle struct OR a per-element bundle array:
  // reconstruct the packed value from its leaves (field/element accesses are
  // intercepted before reaching here).
  if (is_scalar_struct_var(sym)) {
    return read_struct_whole(sym);
  }

  // M7: a whole read of a BUNDLE port (bare port in casts/compares/concat/
  // whole-copies/instance actuals) reassembles the flat value from the field
  // tuple_gets. MUST beat the input fast-path below — the bare port name is a
  // tuple, not an integer value.
  if (bundle_port_of(sym) != nullptr) {
    return read_bundle_port_whole(sym);
  }

  // Whole read of a per-field TUPLE memory (`meta_t x [N]` — detupled into
  // per-field arrays `x.field:[N]`): the flat base net does not exist, so
  // reassemble element-by-element, field-by-field (element k at bit k*elem_bits,
  // matching the unpacked flat-port convention). Bounded so a whole-read of a
  // genuinely large memory does not explode into thousands of nodes.
  if (auto mit = mem_info_.find(&sym); mit != mem_info_.end() && mit->second.is_tuple && !mit->second.fields.empty()
                                       && mit->second.size > 0 && mit->second.size <= 64) {
    const auto               mi   = mit->second;  // copy: builder calls below can rehash mem_info_
    auto                     base = lname_of(sym);
    std::vector<std::string> parts;
    for (int64_t e = 0; e < mi.size; ++e) {
      for (const auto& f : mi.fields) {
        auto          d   = to_pattern(to_int_value(emit_field_read_chain(base, std::to_string(e), f.name)), f.bits, false);
        const int64_t off = e * mi.elem_bits + f.off;
        parts.push_back(off == 0 ? d : builder_.create_shl_stmts(d, std::to_string(off)));
      }
    }
    if (!parts.empty()) {
      return builder_.create_bit_or_stmts(parts);
    }
  }

  auto name = lname_of(sym);

  if (input_syms_.contains(&sym) || reg_syms_.contains(&sym) || proc_blocking_written_.contains(&sym)) {
    return name;
  }

  if (!declared_.contains(&sym)) {
    declare_value_symbol(sym, /*force_reg=*/false);
  }

  // Drivers emit in dataflow dependency order (lower_members), so most reads are
  // plain. A combinational-cycle net is declared a `wire` (2c-wire): its reads
  // are position-independent, so a forward reference (a read before the driver
  // emits) binds to the resolved net — just read it by name. tolg wires the
  // net's single driver as a delay-free feedback edge (LEC-exact). Most such
  // "cycles" are false positives (e.g. a ready/valid handshake whose dataflow
  // loops through a submodule instance but is not a true comb loop); a real comb
  // loop surfaces as a tolg error.
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
    case UnaryOperator::Plus : return lower_rvalue(operand);
    case UnaryOperator::Minus: {
      auto v   = to_int_value(lower_rvalue(operand));
      auto neg = builder_.create_minus_stmts("0", v);
      // -x needs eff_width(x)+1 signed bits. Unsigned negation always wraps, so
      // only the signed case may skip.
      if (ti.is_signed) {
        if (auto w = value_width(operand); w && *w + 1 <= ti.bits) {
          return neg;
        }
      }
      return fit_wrap(neg, ti.bits, ti.is_signed);
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
      return mark_bool(expr.op == UnaryOperator::BitwiseOr ? builder_.create_ne_stmts(v, "0") : builder_.create_eq_stmts(v, "0"));
    }
    case UnaryOperator::BitwiseAnd:  // &v
    case UnaryOperator::BitwiseNand: {
      auto v   = to_pattern(to_int_value(lower_rvalue(operand)), oi.bits, oi.is_signed);
      auto all = mask_text(oi.bits);
      return mark_bool(expr.op == UnaryOperator::BitwiseAnd ? builder_.create_eq_stmts(v, all) : builder_.create_ne_stmts(v, all));
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
      const bool  is_inc   = expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Postincrement;
      const bool  is_pre   = expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Predecrement;
      auto        cur      = to_pattern(to_int_value(lower_rvalue(operand)), oi.bits, oi.is_signed);
      // post-inc/dec returns the OLD value, but the write below re-versions the
      // operand, so snapshot it into a fresh temp first (cprop folds the +0).
      std::string old_snap = is_pre ? std::string{} : builder_.create_plus_stmts(cur, "0");
      auto        nv = trunc_to(is_inc ? builder_.create_plus_stmts(cur, "1") : builder_.create_minus_stmts(cur, "1"), oi.bits);
      // `x++`/`x--` is a BLOCKING write (LRM); set the flag so note_write does
      // not inherit a stale nonblocking style from a preceding `<=` and then
      // false-flag the variable as mixing assignment styles.
      current_assign_nonblocking_ = false;
      assign_to(operand, nv);
      return is_pre ? nv : old_snap;
    }
    default:
      emit_unsupported(expr.sourceRange,
                       "unsupported-unary-op",
                       std::string("unary operator '") + std::string(slang::ast::toString(expr.op)) + "' is not supported");
      return "0";
  }
}

// An upper bound on the magnitude of `e`, used to skip a truncation that cannot
// drop a bit.
//
// slang's getEffectiveWidth() is the WIDTH-TRUNCATION LINT's heuristic, NOT a
// value bound: BinaryExpression returns max(left, right) for Add/Subtract/
// Multiply, deliberately ignoring carry and product growth — and, for unsigned
// subtraction, the wrap that LNAST represents as a NEGATIVE unbounded integer.
// Trusting it to skip a mask silently keeps those bits: `logic [7:0] r = w + 1`
// with w == 8'hff must give 0, and slang answers max(8, 1) == 8 so the 32->8
// conversion looked lossless. The heuristic is unsound transitively too (an
// `(a+1) | b` reports max of its operands' equally-optimistic widths), so ask
// slang only about expressions whose effective width IS a bound: literals,
// whole-variable reads, selects/concatenations, and conversions of those.
std::optional<int> Slang_context::value_width(const slang::ast::Expression& e) const {
  using slang::ast::ExpressionKind;
  switch (e.kind) {
    case ExpressionKind::IntegerLiteral:
    case ExpressionKind::NamedValue:
    case ExpressionKind::HierarchicalValue:
    case ExpressionKind::ElementSelect:
    case ExpressionKind::RangeSelect:
    case ExpressionKind::MemberAccess:
    case ExpressionKind::Concatenation:
    case ExpressionKind::Replication      : break;
    case ExpressionKind::Conversion:
      // slang bounds a conversion by its destination type, but the operand
      // underneath is folded in with the same heuristic — recurse so an
      // arithmetic source is still rejected.
      if (!value_width(e.as<slang::ast::ConversionExpression>().operand())) {
        return std::nullopt;
      }
      break;
    default: return std::nullopt;
  }
  if (auto w = e.getEffectiveWidth()) {
    return static_cast<int>(*w);
  }
  return std::nullopt;
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
    default: lhs = to_int_value(lhs); rhs = to_int_value(rhs);
  }

  switch (expr.op) {
    case BinaryOperator::Add: {
      // a + b needs max(eff_width)+1 bits; skip the overflow wrap when slang
      // proves that fits the result type (operands were context-widened to the
      // result, so the only headroom is their value widths).
      auto sum = builder_.create_plus_stmts(lhs, rhs);
      if (auto wl = value_width(le), wr = value_width(re); wl && wr && std::max(*wl, *wr) + 1 <= ti.bits) {
        return sum;
      }
      return fit_wrap(sum, ti.bits, ti.is_signed);
    }
    case BinaryOperator::Subtract: {
      // Signed a - b fits when max(eff_width)+1 <= bits. UNSIGNED a - b can
      // underflow to a negative value that must wrap, so only the signed case skips.
      auto diff = builder_.create_minus_stmts(lhs, rhs);
      if (ti.is_signed) {
        if (auto wl = value_width(le), wr = value_width(re); wl && wr && std::max(*wl, *wr) + 1 <= ti.bits) {
          return diff;
        }
      }
      return fit_wrap(diff, ti.bits, ti.is_signed);
    }
    case BinaryOperator::Multiply: {
      // a * b needs eff_width(a)+eff_width(b) bits; skip the wrap when that fits.
      auto prod = builder_.create_mult_stmts(lhs, rhs);
      if (auto wl = value_width(le), wr = value_width(re); wl && wr && *wl + *wr <= ti.bits) {
        return prod;
      }
      return fit_wrap(prod, ti.bits, ti.is_signed);
    }
    case BinaryOperator::Divide:
      if (ti.is_signed) {
        return fit_wrap(builder_.create_div_stmts(lhs, rhs), ti.bits, ti.is_signed);
      }
      return builder_.create_div_stmts(lhs, rhs);
    case BinaryOperator::Mod:
      // Emit the REAL `mod` node. The old expansion `a - (a/b)*b` was
      // arithmetically right but destroyed the one thing bitwidth needs: with
      // `a` and `a/b` as independent intervals the correlation between them is
      // unrecoverable, so `(sel+1) % 4` came out [-15,4] instead of [0,3] and
      // check_index_nonneg rejected a legal design (tests/equiv/
      // negative_array_index). Lnast_range::mod() gives the exact range
      // directly. No fit_wrap: |a%b| < |b|, so the result always fits.
      return builder_.create_mod_stmts(lhs, rhs);
    // Bitwise results have slang's exact self-determined width. LNAST integers
    // are otherwise unbounded, so a mask such as `{66{en}} & x` can retain the
    // unsigned sign slot as a 67th value bit and later violate a Concat lane's
    // declared 66-bit window. Materialize the language precision boundary here,
    // just as arithmetic overflow paths do above.
    case BinaryOperator::BinaryAnd : return fit_wrap(builder_.create_bit_and_stmts(lhs, rhs), ti.bits, ti.is_signed);
    case BinaryOperator::BinaryOr  : return fit_wrap(builder_.create_bit_or_stmts({lhs, rhs}), ti.bits, ti.is_signed);
    case BinaryOperator::BinaryXor : return fit_wrap(builder_.create_bit_xor_stmts(lhs, rhs), ti.bits, ti.is_signed);
    case BinaryOperator::BinaryXnor: {
      auto x = builder_.create_bit_not_stmts(builder_.create_bit_xor_stmts(lhs, rhs));
      return ti.is_signed ? x : trunc_to(x, ti.bits);
    }
    case BinaryOperator::Equality  : return mark_bool(builder_.create_eq_stmts(lhs, rhs));
    case BinaryOperator::Inequality: return mark_bool(builder_.create_ne_stmts(lhs, rhs));
    case BinaryOperator::CaseEquality:
      emit_warning(expr.sourceRange, "case-eq-two-state", "unsupported", "=== is lowered as == (two-state)");
      return mark_bool(builder_.create_eq_stmts(lhs, rhs));
    case BinaryOperator::CaseInequality:
      emit_warning(expr.sourceRange, "case-eq-two-state", "unsupported", "!== is lowered as != (two-state)");
      return mark_bool(builder_.create_ne_stmts(lhs, rhs));
    case BinaryOperator::GreaterThan        : return mark_bool(builder_.create_gt_stmts(lhs, rhs));
    case BinaryOperator::GreaterThanEqual   : return mark_bool(builder_.create_ge_stmts(lhs, rhs));
    case BinaryOperator::LessThan           : return mark_bool(builder_.create_lt_stmts(lhs, rhs));
    case BinaryOperator::LessThanEqual      : return mark_bool(builder_.create_le_stmts(lhs, rhs));
    case BinaryOperator::LogicalShiftLeft   :
    case BinaryOperator::ArithmeticShiftLeft: {
      auto amount  = to_pattern(rhs, ri.bits, ri.is_signed);  // shift amounts are unsigned
      auto shifted = builder_.create_shl_stmts(lhs, amount);
      // A CONSTANT left shift grows by exactly the shift amount; skip the wrap
      // when eff_width(lhs)+amount fits. A runtime amount keeps the wrap.
      if (auto wl = value_width(le); wl) {
        if (auto s = try_eval_int(re); s && *s >= 0 && *wl + static_cast<int>(*s) <= ti.bits) {
          return shifted;
        }
      }
      return fit_wrap(shifted, ti.bits, ti.is_signed);
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
          auto m      = mark_bool(builder_.create_eq_stmts(masked, std::to_string(val)));
          return mark_bool(expr.op == BinaryOperator::WildcardEquality ? m : builder_.create_log_not_stmts(m));
        }
      }
      emit_unsupported(expr.sourceRange, "unsupported-wildcard-eq", "==?/!=? needs a constant pattern of <= 64 bits");
      return "0";
    }
    default:
      emit_unsupported(expr.sourceRange,
                       "unsupported-binary-op",
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
  // Type the temp to the expression's width.  The DIRECT slang->lg path infers
  // this fine from the fit_wrap'd arm seed, but the v2prp round-trip re-emits
  // the arms as bare integer literals (sizes lost), so an UNTYPED `mut` temp
  // makes the re-compile re-infer a signed minimal width and sign-extend the
  // values through a chained mux (the CLZ priority-encoder class: io_out[0]
  // proved but the upper bits diverged).  A typed temp pins the width/sign.
  if (ti.bits > 0) {
    builder_.create_declare_stmts(tmp, "mut", int_max_str(ti.bits, ti.is_signed), int_min_str(ti.bits, ti.is_signed));
  } else {
    builder_.create_declare_stmts(tmp, "mut", "", "");  // rangeless: arms may carry x
  }
  builder_.create_assign_stmts(tmp, b);  // else value seeds the width

  auto if_nid = builder_.create_if_stmt(false);
  builder_.add_if_cond(if_nid, cond);
  auto then_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(then_stmts);
  builder_.create_assign_stmts(tmp, a);
  builder_.pop_stmts();

  return tmp;
}

std::string Slang_context::lower_assignment_pattern(const slang::ast::Expression&                  expr,
                                                    std::span<const slang::ast::Expression* const> elems) {
  // `T'{...}` for a packed (integral) struct/array: slang resolves `elements()`
  // positionally MSB-first, so the value is just the fields concatenated — same
  // bit layout as a `{...}` concat of those fields. Unpacked targets (memories /
  // unpacked-array vars) are a different lowering and stay unsupported here.
  //
  // Do NOT re-derive `elements()` from a structured pattern's member/type/default
  // setters: slang's forStruct/forFixedArray already walk the fields (declaration
  // order) or the indices and call matchElementValue for every one that no
  // `name:`/`index:` key covered, which (a) applies the LAST matching `type:` key,
  // per the LRM, and (b) re-BINDS the `default:` SYNTAX at the field type. That
  // re-bind is the whole point: substituting the raw default expression makes each
  // unset field contribute the default's self-determined width instead of its own
  // (`cause_t'{cause: c, interrupt_x: i, default: '0}` then advances the offset by
  // 1, not 58, and lands interrupt_x at bit 6 instead of 63 — silently wrong), and
  // it drops packed-array patterns, which have no fields at all.
  if (!expr.type->isIntegral()) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-assignment-pattern",
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

// `{<<N{x}}` / `{>>N{x}}` — the streaming (bit/byte reversal) operator.
//
// The value is sliced into N-bit blocks and, for `<<`, the block ORDER is
// reversed: `{<<8{x[31:0]}}` is a byte swap, which is how CVA6's load_unit /
// store_unit implement big-endian access. `>>` (and a slice size of 0, which is
// slang's spelling for a plain left-to-right stream) keeps the order, so the
// value passes through unchanged.
//
// Only the FIXED-SIZE, whole-block case is lowered. A width that is not a
// multiple of the slice size leaves a short block whose placement is easy to
// get subtly wrong, and a dynamically sized stream has no static width at all:
// both are refused rather than lowered into a plausible-looking swap.
std::string Slang_context::lower_streaming(const slang::ast::StreamingConcatenationExpression& expr) {
  if (!expr.isFixedSize()) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-streaming",
                     "dynamically sized streaming concatenation is not supported by --reader slang");
    return "0";
  }
  const auto streams = expr.streams();
  if (streams.size() != 1 || streams[0].withExpr != nullptr) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-streaming",
                     "only a single-operand streaming concatenation without `with` is supported by --reader slang");
    return "0";
  }

  const auto  width = static_cast<int>(expr.getBitstreamWidth());
  const auto  slice = static_cast<int>(expr.getSliceSize());
  const auto& oper  = *streams[0].operand;
  auto        val   = to_int_value(lower_rvalue(oper));

  // slice 0 == left-to-right: the bits keep their order, so this is the value.
  if (slice <= 0 || slice >= width) {
    return val;
  }
  if (width % slice != 0) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-streaming",
                     std::format("streaming width {} is not a multiple of the slice size {} — the short block's "
                                 "placement is not supported by --reader slang",
                                 width,
                                 slice));
    return "0";
  }

  // Reverse the blocks: block i of the source lands at position (n-1-i).
  const int                nblocks = width / slice;
  std::vector<std::string> parts;
  parts.reserve(static_cast<size_t>(nblocks));
  for (int i = 0; i < nblocks; ++i) {
    // shift-then-mask rather than get_mask: `#[...]` right-aligns what it
    // extracts, so masking in place and shifting down would shift twice.
    const int lo   = i * slice;
    auto      down = lo == 0 ? val : builder_.create_sra_stmts(val, std::to_string(lo));
    auto      blk  = builder_.create_bit_and_stmts(down, std::format("0x{:x}", (1ULL << slice) - 1));
    const int dest = (nblocks - 1 - i) * slice;
    parts.emplace_back(dest == 0 ? blk : builder_.create_shl_stmts(blk, std::to_string(dest)));
  }
  return builder_.create_bit_or_stmts(parts);
}

// `{a, b, c}` — the FIRST operand is the MSB lane, which is exactly the LNAST
// `concat` node's own order, so the operands pass straight through in source
// order and the node carries the whole bus as ONE n-ary statement.
//
// This used to be a shift+or tower whose per-lane offset was accumulated here.
// One habit of that spelling does NOT survive the move: a zero lane may not be
// DROPPED. The tower computed every offset independently, so skipping
// `{2'b0, x}`'s zero cost nothing; the concat node derives each lane's offset
// from the widths of the lanes BELOW it, so a dropped lane slides every lane
// above it down two bits.
//
// The widths themselves are the easy half here — `tinfo(*e.type).bits` is the
// operand's self-determined width, which IS the window — so slang binds every
// lane width at creation and nothing downstream has to infer one. That is also
// why a constant lane needs no special handling: the width rides in its own
// operand, so `2'b0` may stay the plain value `0` without the literal's
// spelling having to encode a width.
// A constant bit WINDOW of a named root, normalized to (identity key, 0-based
// low bit from the root's LSB, width). The select math mirrors lower_select's
// `normalize` exactly — a declared range may be ascending or descending and
// need not start at 0, and a packed-array element carries a stride.
//
// The KEY is a semantic identity, not an AST identity: the `x` inside `{32{x[31]}}`
// and the `x` in the lane below it are distinct AST nodes reading the same
// storage, and only the resolved symbol (plus, for a packed struct, the field
// path) can say so. A field path re-bases the window on the FIELD, which is how
// the reader lowers it (an independent leaf net) — so `io.x[31]` and `io.x`
// share a key while `io[63]` and `io.x` deliberately do not.
//
// nullopt for anything not pinned to a constant offset over integral
// fixed-range storage: a dynamic index, an unpacked/memory-ized base, an
// arithmetic or literal root, a select-under-field. Callers treat that as "the
// two windows may or may not overlap", i.e. refuse.
std::optional<Slang_context::Bit_window> Slang_context::const_bit_window(const slang::ast::Expression& e) {
  const auto ti = tinfo(*e.type);
  if (ti.bits <= 0) {
    return std::nullopt;
  }
  Bit_window w;
  w.bits = ti.bits;

  const slang::ast::Expression* cur = &e;
  while (cur->kind == ExpressionKind::ElementSelect || cur->kind == ExpressionKind::RangeSelect) {
    const auto& base    = cur->kind == ExpressionKind::ElementSelect ? cur->as<slang::ast::ElementSelectExpression>().value()
                                                                     : cur->as<slang::ast::RangeSelectExpression>().value();
    const auto& base_ty = base.type->getCanonicalType();
    if (!base_ty.isIntegral() || !base_ty.hasFixedRange()) {
      return std::nullopt;  // unpacked / memory-ized base: a different read path
    }
    const auto range   = base_ty.getFixedRange();
    const auto stride  = base_ty.isPackedArray() ? static_cast<int64_t>(base_ty.getArrayElementType()->getBitWidth()) : 1;
    int64_t    elem_lo = 0;  // bottom of the window, 0-based in ELEMENTS from the LSB end
    if (stride <= 0) {
      return std::nullopt;
    }
    // (width_down, width_up): elements the index is above / below, so one
    // formula covers `[i]`, `[hi:lo]`, `[i +: W]` and `[i -: W]`.
    int64_t                       width_down = 1;
    int64_t                       width_up   = 1;
    const slang::ast::Expression* idx        = nullptr;
    if (cur->kind == ExpressionKind::ElementSelect) {
      idx = &cur->as<slang::ast::ElementSelectExpression>().selector();
    } else {
      const auto& rs    = cur->as<slang::ast::RangeSelectExpression>();
      const auto  kind  = rs.getSelectionKind();
      // Element count of THIS slice — `tinfo(*cur->type)`, not the accumulated
      // `w.bits` (the outermost expression's width, which only coincides on the
      // first peel). SV forbids selecting after a range select, so today the
      // two are always equal here; do not make this depend on that.
      const auto  elems = static_cast<int64_t>(tinfo(*cur->type).bits) / stride;
      if (kind == slang::ast::RangeSelectionKind::Simple) {
        auto l = try_eval_int(rs.left());
        auto r = try_eval_int(rs.right());
        if (!l || !r) {
          return std::nullopt;
        }
        elem_lo = range.isDescending() ? std::min(*l, *r) - range.lower() : range.upper() - std::max(*l, *r);
      } else if (kind == slang::ast::RangeSelectionKind::IndexedUp) {
        idx      = &rs.left();
        width_up = elems;
      } else {
        idx        = &rs.left();
        width_down = elems;
      }
    }
    if (idx != nullptr) {
      auto ci = try_eval_int(*idx);
      if (!ci) {
        return std::nullopt;  // dynamic select: no constant window
      }
      elem_lo = range.isDescending() ? (*ci - range.lower() - (width_down - 1)) : (range.upper() - *ci - (width_up - 1));
    }
    if (elem_lo < 0) {
      return std::nullopt;
    }
    w.lo += elem_lo * stride;
    cur   = &base;
  }

  const auto& root = *cur;
  const auto  ri   = tinfo(*root.type);
  if (ri.bits <= 0 || w.lo + w.bits > ri.bits) {
    return std::nullopt;  // an out-of-range select is diagnosed on the normal path
  }

  std::vector<std::string_view> fields;  // innermost-last; reversed into the key
  while (cur->kind == ExpressionKind::MemberAccess) {
    const auto& ma = cur->as<slang::ast::MemberAccessExpression>();
    if (ma.member.kind != slang::ast::SymbolKind::Field || !ma.value().type->isIntegral()) {
      return std::nullopt;
    }
    fields.push_back(ma.member.name);
    cur = &ma.value();
  }
  // NamedValue only. A HIERARCHICAL reference resolves to a ValueSymbol in an
  // InstanceBodySymbol, and slang shares one body across instances with the
  // same parameterization — so two refs down DIFFERENT instance paths could
  // share a symbol pointer, i.e. a key that lies. The whole soundness argument
  // is "same key ⇒ same storage"; nothing may weaken it. Measured cost of
  // refusing: zero (dino, cva6 and minion have no hierarchical-ref sign
  // extension), and a miss only means the generic concat path runs.
  if (cur->kind != ExpressionKind::NamedValue) {
    return std::nullopt;
  }
  const auto& sym = cur->as<slang::ast::NamedValueExpression>().symbol;
  w.key           = absl::StrCat("#", absl::Hex(reinterpret_cast<uintptr_t>(&sym)));  // NOLINT(performance-no-int-to-ptr)
  for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
    absl::StrAppend(&w.key, ".", *it);
  }
  return w;
}

// `{{N{v[msb]}}, v}` / `{{N{v[msb]}}, v[msb:lo]}` / `{{N{v[msb]}}, v[msb:lo], w}`
// is how firtool (and hand-written RTL) spells a sign extension: replicate the
// top bit of the lanes below, N times, above them. That is exactly LNAST's
// `sext`, so emit one instead of lowering the replication — which would
// otherwise cost a bit-extract plus an N-bit broadcast (a select mux, or an
// OR tower of N shifted copies) plus a shift and an OR, all to recompute a bit
// the value below already carries.
//
// Soundness: the replicated bit has to be the MSB of the lane DIRECTLY below the
// replication, which (lanes being packed MSB-first) is the MSB of all the lanes
// below it taken together. Both sides are resolved to constant windows over the
// same storage, so a replicated bit of some OTHER net — `{{32{ready}}, data}`,
// a mask idiom, not a sign extension — can never match. Returns "" (having
// emitted nothing) for every shape it does not recognize.
std::string Slang_context::lower_concat_sext(const slang::ast::ConcatenationExpression& expr) {
  auto ops = expr.operands();
  if (ops.size() < 2 || ops[0]->kind != ExpressionKind::Replication) {
    return "";
  }
  const auto& rep   = ops[0]->as<slang::ast::ReplicationExpression>();
  auto        count = try_eval_int(rep.count());
  if (!count || *count < 1) {
    return "";
  }
  // `{N{b}}` models its operand as a one-element concatenation; unwrap it so the
  // window below sees the bit expression itself.
  const slang::ast::Expression* bit = &rep.concat();
  if (bit->kind == ExpressionKind::Concatenation) {
    auto inner = bit->as<slang::ast::ConcatenationExpression>().operands();
    if (inner.size() != 1) {
      return "";
    }
    bit = inner[0];
  }
  if (tinfo(*bit->type).bits != 1) {
    return "";  // a multi-bit replication is not a sign extension
  }

  auto sign_w = const_bit_window(*bit);
  auto top_w  = const_bit_window(*ops[1]);
  if (!sign_w || !top_w || sign_w->key != top_w->key || sign_w->lo != top_w->lo + top_w->bits - 1) {
    return "";
  }

  int64_t low_bits = 0;
  for (size_t i = 1; i < ops.size(); ++i) {
    auto oi = tinfo(*ops[i]->type);
    if (oi.bits <= 0) {
      return "";
    }
    low_bits += oi.bits;
  }
  const auto total_bits = *count + low_bits;
  if (total_bits != tinfo(*expr.type).bits) {
    return "";  // the concat's own width disagrees: leave the generic path alone
  }

  // Committed: from here on nothing may fail, because lowering emits.
  std::vector<Lnast_builder::Concat_lane> lanes;
  lanes.reserve(ops.size() - 1);
  for (size_t i = 1; i < ops.size(); ++i) {
    const auto& e  = *ops[i];
    auto        oi = tinfo(*e.type);
    lanes.push_back({fit_wrap(to_int_value(lower_rvalue(e)), oi.bits, oi.is_signed), oi.bits});
  }
  // A single SIGNED lane needs no sext node: fit_wrap already restored its
  // top-bit interpretation, which IS the sign extension.
  std::string low = lanes.size() == 1 ? lanes[0].value : builder_.create_concat_stmts(lanes);
  if (lanes.size() != 1 || !tinfo(*ops[1]->type).is_signed) {
    low = builder_.create_sext_stmts(low, std::to_string(low_bits - 1));
  }
  return trunc_to(low, static_cast<int>(total_bits));
}

std::string Slang_context::lower_concat(const slang::ast::ConcatenationExpression& expr) {
  auto ops = expr.operands();

  if (auto sx = lower_concat_sext(expr); !sx.empty()) {
    return sx;
  }

  std::vector<Lnast_builder::Concat_lane> lanes;
  lanes.reserve(ops.size());

  for (const auto* op : ops) {
    const auto& e  = *op;
    auto        oi = tinfo(*e.type);
    // A Concat lane is an IR precision boundary: its driver must already fit
    // the source operand's self-determined window. `fit_wrap` both truncates an
    // unsigned/unbounded expression and restores a signed operand's top-bit
    // interpretation, yielding exactly the two's-complement window SV packs.
    lanes.push_back({fit_wrap(to_int_value(lower_rvalue(e)), oi.bits, oi.is_signed), oi.bits});
  }

  if (lanes.empty()) {
    return "0";  // `{}` is not legal SV; keep the old empty-concat guard
  }
  return builder_.create_concat_stmts(lanes);
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
    // Field read of a struct-element (tuple) memory: `mem[idx].field` lowers to
    // a fused per-field tuple_get (detuple -> tuple_get(d, mem.field, idx)),
    // avoiding a whole-element reconstruct + re-extract.
    if (ma.value().kind == ExpressionKind::ElementSelect) {
      std::vector<const slang::ast::Expression*> sels;
      const auto&                                mbase    = *peel_unpacked_chain(ma.value(), sels);
      const auto*                                base_sym = resolve_base_symbol(mbase);
      if (base_sym != nullptr && !flat_port_syms_.contains(base_sym)) {
        auto mit = mem_info_.find(base_sym);
        if (mit != mem_info_.end() && mit->second.is_tuple && sels.size() == mit->second.rank()) {
          const auto& mi    = mit->second;
          const auto& field = ma.member.as<slang::ast::FieldSymbol>();
          if (const auto* f = find_tuple_field(mi, field.name)) {
            auto idx = build_unpacked_index(mi, sels);
            auto d   = emit_field_read_chain(lname_of(*base_sym), idx, f->name);
            return f->is_signed ? builder_.create_sext_stmts(d, std::to_string(f->bits - 1)) : d;
          }
        }
      }
    }
    // A NESTED field read (`a.b.c`): walk the MemberAccess chain down to its
    // base NamedValue and join the field path, so it resolves to exactly the
    // SAME dotted leaf a one-level access resolves to. Every fast path below is
    // guarded on the base being a NamedValue, so before this a nested chain fell
    // through to the generic select at the end of this branch — extract the
    // whole enclosing sub-struct, then shift the field out. That read is
    // OVER-WIDE: it covers sibling lanes it never uses, so writing field X while
    // reading disjoint field Y looked like a self-reference on one word and
    // produced a FALSE combinational cycle. See struct_port_fields' recursive
    // flatten and lhd/tests/struct_nested_field_leaf_test.sh.
    {
      std::vector<std::string_view> path;
      const slang::ast::Expression* cur = &expr;
      while (cur->kind == ExpressionKind::MemberAccess) {
        const auto& m = cur->as<slang::ast::MemberAccessExpression>();
        if (m.member.kind != slang::ast::SymbolKind::Field) {
          break;
        }
        path.push_back(m.member.name);
        cur = &m.value();
      }
      if (!path.empty() && cur->kind == ExpressionKind::NamedValue) {
        std::string dotted;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
          if (!dotted.empty()) {
            dotted += '.';
          }
          dotted += std::string(*it);
        }
        const auto& bsym = cur->as<slang::ast::NamedValueExpression>().symbol;
        if (const auto* bsi = bundle_port_of(bsym)) {
          // M7: field read of a BUNDLE port at any depth — a plain read of the
          // dotted io leaf (`req.cmd`), the hand-flattened-twin form the SSA
          // port flatten produces. A part-select WITHIN a field (`req.f[3:0]`)
          // recurses here through the generic select path below, so its offset
          // math is already field-relative.
          if (const auto* f = find_struct_field(*bsi, dotted)) {
            return read_leaf(absl::StrCat(bundle_port_body_base(bsym), ".", f->name));
          }
          // A WHOLE SUB-STRUCT read (`ctrl.req`) is deliberately NOT special-cased.
          // An interior level is a name PREFIX, never a leaf entry, so it falls
          // through to the generic tail below, which reconstructs from all the
          // port's leaves and slices. That read is over-wide — and a narrower
          // subtree-only reconstruct WAS written here and then REMOVED, because
          // no fixture could tell the two apart. Once the leaves are separate
          // NETS the over-wide read is harmless: the slice provably drops the
          // sibling lanes and constprop folds the dependency away. The
          // word-level cycle needed a `set_mask` CHAIN ON ONE NET, which a
          // per-leaf port can no longer form. (A struct VARIABLE still can —
          // that is vpu_ctrl; see lhdsuite fixme.md issue 1b.) Do not re-add it
          // without a fixture that fails first.
        }
      }
    }
    // Field read of a scalar packed-struct VARIABLE lowered as a bundle:
    // `io.operation` is just the leaf net (an independent wire), not a bit-slice
    // of a flat `io` bus. (`io.operation[4]` then bit-selects this leaf.)
    if (ma.value().kind == ExpressionKind::NamedValue) {
      const auto& bsym = ma.value().as<slang::ast::NamedValueExpression>().symbol;
      if (is_scalar_struct_var(bsym)) {
        if (!declared_.contains(&bsym)) {
          declare_value_symbol(bsym, /*force_reg=*/false);
        }
        const auto& field = ma.member.as<slang::ast::FieldSymbol>();
        if (auto it = struct_var_info_.find(&bsym); it != struct_var_info_.end()) {
          if (const auto* f = find_struct_field(it->second, field.name)) {
            // A real-tuple struct reads the field via tuple_get; a flat-leaf struct
            // (mut, or a wire with nested fields) uses its leaf net directly.
            return it->second.is_tuple ? read_struct_field_get(lname_of(bsym), f->name)
                                       : read_leaf(absl::StrCat(lname_of(bsym), ".", f->name));
          }
        }
      }
    }
    const auto& field = ma.member.as<slang::ast::FieldSymbol>();
    auto        bi    = tinfo(*ma.value().type);
    auto        p     = to_pattern(to_int_value(lower_rvalue(ma.value())), bi.bits, bi.is_signed);
    auto        lo    = static_cast<int64_t>(field.bitOffset);
    auto        r     = extract_field(p, lo, ti.bits);
    return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
  }

  const auto& base    = expr.kind == ExpressionKind::ElementSelect ? expr.as<slang::ast::ElementSelectExpression>().value()
                                                                   : expr.as<slang::ast::RangeSelectExpression>().value();
  const auto& base_ty = base.type->getCanonicalType();

  if (base_ty.isUnpackedArray()) {
    return lower_unpacked_read(expr);
  }

  // An ELEMENT-select of a packed 2-D reg that was memory-ized (register file):
  // route to the memory read path even though the base is a packed array. Only
  // a single-element select of the memory-ized base — a sub-bit/range select of
  // an element keeps the bit-slice path (it would resolve a deeper base).
  if (expr.kind == ExpressionKind::ElementSelect && base.kind != ExpressionKind::ElementSelect
      && base.kind != ExpressionKind::RangeSelect) {
    const auto* base_sym = resolve_base_symbol(base);
    if (base_sym != nullptr && !flat_port_syms_.contains(base_sym) && mem_info_.contains(base_sym)
        && packed_mem_regs_.contains(base_sym)) {
      return lower_unpacked_read(expr);
    }
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

  // A whole-element select `vec[const]` of a BUNDLE PORT array routes to the
  // element's independent leaf net. A sub-element / dynamic / range select falls
  // through to the generic path, which reconstructs the whole value from the
  // leaves via lower_rvalue(base) below.
  if (expr.kind == ExpressionKind::ElementSelect && base.kind == ExpressionKind::NamedValue) {
    const auto& bsym = base.as<slang::ast::NamedValueExpression>().symbol;
    if (const Struct_info* array_info = bundle_port_of(bsym);
        array_info != nullptr && base_ty.isPackedArray() && ti.bits == stride) {
      const auto& es        = expr.as<slang::ast::ElementSelectExpression>();
      const auto  leaf_base = bundle_port_body_base(bsym);
      // A reference, not a copy: a dynamic select below asks for every lane, so
      // copying the field table per access is O(lanes * fields) of churn on the
      // arrays this path exists for.
      const auto& fields    = array_info->fields;
      auto        read_lane = [&](int64_t idx) {
        const auto  prefix = absl::StrCat("e", idx);
        const auto  dot_prefix = absl::StrCat(prefix, ".");  // hoisted: the loop below asks per field
        std::string value;
        for (const auto& f : fields) {
          if (f.name != prefix && !std::string_view(f.name).starts_with(dot_prefix)) {
            continue;
          }
          auto       part = to_pattern(read_leaf(absl::StrCat(leaf_base, ".", f.name)), f.bits, f.is_signed);
          const auto rel = f.off - idx * stride;
          if (rel != 0) {
            part = builder_.create_shl_stmts(part, std::to_string(rel));
          }
          value = value.empty() ? part : builder_.create_bit_or_stmts({value, part});
        }
        return value;
      };
      if (auto ci = try_eval_int(es.selector())) {
        int64_t idx = range.isDescending() ? (*ci - range.lower()) : (range.upper() - *ci);
        if (auto value = read_lane(idx); !value.empty()) {
          return ti.is_signed ? builder_.create_sext_stmts(value, std::to_string(stride - 1)) : value;
        }
      } else {
        // Preserve the aggregate as positional element refs at this access
        // boundary. uPass recognizes a runtime tuple_get over those refs and
        // lowers it to Hotmux, avoiding a permanent concat+shift cone. Append
        // an explicit unknown lane so the Hotmux's mandatory else arm matches
        // SystemVerilog out-of-range selection instead of aliasing the final
        // valid element.
        {
          // Build every lane value before the tuple node. read_lane can emit
          // masks/shifts/ors (and constant-folded temporaries); creating the
          // tuple first would make its children forward-reference those
          // producers, so uPass sees unresolved slots while lowering the
          // runtime tuple_get.
          // A lane with no matching leaf yields "" — an empty ref child would
          // be a malformed tuple, so fall through to the generic flat-value
          // path instead (same guard the constant-index branch above has).
          std::vector<std::string> lanes;
          lanes.reserve(range.width());
          bool every_lane_read = true;
          for (int64_t lane_idx = 0; lane_idx < static_cast<int64_t>(range.width()); ++lane_idx) {
            lanes.push_back(read_lane(lane_idx));
            every_lane_read = every_lane_read && !lanes.back().empty();
          }
          if (every_lane_read && !lanes.empty()) {
            auto index = to_int_value(lower_rvalue(es.selector()));
            if (range.isDescending()) {
              if (range.lower() != 0) {
                index = builder_.create_minus_stmts(index, std::to_string(range.lower()));
              }
            } else {
              index = builder_.create_minus_stmts(std::to_string(range.upper()), index);
            }

            auto& ln       = *builder_.lnast;
            auto  tuple    = builder_.create_lnast_tmp();
            auto  tuple_op = builder_.add_child(Lnast_ntype::create_tuple_add());
            ln.add_child(tuple_op, Lnast_node::create_ref(tuple));
            for (const auto& lane : lanes) {
              ln.add_child(tuple_op, Lnast_node::create_ref(lane));
            }
            std::string xbits(static_cast<size_t>(stride), '?');
            ln.add_child(tuple_op, Lnast_node::create_const(absl::StrCat("0ub", xbits)));

            auto result = builder_.create_lnast_tmp();
            auto get    = builder_.add_child(Lnast_ntype::create_tuple_get());
            ln.add_child(get, Lnast_node::create_ref(result));
            ln.add_child(get, Lnast_node::create_ref(tuple));
            builder_.add_value_child_pub(get, index);
            return ti.is_signed ? builder_.create_sext_stmts(result, std::to_string(stride - 1)) : result;
          }
        }
      }
    }
  }

  auto p = to_pattern(to_int_value(lower_rvalue(base)), bi.bits, bi.is_signed);

  // (selected width in bits, low element index normalized to 0-based)
  int                    sel_bits = ti.bits;
  std::optional<int64_t> const_low;
  std::string            dyn_low;               // 0-based element index expression
  bool                   comptime_dyn = false;  // dyn_low is a COMPTIME pkg-param expression

  auto normalize = [&](const slang::ast::Expression& idx,
                       int64_t                       width_down,
                       int64_t                       width_up) -> std::pair<std::optional<int64_t>, std::string> {
    // bottom element of the selection, 0-based from the LSB end
    if (auto ci = try_eval_int(idx)) {
      // Provenance: a comptime index NAMING a package param (`sigs[PKG_BIT]`,
      // `x[PKG_SZ-1]`) keeps the name — emit the bias arithmetic symbolically
      // via the dynamic route; comptime_dyn skips its runtime wrap-guards (the
      // amount folds back to this very constant on recompile).
      if (!(options_.preserve_param_provenance && contains_package_param(idx))) {
        int64_t bottom = range.isDescending() ? (*ci - range.lower() - (width_down - 1)) : (range.upper() - *ci - (width_up - 1));
        return {bottom, {}};
      }
      comptime_dyn = true;
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
    const auto& es               = expr.as<slang::ast::ElementSelectExpression>();
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
  if (comptime_dyn) {
    // COMPTIME symbolic amount (provenance): the value is a known in-range
    // constant on recompile, so the runtime wrap-guard widening below is
    // unnecessary — a plain shift+mask keeps the emitted text readable.
    auto r = trunc_to(builder_.create_sra_stmts(p, shamt), sel_bits);
    return ti.is_signed ? builder_.create_sext_stmts(r, std::to_string(ti.bits - 1)) : r;
  }
  const int bias    = sel_bits;
  // `+ bias` is plain IR arithmetic: bitwidth infers the Sum wide enough for the
  // carry, and cgen honours that width (Verilog SELF-determines a shift's amount
  // operand, so cgen's create_locals lands it in a declared variable instead of
  // folding it into the shift text — see the shift-amount pass there).
  //
  // Do NOT pre-widen `shamt` with a zero-extending get_mask here. That used to
  // guard the same carry -- "a 4-bit idx makes `idx + 1 == 0` at idx==15" -- back
  // when cgen dropped it, but it works by REINTERPRETING the index as unsigned,
  // which silently discards the sign of a SIGNED index: `a[$signed(b) +: 4]` with
  // b = 3'b111 became `31 + 4 = 35` instead of `-1 + 4 = 3`, so the select read 0
  // where the source keeps a[2:0]. cvc5 refuted that graph against the intended
  // lowering (witness a=1 b=5: 8 vs 0) -- the emitted Verilog only looked right
  // because cgen's truncation cancelled it. Keeping `shamt` at its own
  // signedness lets `sext(b) + bias` stay signed and land in 0..bi.bits.
  auto      shifted = builder_.create_sra_stmts(builder_.create_shl_stmts(p, std::to_string(bias)),
                                           builder_.create_plus_stmts(shamt, std::to_string(bias)));
  auto      r       = trunc_to(shifted, sel_bits);
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
    emit_unsupported(expr.sourceRange,
                     "unsupported-system-call",
                     std::string("system call '") + std::string(name) + "' is not supported by --reader slang");
    return "0";
  }

  // User function: inline the body for synthesizable, input-only functions.
  const auto* sub = std::get<const slang::ast::SubroutineSymbol*>(expr.subroutine);
  if (sub != nullptr && sub->subroutineKind == slang::ast::SubroutineKind::Function && sub->returnValVar != nullptr
      && !expr.hasOutputArgs() && !sub->flags.has(slang::ast::MethodFlags::DPIImport)) {
    return inline_call(expr, *sub);
  }

  emit_unsupported(expr.sourceRange,
                   "unsupported-function-call",
                   std::string("call to '") + std::string(expr.getSubroutineName())
                       + "' is not supported by --reader slang yet (only compile-time evaluable functions fold)");
  return "0";
}

std::string Slang_context::inline_call(const slang::ast::CallExpression& expr, const slang::ast::SubroutineSymbol& sub) {
  if (inline_depth_ > 32) {
    emit_unsupported(expr.sourceRange,
                     "unsupported-function-call",
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
      emit_unsupported(expr.sourceRange,
                       "unsupported-function-call",
                       "only pure input-argument functions can be inlined by --reader slang");
      return "0";
    }
    declare_value_symbol(fa, /*force_reg=*/false);
    // A struct-typed formal is declared as a per-field bundle (its reads route
    // through the leaves), so a flat whole store would be dead — split the
    // actual's value onto the leaves (same as assign_to's NamedValue path).
    const bool saved_nb         = current_assign_nonblocking_;
    current_assign_nonblocking_ = false;  // formal binding is a blocking write
    if (!assign_struct_whole_value(fa, argv[i], expr.sourceRange.start())) {
      note_write(fa, /*nonblocking=*/false, expr.sourceRange.start());
      builder_.create_assign_stmts(lname_of(fa), argv[i]);
    }
    current_assign_nonblocking_ = saved_nb;
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

  // The SubroutineSymbol (including its formal and return symbols) is shared by
  // every call site.  Returning the mutable return variable by name aliases two
  // calls in one expression: `f(a) < f(b)` lowers both operands to the second
  // call's `f` variable and becomes `f < f`.  Snapshot the completed call into
  // a fresh SSA temp before another call can rebind the formals / overwrite the
  // return slot.
  auto result = builder_.create_lnast_tmp();
  builder_.create_assign_stmts(result, read_symbol(rv, expr.sourceRange));
  return result;
}
