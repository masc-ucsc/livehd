//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Statement lowering (todo/ 2s subtask B). Structured statements stay
// structured: if/case become LNAST if/unique_if tree nodes and the upass SSA
// machinery does branch merging. Loops are const-evaluated and unrolled
// slang-side with a capped, diagnosed budget (yosys-slang
// UnrollLimitTracking is the model): loop variables become EvalContext
// locals so tier-1 constant folding sees them.

#include <format>
#include <functional>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang_context.hpp"

using slang::ast::StatementKind;

namespace {
// Defined below, next to the other loop helpers; declared here because the
// statement-list lowering needs it. (Two anonymous namespaces in one TU are
// the same namespace, so this is a plain forward declaration.)
bool subtree_has_break(const slang::ast::Statement& stmt);
}  // namespace

void Slang_context::lower_statement(const slang::ast::Statement& stmt) {
  switch (stmt.kind) {
    case StatementKind::Empty: return;
    case StatementKind::List : {
      // A `break` ends the CURRENT ITERATION as well as the loop, so everything
      // after it in this list must not run when the break was taken.
      // lower_for_loop's `broken` flag only guards LATER iterations; guard the
      // REMAINDER of this list on the same flag. Without it,
      //   for (i = 0; i < 4; i++) begin  if (stop[i]) break;  acc += d[i];  end
      // still accumulates d[i] in the breaking iteration.
      //
      // `break_flags_.back()` is exactly the flag the Break statement raises
      // (see StatementKind::Break below), and subtree_has_break deliberately
      // does not look inside a nested loop, so the flag and the break always
      // belong to the same loop -- PROVIDED the nearest enclosing loop is the
      // one that owns that flag. It is not when the nearest loop is ROLLED (an
      // LNAST `for`: uPass's func_break already ends the iteration, and the
      // flag on the stack belongs to some OUTER unrolled loop) or when it
      // pushed the "no flag of my own" sentinel (while/repeat/foreach). Guard
      // on neither: `own_brk_flag()` returns the flag only when it is ours.
      const auto& list = stmt.as<slang::ast::StatementList>().list;
      size_t      arms = 0;
      for (size_t i = 0; i < list.size(); ++i) {
        lower_statement(*list[i]);
        const auto* flag = own_brk_flag();
        if (flag == nullptr || i + 1 >= list.size() || !subtree_has_break(*list[i])) {
          continue;
        }
        auto guard = builder_.create_eq_stmts(*flag, "0");
        auto if_id = builder_.create_if_stmt(false);
        builder_.add_if_cond(if_id, guard);
        builder_.push_stmts(builder_.add_if_stmts(if_id));
        ++arms;
      }
      while (arms-- > 0) {
        builder_.pop_stmts();
      }
      return;
    }
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
              || name == "$displayb" || name == "$displayh" || name == "$displayo" || name == "$dumpfile" || name == "$dumpvars"
              || name == "$finish" || name == "$stop" || name == "$fatal") {
            return;
          }
          emit_unsupported(stmt.sourceRange,
                           "unsupported-system-task",
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
        // A scalar packed-struct local (`automatic wb_t _GEN = '{...}`, e.g.
        // firtool StageReg `_GEN`/`_GEN_1`) is lowered to per-field leaf nets,
        // so its initializer must write each LEAF — exactly like the continuous
        // `assign x = '{...}` / net-initializer paths (slang_structure.cpp). A
        // whole `create_assign_stmts(_GEN, …)` would write the bundle NAME but
        // leave the leaves (`_GEN.toreg`, …) undriven; a later whole read
        // (`read_struct_whole`, used when the struct is a field value of an
        // outer `'{…, wb_ctrl: _GEN}`) reassembles from those leaves and gets
        // nil/poison. assign_struct_whole splits the literal/expression RHS into
        // the matching per-field leaf stores.
        if (is_scalar_struct_var(vds.symbol) && assign_struct_whole(vds.symbol, *init)) {
          clear_pending_loc();
          return;
        }
        // Materialize to an integer like lower_assign: a local `reg b = x && y`
        // initializer is an LNAST bool, but the variable is integer-typed, so
        // store 0/1 (else a later integer use, e.g. `b != 0`, fails typecheck).
        auto v = to_int_value(lower_rvalue(*init));
        note_write(vds.symbol, /*nonblocking=*/false, stmt.sourceRange.start());
        builder_.create_assign_stmts(lname_of(vds.symbol), v);
        clear_pending_loc();
      }
      return;
    }
    case StatementKind::Conditional: lower_conditional(stmt.as<slang::ast::ConditionalStatement>()); return;
    case StatementKind::Case       : lower_case(stmt.as<slang::ast::CaseStatement>()); return;
    case StatementKind::ForLoop    : lower_for_loop(stmt.as<slang::ast::ForLoopStatement>()); return;
    case StatementKind::WhileLoop  :
    case StatementKind::DoWhileLoop:
    case StatementKind::RepeatLoop : lower_while_loop(stmt); return;
    case StatementKind::ForeachLoop: lower_foreach(stmt.as<slang::ast::ForeachLoopStatement>()); return;
    case StatementKind::Timed      : {
      const auto& timed = stmt.as<slang::ast::TimedStatement>();
      if (timed.timing.kind == slang::ast::TimingControlKind::Delay) {
        emit_warning(stmt.sourceRange, "delay-ignored", "unsupported", "#delay is ignored (synthesis semantics)");
        lower_statement(timed.stmt);
        return;
      }
      emit_unsupported(stmt.sourceRange,
                       "unsupported-timing",
                       "event controls inside a process body are not supported by --reader slang");
      return;
    }
    case StatementKind::ImmediateAssertion: lower_immediate_assertion(stmt.as<slang::ast::ImmediateAssertionStatement>()); return;
    case StatementKind::ConcurrentAssertion:
      emit_warning(stmt.sourceRange, "assertion-ignored", "unsupported", "concurrent assertion ignored (synthesis semantics)");
      return;
    case StatementKind::Return: {
      // Inside an inlined function body, `return expr` assigns the result var.
      if (in_function_call_ && func_ret_sym_ != nullptr) {
        const auto* re = stmt.as<slang::ast::ReturnStatement>().expr;
        if (re != nullptr) {
          set_pending_loc(stmt.sourceRange);
          // A Verilog function returns a bit-vector (`logic`/`bit`/`int`), never
          // a distinct bool: coerce a boolean result (e.g. `return a == b;`) to
          // an integer so the result var is integer-kind. Otherwise a caller's
          // `f(x) != 0` later trips the bool-vs-int comparison type check.
          auto       v                = to_int_value(lower_rvalue(*re));
          // A struct-typed return var is declared as a per-field bundle
          // (inline_call -> declare_value_symbol), so a flat whole store would
          // be dead: every read of the result routes through the leaves. Split
          // the value onto the leaves (same as assign_to's NamedValue path);
          // only a non-bundle result var takes the flat assign.
          const bool saved_nb         = current_assign_nonblocking_;
          current_assign_nonblocking_ = false;  // a function-body return is a blocking write
          if (!assign_struct_whole_value(*func_ret_sym_, v, stmt.sourceRange.start())) {
            note_write(*func_ret_sym_, /*nonblocking=*/false, stmt.sourceRange.start());
            builder_.create_assign_stmts(lname_of(*func_ret_sym_), v);
          }
          current_assign_nonblocking_ = saved_nb;
          clear_pending_loc();
        }
        return;
      }
      emit_unsupported(stmt.sourceRange,
                       "unsupported-jump",
                       std::string(slang::ast::toString(stmt.kind)) + " is not supported in this context by --reader slang");
      return;
    }
    case StatementKind::Break:
      // A ROLLED loop (an LNAST `for`) carries the break as a marker node and
      // uPass lowers it to `loop_exec = false; loop_next_active = false` — the
      // real semantics, including "the rest of THIS iteration does not run",
      // which the unrolled `broken`-flag form below can only approximate.
      if (!loop_rolled_.empty() && loop_rolled_.back()) {
        emit_loop_marker(Lnast_ntype::create_func_break());
        return;
      }
      // Inside an unrolled loop whose body contains a break, the loop set up a
      // `broken` flag and guards every iteration on it (see lower_for_loop);
      // the break itself just raises the flag. It sits wherever it was written
      // — typically an `if` arm — so the raise inherits that condition, which
      // is exactly the priority-select semantics ("first match wins").
      //
      // own_brk_flag() is null when the NEAREST loop is not the flag's owner (a
      // while/repeat/foreach pushed the sentinel): raising an OUTER loop's flag
      // there would terminate the wrong loop, so refuse instead.
      if (const auto* flag = own_brk_flag(); flag != nullptr) {
        builder_.create_assign_stmts(*flag, "1");
        return;
      }
      emit_unsupported(stmt.sourceRange,
                       "unsupported-jump",
                       std::string(slang::ast::toString(stmt.kind)) + " is not supported in this context by --reader slang");
      return;
    case StatementKind::Continue:
      // A ROLLED loop gets it for free: uPass lowers the marker to
      // `loop_exec = false` WITHOUT clearing loop_next_active, so this
      // iteration stops and every later one still runs.
      if (!loop_rolled_.empty() && loop_rolled_.back()) {
        emit_loop_marker(Lnast_ntype::create_func_continue());
        return;
      }
      // Unrolled, `continue` skips the REST of one iteration, which needs a
      // per-iteration flag rather than the loop-wide one break uses. Not
      // implemented — refuse rather than lower it as a break, which would
      // silently drop every later iteration.
      emit_unsupported(stmt.sourceRange,
                       "unsupported-jump",
                       std::string(slang::ast::toString(stmt.kind)) + " is not supported in this context by --reader slang");
      return;
    default:
      emit_unsupported(stmt.sourceRange,
                       "unsupported-statement",
                       std::string("statement kind '") + std::string(slang::ast::toString(stmt.kind))
                           + "' is not supported by --reader slang yet");
  }
}

