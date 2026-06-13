//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Statement lowering (todo/ 2s subtask B). Structured statements stay
// structured: if/case become LNAST if/unique_if tree nodes and the upass SSA
// machinery does branch merging. Loops are const-evaluated and unrolled
// slang-side with a capped, diagnosed budget (yosys-slang
// UnrollLimitTracking is the model): loop variables become EvalContext
// locals so tier-1 constant folding sees them.

#include <format>
#include <functional>

#include "slang/ast/Statement.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang_context.hpp"

using slang::ast::StatementKind;

void Slang_context::lower_statement(const slang::ast::Statement& stmt) {
  switch (stmt.kind) {
    case StatementKind::Empty: return;
    case StatementKind::List:
      for (const auto* s : stmt.as<slang::ast::StatementList>().list) {
        lower_statement(*s);
      }
      return;
    case StatementKind::Block: {
      const auto& block = stmt.as<slang::ast::BlockStatement>();
      if (block.blockKind != slang::ast::StatementBlockKind::Sequential) {
        emit_unsupported(stmt.sourceRange, "unsupported-fork", "fork/join blocks are not supported by --reader slang");
        return;
      }
      lower_statement(block.body);
      return;
    }
    case StatementKind::ExpressionStatement: {
      const auto& expr = stmt.as<slang::ast::ExpressionStatement>().expr;
      if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        set_pending_loc(stmt.sourceRange);
        lower_assign(expr.as<slang::ast::AssignmentExpression>());
        clear_pending_loc();
        return;
      }
      if (expr.kind == slang::ast::ExpressionKind::Call) {
        const auto& call = expr.as<slang::ast::CallExpression>();
        if (call.isSystemCall()) {
          auto name = call.getSubroutineName();
          // Simulation-side tasks are no-ops for synthesis; keep quiet for the
          // common printers, diagnose the rest.
          if (name == "$display" || name == "$write" || name == "$monitor" || name == "$strobe" || name == "$time"
              || name == "$displayb" || name == "$displayh" || name == "$displayo" || name == "$dumpfile"
              || name == "$dumpvars" || name == "$finish" || name == "$stop") {
            return;
          }
          emit_unsupported(stmt.sourceRange, "unsupported-system-task",
                           std::string("system task '") + std::string(name) + "' is not supported by --reader slang");
          return;
        }
      }
      // void-context expression (e.g. function call statement)
      lower_rvalue(expr);
      return;
    }
    case StatementKind::VariableDeclaration: {
      const auto& vds = stmt.as<slang::ast::VariableDeclStatement>();
      declare_value_symbol(vds.symbol, /*force_reg=*/false);
      if (const auto* init = vds.symbol.getInitializer()) {
        set_pending_loc(stmt.sourceRange);
        auto v = lower_rvalue(*init);
        note_write(vds.symbol, /*nonblocking=*/false, stmt.sourceRange.start());
        builder_.create_assign_stmts(lname_of(vds.symbol), v);
        clear_pending_loc();
      }
      return;
    }
    case StatementKind::Conditional: lower_conditional(stmt.as<slang::ast::ConditionalStatement>()); return;
    case StatementKind::Case: lower_case(stmt.as<slang::ast::CaseStatement>()); return;
    case StatementKind::ForLoop: lower_for_loop(stmt.as<slang::ast::ForLoopStatement>()); return;
    case StatementKind::WhileLoop:
    case StatementKind::DoWhileLoop:
    case StatementKind::RepeatLoop: lower_while_loop(stmt); return;
    case StatementKind::ForeachLoop: lower_foreach(stmt.as<slang::ast::ForeachLoopStatement>()); return;
    case StatementKind::Timed: {
      const auto& timed = stmt.as<slang::ast::TimedStatement>();
      if (timed.timing.kind == slang::ast::TimingControlKind::Delay) {
        emit_warning(stmt.sourceRange, "delay-ignored", "unsupported", "#delay is ignored (synthesis semantics)");
        lower_statement(timed.stmt);
        return;
      }
      emit_unsupported(stmt.sourceRange, "unsupported-timing",
                       "event controls inside a process body are not supported by --reader slang");
      return;
    }
    case StatementKind::ImmediateAssertion:
      // synthesis ignores immediate assertions (a cassert lowering can come later)
      return;
    case StatementKind::ConcurrentAssertion:
      emit_warning(stmt.sourceRange, "assertion-ignored", "unsupported", "concurrent assertion ignored (synthesis semantics)");
      return;
    case StatementKind::Return:
    case StatementKind::Break:
    case StatementKind::Continue:
      // Only reachable outside an unrolled-loop/function context here.
      emit_unsupported(stmt.sourceRange, "unsupported-jump",
                       std::string(slang::ast::toString(stmt.kind)) + " is not supported in this context by --reader slang");
      return;
    default:
      emit_unsupported(stmt.sourceRange, "unsupported-statement",
                       std::string("statement kind '") + std::string(slang::ast::toString(stmt.kind))
                           + "' is not supported by --reader slang yet");
  }
}