// A SystemVerilog IMMEDIATE assertion becomes a design obligation, the same
// LNAST `cassert` node a Pyrope `assert` produces — tolg materializes it as an
// `fproperty` Sub, pass.formal proves what it can and cgen emits a runtime
// check for the rest.
//
// This used to be dropped on the floor ("synthesis ignores immediate
// assertions"), silently: a design whose every property was an immediate assert
// verified as `no assert/assert_always obligations found` — a run that proves
// nothing while exiting 0. riscv-formal's generated checks are exactly that
// shape (48 immediate asserts in one testbench), so the drop was the difference
// between checking a RISC-V core and checking nothing.
//
// `assume` is a hypothesis (prove-then-use, per the fcore contract), so it
// carries the `__fkind__assume` sentinel the Pyrope front-end uses. `cover` is
// a coverage COUNT, not an obligation, and there is nothing to prove — it stays
// ignored, but says so.
void Slang_context::lower_immediate_assertion(const slang::ast::ImmediateAssertionStatement& stmt) {
  using slang::ast::AssertionKind;

  if (stmt.assertionKind == AssertionKind::CoverProperty || stmt.assertionKind == AssertionKind::CoverSequence) {
    emit_warning(stmt.sourceRange, "cover-ignored", "unsupported", "immediate cover is a count, not an obligation — ignored");
    return;
  }
  // A DEFERRED assertion (`assert #0` / `assert final`) fires in the observed/
  // final simulation region, after glitches settle. That is a simulation-time
  // semantics this static obligation cannot reproduce, so refuse it rather than
  // prove something subtly different.
  if (stmt.isDeferred) {
    emit_warning(stmt.sourceRange,
                 "deferred-assertion-ignored",
                 "unsupported",
                 "deferred assertion (#0 / final) has simulation-region semantics — ignored");
    return;
  }

  set_pending_loc(stmt.sourceRange);
  // `booleanize`: pyrope keeps bool and int apart and a cassert condition is a
  // bool, but `assert(flag)` over a 1-bit net lowers to an integer.
  auto cond = booleanize(lower_rvalue(stmt.cond));
  auto idx  = builder_.add_child(Lnast_ntype::create_cassert());
  builder_.add_value_child_pub(idx, cond);
  if (stmt.assertionKind == AssertionKind::Assume) {
    // UNQUOTED sentinel, matching prp2lnast: a user message is always a string
    // const, so this can never collide with one.
    builder_.add_child(idx, Lnast_node::create_const("__fkind__assume"));
  }
  clear_pending_loc();
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
    // If the THEN lowered to NOTHING (an empty `begin end`, or a dropped
    // `q <= q` self-assign hold), an `if(cond){}else{X}` leaves an
    // empty-then branch that downstream mishandles: constprop collapses it to
    // `if(cond){X}` (losing the negation) and tolg drops the guard entirely —
    // both flip the effective condition (LEC mismatch, e.g. SoomRV FIFO). Emit
    // the else as a negated then-only `if(!cond){X}` instead; the harmless
    // empty `if(cond){}` is DCE'd. (`cond` is a bool from booleanize.)
    const bool then_empty = builder_.lnast->get_first_child(then_stmts).is_invalid();
    if (then_empty) {
      auto neg = mark_bool(builder_.create_log_not_stmts(cond));
      set_pending_loc(stmt.sourceRange);
      auto neg_if = builder_.create_if_stmt(false);
      clear_pending_loc();
      builder_.add_if_cond(neg_if, neg);
      auto neg_then = builder_.add_if_stmts(neg_if);
      builder_.push_stmts(neg_then);
      lower_statement(*stmt.ifFalse);
      builder_.pop_stmts();
    } else {
      auto else_stmts = builder_.add_if_stmts(if_nid);
      builder_.push_stmts(else_stmts);
      lower_statement(*stmt.ifFalse);
      builder_.pop_stmts();
    }
  }
}