void Slang_context::lower_conditional(const slang::ast::ConditionalStatement& stmt) {
  // Fold the &&& conditions; patterns are unsupported.
  std::string cond;
  for (const auto& c : stmt.conditions) {
    if (c.pattern != nullptr) {
      emit_unsupported(stmt.sourceRange, "unsupported-pattern", "if-statement match patterns are not supported");
      return;
    }
    auto v = booleanize(lower_rvalue(*c.expr));
    cond   = cond.empty() ? v : builder_.create_log_and_stmts(cond, v);
  }

  set_pending_loc(stmt.sourceRange);
  auto if_nid = builder_.create_if_stmt(false);
  clear_pending_loc();
  builder_.add_if_cond(if_nid, cond);

  auto then_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(then_stmts);
  lower_statement(stmt.ifTrue);
  builder_.pop_stmts();

  if (stmt.ifFalse != nullptr) {
    auto else_stmts = builder_.add_if_stmts(if_nid);
    builder_.push_stmts(else_stmts);
    lower_statement(*stmt.ifFalse);
    builder_.pop_stmts();
  }
}

// Build the per-item match condition for a case arm expression. `sel` is the
// lowered selector, `si` its type info. Wildcard bits (z for casez, x+z for
// casex) compare only the known bits.
std::string Slang_context::case_item_match(const std::string& sel, const Tinfo& si, const slang::ast::Expression& item,
                                           slang::ast::CaseStatementCondition cond_kind) {
  using slang::ast::CaseStatementCondition;

  if (cond_kind == CaseStatementCondition::WildcardJustZ || cond_kind == CaseStatementCondition::WildcardXOrZ) {
    if (auto cv = try_eval(item); cv && cv->isInteger()) {
      const auto& sv = cv->integer();
      if (sv.hasUnknown()) {
        auto nbits = static_cast<int>(sv.getBitWidth());
        if (nbits > 64) {
          emit_unsupported(item.sourceRange, "wide-wildcard-case", "wildcard case items wider than 64 bits");
          return "0";
        }
        uint64_t mask = 0, val = 0;
        bool     has_x_nonwild = false;
        for (int i = 0; i < nbits; ++i) {
          auto b = sv[i];
          if (b.isUnknown()) {
            const bool is_z = b.value == slang::logic_t::Z_VALUE;
            if (cond_kind == CaseStatementCondition::WildcardXOrZ || is_z) {
              continue;  // wildcard bit
            }
            has_x_nonwild = true;  // x bit in casez: can never match two-state
            continue;
          }
          mask |= 1ULL << i;
          if (b.value != 0) {
            val |= 1ULL << i;
          }
        }
        if (has_x_nonwild) {
          emit_warning(item.sourceRange, "casez-x-item", "comptime", "x bits in a casez item never match (two-state)");
          return "0";
        }
        auto sp     = to_pattern(sel, si.bits, si.is_signed);
        auto masked = builder_.create_bit_and_stmts(sp, std::to_string(mask));
        return builder_.create_eq_stmts(masked, std::to_string(val));
      }
    }
  }

  return builder_.create_eq_stmts(sel, lower_rvalue(item));
}