// Build the per-item match condition for a case arm expression. `sel` is the
// lowered selector, `si` its type info. Wildcard bits (z for casez, x+z for
// casex) compare only the known bits.
std::string Slang_context::case_item_match(const std::string& sel, const Tinfo& si, const slang::ast::Expression& item,
                                           slang::ast::CaseStatementCondition cond_kind) {
  using slang::ast::CaseStatementCondition;

  // `case (x) inside { ..., [lo:hi], ... }`: a range item matches when
  // lo <= x <= hi (inclusive). Only appears under the Inside condition.
  if (item.kind == slang::ast::ExpressionKind::ValueRange) {
    const auto& vr = item.as<slang::ast::ValueRangeExpression>();
    if (vr.rangeKind != slang::ast::ValueRangeKind::Simple) {
      emit_unsupported(item.sourceRange,
                       "unsupported-case-inside",
                       "tolerance ranges ([a +/- b]) in case-inside are not supported by --reader slang");
      return "0";
    }
    auto sp = to_pattern(sel, si.bits, si.is_signed);
    auto lo = lower_rvalue(vr.left());
    auto hi = lower_rvalue(vr.right());
    auto ge = builder_.create_ge_stmts(sp, lo);
    auto le = builder_.create_le_stmts(sp, hi);
    return builder_.create_log_and_stmts(ge, le);
  }

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
        if (nbits < 64) {
          // Bits above the item literal are zero-extended: a wider selector must
          // have them clear to match. The disjointness pre-scan in lower_case
          // already assumes this — extend the mask here so the EMITTED compare
          // agrees (else a narrow casez item vs a wider selector ignores the
          // selector's high bits and the two lowerings disagree).
          mask |= ~((1ULL << nbits) - 1);
        }
        auto sp     = to_pattern(sel, si.bits, si.is_signed);
        auto masked = builder_.create_bit_and_stmts(sp, std::to_string(mask));
        return builder_.create_eq_stmts(masked, std::to_string(val));
      }
    }
  }

  // to_int_value: a case ITEM may lower to a boolean (`case (1'b1) a || b:` --
  // the one-hot idiom every Verilog CPU uses), while the selector is an integer.
  // Verilog compares 1-bit values here; Pyrope's `==` has no implicit bool/int
  // conversion, so without this the arm dies as `type-mismatch-eq` ("<const>:
  // integer vs %N:boolean"). Integers pass through unchanged, so this is a no-op
  // for the ordinary `case (x) 3:` shape. Same idiom as the `inside` operator
  // (slang_expr.cpp).
  return builder_.create_eq_stmts(sel, to_int_value(lower_rvalue(item)));
}