void Slang_context::lower_case(const slang::ast::CaseStatement& stmt) {
  using slang::ast::CaseStatementCondition;
  using slang::ast::UniquePriorityCheck;

  if (stmt.condition == CaseStatementCondition::Inside) {
    emit_unsupported(stmt.sourceRange, "unsupported-case-inside", "case ... inside is not supported by --reader slang yet");
    return;
  }

  auto si  = tinfo(*stmt.expr.type);
  auto sel = lower_rvalue(stmt.expr);

  // Decide if/unique_if. unique/unique0 declare the arms disjoint; plain
  // `case` with all-constant pairwise-disjoint items is provably disjoint and
  // also lowers to unique_if (-> one Hotmux). Anything else keeps priority
  // (first-match) semantics as a flat if/elif chain.
  bool unique = stmt.check == UniquePriorityCheck::Unique || stmt.check == UniquePriorityCheck::Unique0;
  if (!unique) {
    std::vector<std::pair<uint64_t, uint64_t>> seen;  // (mask, value-under-mask)
    bool                                       all_const = true;
    bool                                       disjoint  = true;
    for (const auto& group : stmt.items) {
      for (const auto* item : group.expressions) {
        auto cv = try_eval(*item);
        if (!cv || !cv->isInteger() || cv->integer().getBitWidth() > 64) {
          all_const = false;
          break;
        }
        const auto& sv    = cv->integer();
        auto        nbits = static_cast<int>(sv.getBitWidth());
        uint64_t    mask = 0, val = 0;
        for (int i = 0; i < nbits && i < 64; ++i) {
          auto b = sv[i];
          if (b.isUnknown()) {
            if (stmt.condition == CaseStatementCondition::Normal) {
              all_const = false;  // x/z in a plain case item: never matches; keep priority lowering
            }
            continue;
          }
          mask |= 1ULL << i;
          if (b.value != 0) {
            val |= 1ULL << i;
          }
        }
        if (nbits < 64) {
          mask |= ~((1ULL << nbits) - 1);  // bits above the literal are zero-extended
        }
        for (const auto& [m2, v2] : seen) {
          uint64_t common = mask & m2;
          if ((val & common) == (v2 & common)) {
            disjoint = false;
            break;
          }
        }
        seen.emplace_back(mask, val);
        if (!disjoint) {
          break;
        }
      }
      if (!all_const || !disjoint) {
        break;
      }
    }
    unique = all_const && disjoint;
  }

  // Pre-compute every arm's match condition BEFORE the if node. A `uif`
  // references all arm conditions in its header, so (exactly like
  // lower_conditional) their defining statements must precede the if — else
  // tolg meets a forward ref, wires nil, and the Hotmux selector folds to a
  // non-const that trips cprop's `is_just_i64` assertion.
  std::vector<std::string> arm_conds;
  arm_conds.reserve(stmt.items.size());
  for (const auto& group : stmt.items) {
    std::string arm_cond;
    for (const auto* item : group.expressions) {
      auto m   = case_item_match(sel, si, *item, stmt.condition);
      arm_cond = arm_cond.empty() ? m : builder_.create_log_or_stmts(arm_cond, m);
    }
    arm_conds.emplace_back(std::move(arm_cond));
  }

  set_pending_loc(stmt.sourceRange);
  auto if_nid = builder_.create_if_stmt(unique);
  clear_pending_loc();

  size_t ai = 0;
  for (const auto& group : stmt.items) {
    const auto& arm_cond = arm_conds[ai++];
    if (arm_cond.empty()) {
      continue;
    }
    builder_.add_if_cond(if_nid, arm_cond);
    auto arm_stmts = builder_.add_if_stmts(if_nid);
    builder_.push_stmts(arm_stmts);
    lower_statement(*group.stmt);
    builder_.pop_stmts();
  }

  if (stmt.defaultCase != nullptr) {
    auto else_stmts = builder_.add_if_stmts(if_nid);
    builder_.push_stmts(else_stmts);
    lower_statement(*stmt.defaultCase);
    builder_.pop_stmts();
  } else if (unique) {
    // unique_if requires the else arm; an empty one keeps prior values.
    builder_.add_if_stmts(if_nid);
  }
}

bool Slang_context::unroll_tick(const slang::ast::Statement& stmt) {
  if (--unroll_budget_ > 0) {
    return true;
  }
  emit_error(stmt.sourceRange,
             "unroll-limit",
             "comptime",
             std::format("loop unroll limit of {} exhausted", options_.unroll_limit),
             "make the loop bounds compile-time constants, or raise --set inou.verilog.unroll_limit");
  return false;
}