void Slang_context::lower_case(const slang::ast::CaseStatement& stmt) {
  using slang::ast::CaseStatementCondition;
  using slang::ast::UniquePriorityCheck;

  auto si  = tinfo(*stmt.expr.type);
  // to_int_value for the mirror shape `case (a || b) 1'b1:` — a boolean selector
  // against integer items. No-op when the selector is already an integer.
  auto sel = to_int_value(lower_rvalue(stmt.expr));

  // Decide if/unique_if. unique/unique0 declare the arms disjoint; plain
  // `case` with all-constant pairwise-disjoint items is provably disjoint and
  // also lowers to unique_if (-> one Hotmux). Anything else keeps priority
  // (first-match) semantics as a flat if/elif chain.
  //
  // The const scan below ALSO answers "do the arms cover every selector value"
  // (see `exhaustive`), which a plain `case (sel1b) 1'b0: … 1'b1: …` does even
  // with no default — so it runs unconditionally, not only when `unique` is
  // still undecided.
  const bool unique_declared = stmt.check == UniquePriorityCheck::Unique || stmt.check == UniquePriorityCheck::Unique0;
  bool       unique          = unique_declared;
  bool       exhaustive      = false;
  {
    std::vector<std::pair<uint64_t, uint64_t>> seen;  // (mask, value-under-mask)
    bool                                       all_const = true;
    bool                                       disjoint  = true;
    bool                                       all_exact = true;  // no wildcard bits in any item
    uint64_t                                   n_items   = 0;
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
        if (mask != ~0ULL) {
          all_exact = false;  // wildcard bits: this item covers a value RANGE, not one value
        }
        ++n_items;
        seen.emplace_back(mask, val);
        if (!disjoint) {
          break;
        }
      }
      if (!all_const || !disjoint) {
        break;
      }
    }
    unique = unique_declared || (all_const && disjoint);
    // Exhaustive: every arm is one exact constant, the arms are pairwise
    // distinct, and there are as many of them as the selector has values. Then
    // the missing `default` is UNREACHABLE, so an else assigning don't-care is
    // behavior-preserving — and it stops an `always @*` from inferring a latch
    // (picorv32: `case (reg_op1[1]) 1'b0: … 1'b1: …` inside a full_case arm).
    // The width cap keeps 1ULL << bits well-defined and the count sane.
    exhaustive
        = all_const && disjoint && all_exact && si.bits > 0 && si.bits < 20 && n_items == (1ULL << static_cast<unsigned>(si.bits));
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
  } else if (has_full_case_attr(stmt) || exhaustive) {
    emit_full_case_else(stmt, if_nid);
  } else if (unique) {
    // unique_if requires the else arm; an empty one keeps prior values.
    builder_.add_if_stmts(if_nid);
  }
}

bool Slang_context::has_full_case_attr(const slang::ast::Statement& stmt) const {
  if (body_ == nullptr) {
    return false;
  }
  for (const auto* attr : body_->getCompilation().getAttributes(stmt)) {
    // `(* full_case *)` is the bare-name form (value 1); `(* full_case = 0 *)`
    // explicitly opts out, so honor the value when one is given.
    if (attr->name == "full_case") {
      auto cv = attr->getValue();
      return !cv.isInteger() || cv.isTrue();
    }
  }
  return false;
}

namespace {
// The variables a case arm writes. Same shape as slang_structure.cpp's
// Write_collector (file-local there); only the write SET matters here, so
// blocking/nonblocking are not split. Insertion ORDER is kept: the emitted
// else arm must be deterministic (//inou/slang:slang_emit_determinism).
struct Fullcase_write_collector : public slang::ast::ASTVisitor<Fullcase_write_collector, slang::ast::VisitFlags::AllGood> {
  std::vector<const slang::ast::ValueSymbol*>         order;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> seen;

  void note(const slang::ast::Expression& lhs) {
    if (lhs.kind == slang::ast::ExpressionKind::Concatenation) {
      for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
        note(*op);
      }
      return;
    }
    const auto* e = &lhs;
    while (true) {
      if (e->kind == slang::ast::ExpressionKind::ElementSelect) {
        e = &e->as<slang::ast::ElementSelectExpression>().value();
      } else if (e->kind == slang::ast::ExpressionKind::RangeSelect) {
        e = &e->as<slang::ast::RangeSelectExpression>().value();
      } else if (e->kind == slang::ast::ExpressionKind::MemberAccess) {
        e = &e->as<slang::ast::MemberAccessExpression>().value();
      } else {
        break;
      }
    }
    if (e->kind != slang::ast::ExpressionKind::NamedValue) {
      return;
    }
    const auto* sym = &e->as<slang::ast::NamedValueExpression>().symbol;
    if (seen.insert(sym).second) {
      order.emplace_back(sym);
    }
  }

  void handle(const slang::ast::AssignmentExpression& expr) {
    note(expr.left());
    visitDefault(expr);
  }
};
}  // namespace

void Slang_context::emit_full_case_else(const slang::ast::CaseStatement& stmt, const Lnast_nid& if_nid) {
  Fullcase_write_collector wc;
  for (const auto& group : stmt.items) {
    group.stmt->visit(wc);
  }

  auto else_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(else_stmts);
  for (const auto* sym : wc.order) {
    // Width-matched unknown: `0ub????`. A bare `0ub?` would be one bit wide and
    // the assignment would narrow the variable.
    auto ti = tinfo(sym->getType());
    if (ti.bits <= 0) {
      continue;  // non-integral (real/string/...): nothing sensible to don't-care
    }
    builder_.create_assign_stmts(lname_of(*sym), absl::StrCat("0ub", std::string(static_cast<size_t>(ti.bits), '?')));
  }
  builder_.pop_stmts();
}

bool Slang_context::unroll_tick(const slang::ast::Statement& stmt) {
  if (--unroll_budget_ > 0) {
    return true;
  }
  emit_error(stmt.sourceRange,
             "unroll-limit",
             "comptime",
             std::format("loop unroll limit of {} exhausted", options_.unroll_limit),
             "make the loop bounds compile-time constants, or raise --set compile.slang.unroll_limit");
  return false;
}

namespace {
// Does this statement subtree contain a `break` that binds to THIS loop? A
// nested loop's body is skipped: a break in there belongs to the inner loop.
bool subtree_has_break(const slang::ast::Statement& stmt) {
  bool found = false;
  auto v     = slang::ast::makeVisitor(
      [&](auto& visitor, const slang::ast::BreakStatement&) {
        found = true;
        (void)visitor;
      },
      [&](auto&, const slang::ast::ForLoopStatement&) {},  // inner loop owns its breaks
      [&](auto&, const slang::ast::WhileLoopStatement&) {},
      [&](auto&, const slang::ast::RepeatLoopStatement&) {},
      [&](auto&, const slang::ast::ForeachLoopStatement&) {});
  stmt.visit(v);
  return found;
}

const slang::ast::Expression* peel_loop_expr(const slang::ast::Expression& raw) {
  const auto* e = &raw;
  while (e->kind == slang::ast::ExpressionKind::Conversion) {
    e = &e->as<slang::ast::ConversionExpression>().operand();
  }
  return e;
}
}  // namespace