void Slang_context::lower_for_loop(const slang::ast::ForLoopStatement& stmt) {
  if (!eval_ctx_) {
    emit_unsupported(stmt.sourceRange, "unsupported-loop", "for loop outside an evaluable context");
    return;
  }

  // Bind the loop variables as EvalContext locals so the stop/step
  // expressions and body reads of them constant-fold (tier 1).
  std::vector<const slang::ast::ValueSymbol*> locals;
  for (const auto* lv : stmt.loopVars) {
    slang::ConstantValue init;
    if (const auto* ie = lv->getInitializer()) {
      if (auto cv = try_eval(*ie)) {
        init = *cv;
      }
    }
    if (init.bad()) {
      emit_error(stmt.sourceRange, "non-const-loop-init", "comptime", "for-loop initializer must be compile-time constant");
      return;
    }
    eval_ctx_->createLocal(lv, init);
    locals.push_back(lv);
  }
  if (stmt.loopVars.empty()) {
    // `for (i = 0; ...)` over an existing genvar-style variable
    for (const auto* ie : stmt.initializers) {
      if (!try_eval(*ie)) {  // eval applies the assignment to a local
        emit_error(stmt.sourceRange, "non-const-loop-init", "comptime", "for-loop initializer must be compile-time constant");
        return;
      }
    }
  }

  while (true) {
    if (stmt.stopExpr != nullptr) {
      auto cv = try_eval(*stmt.stopExpr);
      if (!cv) {
        emit_error(stmt.stopExpr->sourceRange,
                   "non-const-loop-bound",
                   "comptime",
                   "for-loop condition must be compile-time constant to unroll",
                   "only constant-bounded loops are synthesizable by --reader slang");
        break;
      }
      if (!cv->isTrue()) {
        break;
      }
    }
    if (!unroll_tick(stmt)) {
      break;
    }

    lower_statement(stmt.body);

    bool stepped = true;
    for (const auto* se : stmt.steps) {
      if (!try_eval(*se)) {
        emit_error(se->sourceRange, "non-const-loop-step", "comptime", "for-loop step must be compile-time evaluable");
        stepped = false;
        break;
      }
    }
    if (!stepped) {
      break;
    }
    if (stmt.stopExpr == nullptr && stmt.steps.empty()) {
      emit_error(stmt.sourceRange, "loop-no-progress", "comptime", "for loop without condition or step cannot unroll");
      break;
    }
  }

  for (const auto* lv : locals) {
    eval_ctx_->deleteLocal(lv);
  }
}

void Slang_context::lower_while_loop(const slang::ast::Statement& stmt) {
  const slang::ast::Expression* cond     = nullptr;
  const slang::ast::Statement*  body     = nullptr;
  bool                          at_least = false;
  int64_t                       count    = -1;

  if (stmt.kind == StatementKind::WhileLoop) {
    const auto& w = stmt.as<slang::ast::WhileLoopStatement>();
    cond          = &w.cond;
    body          = &w.body;
  } else if (stmt.kind == StatementKind::DoWhileLoop) {
    const auto& w = stmt.as<slang::ast::DoWhileLoopStatement>();
    cond          = &w.cond;
    body          = &w.body;
    at_least      = true;
  } else {
    const auto& r = stmt.as<slang::ast::RepeatLoopStatement>();
    auto        c = try_eval_int(r.count);
    if (!c) {
      emit_error(stmt.sourceRange, "non-const-repeat", "comptime", "repeat count must be compile-time constant");
      return;
    }
    count = *c;
    body  = &r.body;
  }

  if (count >= 0) {
    for (int64_t i = 0; i < count; ++i) {
      if (!unroll_tick(stmt)) {
        return;
      }
      lower_statement(*body);
    }
    return;
  }

  // while/do-while: unrollable only when the condition stays compile-time
  // (it must depend on locals the body steps via blocking const writes).
  bool first = true;
  while (true) {
    if (!first || !at_least) {
      auto cv = try_eval(*cond);
      if (!cv) {
        emit_unsupported(cond->sourceRange, "non-const-while",
                         "while loops with non-constant conditions are not supported by --reader slang");
        return;
      }
      if (!cv->isTrue()) {
        return;
      }
    }
    first = false;
    if (!unroll_tick(stmt)) {
      return;
    }
    lower_statement(*body);
  }
}

void Slang_context::lower_foreach(const slang::ast::ForeachLoopStatement& stmt) {
  if (!eval_ctx_) {
    emit_unsupported(stmt.sourceRange, "unsupported-loop", "foreach outside an evaluable context");
    return;
  }

  // Recursive per-dimension unroll with the loop vars bound as eval locals.
  std::vector<const slang::ast::ForeachLoopStatement::LoopDim*> dims;
  for (const auto& d : stmt.loopDims) {
    dims.push_back(&d);
  }

  std::function<void(size_t)> recurse = [&](size_t level) {
    if (level == dims.size()) {
      lower_statement(stmt.body);
      return;
    }
    const auto& dim = *dims[level];
    if (!dim.range) {
      emit_unsupported(stmt.sourceRange, "dynamic-foreach", "foreach over a dynamically sized dimension");
      return;
    }
    if (dim.loopVar == nullptr) {  // skipped dimension `[, j]`
      recurse(level + 1);
      return;
    }
    eval_ctx_->createLocal(dim.loopVar);
    for (int32_t i = dim.range->lower(); i <= dim.range->upper(); ++i) {
      if (!unroll_tick(stmt)) {
        break;
      }
      *eval_ctx_->findLocal(dim.loopVar) = slang::SVInt(32, static_cast<uint64_t>(i), true);
      recurse(level + 1);
    }
    eval_ctx_->deleteLocal(dim.loopVar);
  };
  recurse(0);
}