// A bare loop-control marker (`break` / `continue`), in the shape prp2lnast
// emits: the node plus one fresh tmp ref, no argument.
void Slang_context::emit_loop_marker(Lnast_ntype::Lnast_ntype_int head) {
  auto  idx = builder_.add_child(head);
  auto& ln  = *builder_.lnast;
  ln.add_child(idx, builder_.mint_tmp_ref());
}

bool Slang_context::lower_deferred_for(const slang::ast::ForLoopStatement& stmt) {
  using slang::ast::BinaryOperator;
  using slang::ast::ExpressionKind;
  using slang::ast::UnaryOperator;

  // A body containing `break` used to be refused here and sent to the unroller.
  // It no longer needs to be: the loop is emitted as an LNAST `for`, and uPass
  // owns break/continue (func_break/func_continue -> loop_exec/loop_next_active).
  if (stmt.loopVars.size() != 1 || stmt.stopExpr == nullptr || stmt.steps.size() != 1) {
    return false;
  }
  const auto* ivar = stmt.loopVars[0];
  const auto* init = ivar->getInitializer();
  if (init == nullptr) {
    return false;
  }

  const auto* stop = peel_loop_expr(*stmt.stopExpr);
  if (stop->kind != ExpressionKind::BinaryOp) {
    return false;
  }
  const auto& cmp = stop->as<slang::ast::BinaryExpression>();
  if (cmp.op != BinaryOperator::LessThan && cmp.op != BinaryOperator::LessThanEqual) {
    return false;
  }
  const auto* cmp_lhs = peel_loop_expr(cmp.left());
  if (cmp_lhs->kind != ExpressionKind::NamedValue || &cmp_lhs->as<slang::ast::NamedValueExpression>().symbol != ivar) {
    return false;
  }

  const auto* step = peel_loop_expr(*stmt.steps[0]);
  if (step->kind != ExpressionKind::UnaryOp) {
    return false;
  }
  const auto& inc = step->as<slang::ast::UnaryExpression>();
  if (inc.op != UnaryOperator::Preincrement && inc.op != UnaryOperator::Postincrement) {
    return false;
  }
  const auto* inc_arg = peel_loop_expr(inc.operand());
  if (inc_arg->kind != ExpressionKind::NamedValue || &inc_arg->as<slang::ast::NamedValueExpression>().symbol != ivar) {
    return false;
  }

  auto start = to_int_value(lower_rvalue(*init));
  auto end   = to_int_value(lower_rvalue(cmp.right()));
  if (cmp.op == BinaryOperator::LessThan) {
    end = builder_.create_minus_stmts(end, "1");  // LNAST ranges are inclusive
  }

  auto& ln       = *builder_.lnast;
  auto  range    = builder_.add_child(Lnast_ntype::create_range());
  auto  range_id = builder_.create_lnast_tmp();
  ln.add_child(range, Lnast_node::create_ref(range_id));
  builder_.add_value_child_pub(range, start);
  builder_.add_value_child_pub(range, end);

  // PYROPE HAS NO SHADOWING, and the `for` node BINDS its index. By the time we
  // get here the process has usually already emitted this symbol's lazy
  // `mut <name> = 0sb?` declare, so binding the loop to that same name nests the
  // `for`'s binding inside the declare's scope. Written as Pyrope that is a hard
  // error --
  //     variable shadowing: 'i' is already declared in an enclosing scope
  // -- but slang builds LNAST directly and never meets the parser check, so it
  // used to sail through and be mis-lowered instead: uPass moved the statements
  // that PRECEDED the loop after it, which silently dropped a pre-loop
  // initialization (`o = 8'd7; for (…) o = o + d[i];` lost the 7). Bind a FRESH
  // name the process cannot have declared, and point every body read of the
  // symbol at it for as long as we are inside the body.
  auto        loop_name = fresh_local("idx");
  const auto  prev_it   = sym_lname_.find(ivar);
  const bool  had_prev  = prev_it != sym_lname_.end();
  std::string prev_name = had_prev ? prev_it->second : std::string{};
  sym_lname_[ivar]      = loop_name;

  auto loop = builder_.add_child(Lnast_ntype::create_for());
  ln.add_child(loop, Lnast_node::create_ref(loop_name));
  ln.add_child(loop, Lnast_node::create_ref(range_id));
  auto body = ln.add_child(loop, Lnast_ntype::create_stmts());

  const bool was_declared = declared_.contains(ivar);
  declared_.insert(ivar);  // the for node, rather than a declare, binds it
  builder_.push_stmts(body);
  loop_rolled_.push_back(true);  // break/continue in here become LNAST markers
  lower_statement(stmt.body);
  loop_rolled_.pop_back();
  builder_.pop_stmts();
  if (!was_declared) {
    declared_.erase(ivar);
  }
  if (had_prev) {
    sym_lname_[ivar] = prev_name;
  } else {
    sym_lname_.erase(ivar);
  }
  ln.add_child(loop, Lnast_node::create_const("val"));
  return true;
}

void Slang_context::lower_for_loop(const slang::ast::ForLoopStatement& stmt) {
  if (!eval_ctx_) {
    emit_unsupported(stmt.sourceRange, "unsupported-loop", "for loop outside an evaluable context");
    return;
  }

  // `--set compile.slang.roll_loops=true`: hand a canonical loop to LNAST intact
  // rather than unrolling it here. A non-canonical shape falls through to the
  // unroller below.
  if (options_.roll_loops && lower_deferred_for(stmt)) {
    return;
  }

  // Bind the loop variables as EvalContext locals so the stop/step
  // expressions and body reads of them constant-fold (tier 1).
  std::vector<const slang::ast::ValueSymbol*>                                           locals;
  // A counter declared OUTSIDE the for-header OUTLIVES the loop: SystemVerilog
  // leaves it at the value that failed the stop test (or, with a `break`, at
  // the breaking iteration's value), and real code READS it afterwards --
  // cva6's miss_handler indexes the AMO bypass port with it
  // (`bypass_ports_req[id] = amo_bypass_req` after `for (id = 0; id < NR_PORTS;
  // id++)`), and pmp's `if (i == NrPMPEntries)` no-match fallback tests it. The
  // unroll steps the counter COMPTIME only, so without an explicit write-back
  // every such read binds to the declare-time `0sb?` poison and the whole cone
  // folds to x -- silently, with a clean exit 0.
  //
  // Keep the assignment TARGET EXPRESSION, not just the symbol, so the
  // write-back rides the normal lvalue path (lazy declare + note_write +
  // struct/bundle-port split). A counter declared IN the header
  // (`for (int i = 0; ...)`) goes out of scope with the loop in SV too, so it
  // is deliberately NOT recorded here.
  std::vector<std::pair<const slang::ast::ValueSymbol*, const slang::ast::Expression*>> outlives;
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
    // `for (i = 0; ...)` over a variable declared OUTSIDE the for-header (e.g.
    // a module-scope `integer i`, as the firtool-generated SRAM models use).
    // slang doesn't list it in loopVars, and try_eval of `i = 0` fails until
    // `i` is an EvalContext local — so bind the assigned variable here, seeded
    // with the init value. The loop is unrolled, so the counter is comptime.
    for (const auto* ie : stmt.initializers) {
      if (ie->kind == slang::ast::ExpressionKind::Assignment) {
        const auto& assign = ie->as<slang::ast::AssignmentExpression>();
        if (assign.left().kind == slang::ast::ExpressionKind::NamedValue) {
          const auto& sym = assign.left().as<slang::ast::NamedValueExpression>().symbol;
          if (auto cv = try_eval(assign.right())) {
            eval_ctx_->createLocal(&sym, *cv);
            locals.push_back(&sym);
            outlives.emplace_back(&sym, &assign.left());
            continue;
          }
        }
      }
      if (!try_eval(*ie)) {  // eval applies the assignment to a local
        emit_error(stmt.sourceRange, "non-const-loop-init", "comptime", "for-loop initializer must be compile-time constant");
        return;
      }
    }
  }

  // slang sometimes cannot evaluate a bound that is semantically constant
  // only after function/config bindings reach uPass (CVA6's Cfg.Nr*Rules).
  // Preserve the canonical loop instead of rejecting it at this frontend.
  if (stmt.stopExpr != nullptr && !try_eval(*stmt.stopExpr)) {
    for (const auto* lv : locals) {
      eval_ctx_->deleteLocal(lv);
    }
    if (lower_deferred_for(stmt)) {
      return;
    }
    emit_error(stmt.stopExpr->sourceRange,
               "non-const-loop-bound",
               "comptime",
               "for-loop condition must be compile-time constant to unroll",
               "only canonical increasing loops can defer their bound to uPass");
    return;
  }

  // A `break` in the body (the priority-select idiom: `if (req[i]) break;`)
  // needs a runtime flag, since which iteration wins is not comptime. Only set
  // one up when the body actually has a break, so every other loop keeps its
  // previous emission byte for byte.
  std::string brk_flag;
  if (subtree_has_break(stmt.body)) {
    brk_flag = fresh_local("brk");
    builder_.create_declare_stmts(brk_flag, "mut", "", "");
    builder_.create_assign_stmts(brk_flag, "0");
  }
  // Push UNCONDITIONALLY (the empty sentinel when this loop owns no flag) so a
  // `break` that reaches this frame can never raise an OUTER loop's flag.
  break_flags_.push_back(brk_flag);

  // `<counter> = <its comptime value right now>`, emitted into whatever stmts
  // block is on top. Must run while the EvalContext local is still alive.
  const auto emit_counter_writeback = [&] {
    const bool saved_nb         = current_assign_nonblocking_;
    current_assign_nonblocking_ = false;  // a loop counter is a BLOCKING write
    for (const auto& [sym, lhs] : outlives) {
      const auto* cv = eval_ctx_->findLocal(sym);
      if (cv == nullptr || !cv->isInteger()) {
        continue;
      }
      set_pending_loc(stmt.sourceRange);
      assign_to(*lhs, const_text(cv->integer()));
      clear_pending_loc();
    }
    current_assign_nonblocking_ = saved_nb;
  };

  // An unroll that ABORTED (budget exhausted, non-const stop/step) leaves the
  // counter at a partial value; writing that back would ship a wrong constant
  // on top of an already-reported error.
  bool unroll_ok = true;

  loop_rolled_.push_back(false);  // this body is UNROLLED: break uses brk_flag
  while (true) {
    if (stmt.stopExpr != nullptr) {
      auto cv = try_eval(*stmt.stopExpr);
      if (!cv) {
        emit_error(stmt.stopExpr->sourceRange,
                   "non-const-loop-bound",
                   "comptime",
                   "for-loop condition must be compile-time constant to unroll",
                   "only constant-bounded loops are synthesizable by --reader slang");
        unroll_ok = false;
        break;
      }
      if (!cv->isTrue()) {
        break;
      }
    }
    if (!unroll_tick(stmt)) {
      unroll_ok = false;  // budget exhausted; unroll_tick already reported it
      break;
    }

    if (brk_flag.empty()) {
      lower_statement(stmt.body);
    } else {
      // `if (not broken) { <body> }` — an iteration after the break is taken
      // must not write anything. Assignments are last-wins in program order, so
      // without the guard every later iteration would overwrite the winner.
      auto guard = builder_.create_eq_stmts(brk_flag, "0");
      auto if_id = builder_.create_if_stmt(false);
      builder_.add_if_cond(if_id, guard);
      auto arm = builder_.add_if_stmts(if_id);
      builder_.push_stmts(arm);
      // The counter holds THIS iteration's value for the whole iteration, and a
      // taken break freezes it there. So the write goes at the TOP of the
      // guarded arm: last-wins plus the guard make the SURVIVING write the
      // breaking iteration's. (Without a break the terminal write below is the
      // last one anyway, so no per-iteration store is emitted in that case.)
      emit_counter_writeback();
      lower_statement(stmt.body);
      builder_.pop_stmts();
    }

    bool stepped = true;
    for (const auto* se : stmt.steps) {
      if (!try_eval(*se)) {
        emit_error(se->sourceRange, "non-const-loop-step", "comptime", "for-loop step must be compile-time evaluable");
        stepped = false;
        break;
      }
    }
    if (!stepped) {
      unroll_ok = false;
      break;
    }
    if (stmt.stopExpr == nullptr && stmt.steps.empty()) {
      emit_error(stmt.sourceRange, "loop-no-progress", "comptime", "for loop without condition or step cannot unroll");
      unroll_ok = false;
      break;
    }
  }

  // Loop exit: the counter holds the first value that FAILED the stop test.
  // With a runtime break it only reaches that value when no break fired, so the
  // terminal write is guarded the same way the iterations are.
  if (unroll_ok && !outlives.empty()) {
    if (brk_flag.empty()) {
      emit_counter_writeback();
    } else {
      auto guard = builder_.create_eq_stmts(brk_flag, "0");
      auto if_id = builder_.create_if_stmt(false);
      builder_.add_if_cond(if_id, guard);
      auto arm = builder_.add_if_stmts(if_id);
      builder_.push_stmts(arm);
      emit_counter_writeback();
      builder_.pop_stmts();
    }
  }

  loop_rolled_.pop_back();
  break_flags_.pop_back();

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

  // while/do-while/repeat unroll here WITHOUT a `broken` flag, so a `break` in
  // the body has nothing of its own to raise. Mark the stacks accordingly: the
  // Break arm then refuses instead of raising an OUTER for-loop's flag (which
  // would terminate the wrong loop), and it never mistakes an outer ROLLED
  // loop's marker semantics for its own.
  Unflagged_loop_scope loop_scope(this);

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
        emit_unsupported(cond->sourceRange,
                         "non-const-while",
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
  // Same as lower_while_loop: unrolled here, owns no `broken` flag (see
  // Unflagged_loop_scope).
  Unflagged_loop_scope loop_scope(this);
  recurse(0);
}
