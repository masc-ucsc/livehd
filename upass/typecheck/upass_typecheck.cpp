//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_typecheck.hpp"

#include <format>

#include "const.hpp"
#include "diag.hpp"
#include "lnast.hpp"

// Registered once here (static-init at link time; alwayslink keeps it alive).
// depends_on {"attributes"} so the resolver runs attributes first (typecheck
// re-infers kinds itself, but this pins the intended slot in the order).
static upass::uPass_plugin plugin_typecheck("typecheck", upass::uPass_wrapper<uPass_typecheck>::get_upass, {"attributes"});

// NOTE: the new `process_arith`/shaped Vote hooks (upass_core.hpp §Step-E) are
// NOT yet wired into the runner dispatch — it still drives the legacy per-op
// `process_<op>()` virtuals (upass_runner.cpp A_OP/C_OP). So this pass overrides
// those. If a future runner refactor routes ops through process_arith, migrate.

const char* uPass_typecheck::kind_name(Kind k) {
  switch (k) {
    case Kind::integer: return "integer";
    case Kind::boolean: return "boolean";
    case Kind::string : return "string";
    case Kind::range  : return "range";
    case Kind::tuple  : return "tuple";
    case Kind::nil    : return "nil";
    default           : return "unknown";
  }
}

int uPass_typecheck::eq_class(Kind k) {
  // Equality classes. `bool` and `string` are distinct (no implicit
  // conversion — `bool == int` and `string == int` are errors). int/range/tuple
  // share a class: a scalar is a 1-element flat tuple and a range compares to
  // its flat-tuple expansion, so `1 == (1,)` and `2..=4 == (2,3,4)` are legal.
  // unknown/nil: no class (check skipped). (Assignment uses exact-kind equality,
  // not this coarse class — a var's type cannot change, even int↔tuple.)
  switch (k) {
    case Kind::integer:
    case Kind::range:
    case Kind::tuple  : return 0;
    case Kind::boolean: return 1;
    case Kind::string : return 2;
    default           : return -1;
  }
}

uPass_typecheck::Kind uPass_typecheck::seed_kind_from_const(std::string_view t) {
  // optable.md §Inference: literal text → kind.
  if (t == "nil") {
    return Kind::nil;
  }
  if (t == "true" || t == "false") {
    return Kind::boolean;
  }
  // A single unknown bit `0sb?`/`0ub?` is TYPELESS (could be bool or int) → the
  // unify-with-anything wildcard. `0sb??` (≥2 bits) is an int (handled below).
  if (t == "0sb?" || t == "0ub?") {
    return Kind::unknown;
  }
  if (!t.empty() && t.front() == '"') {
    return Kind::string;  // double-quoted string const. (Single-quoted `'a'` is
                          // a CHARACTER literal = integer — handled below.)
  }
  auto v = Const::from_pyrope(t);
  if (v && v->is_integer()) {
    return Kind::integer;  // includes multi-bit unknown literals like `0sb??`
  }
  return Kind::unknown;
}

uPass_typecheck::Kind uPass_typecheck::kind_of(std::string_view name) const {
  auto it = kind_map.find(std::string{name});
  return it == kind_map.end() ? Kind::unknown : it->second;
}

void uPass_typecheck::set_kind(std::string_view name, Kind k) {
  if (name.empty()) {
    return;
  }
  kind_map[std::string{name}] = k;
}

uPass_typecheck::Kind uPass_typecheck::kind_of_operand_at_cursor() {
  if (Lnast_ntype::is_const(get_raw_ntype())) {
    return seed_kind_from_const(current_text());
  }
  // ref (or anything unexpected) → map lookup; absent ⇒ unknown (wildcard).
  return kind_of(normalize_name(current_text()));
}

uPass_typecheck::Kind uPass_typecheck::kind_of_type_at_cursor() {
  auto t = get_raw_ntype();
  if (Lnast_ntype::is_prim_type_int(t)) {
    return Kind::integer;  // NO range read — bits/sign/max/min are bitwidth's
  }
  if (Lnast_ntype::is_prim_type_bool(t)) {
    return Kind::boolean;
  }
  if (Lnast_ntype::is_prim_type_string(t)) {
    return Kind::string;
  }
  return Kind::unknown;  // named/comp/none types don't constrain the kind here
}

std::vector<uPass_typecheck::Kind> uPass_typecheck::collect_operands(std::string& dst_name) {
  std::vector<Kind> ks;
  if (!move_to_child()) {
    return ks;  // empty op (shouldn't happen); cursor unmoved
  }
  dst_name = normalize_name(current_text());  // child0 = dst ref
  while (move_to_sibling()) {
    ks.push_back(kind_of_operand_at_cursor());
  }
  move_to_parent();  // MANDATORY: restore cursor to the op node
  return ks;
}

void uPass_typecheck::emit_type_error(std::string_view code, const std::string& msg, std::string_view hint) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = std::string{code},
      .category = "type",
      .pass     = "upass.typecheck",
      .message  = msg,
      .hint     = std::string{hint},
  });
}

void uPass_typecheck::require_all(Kind required, Kind result, std::string_view sym, std::string_view code, bool allow_nil) {
  std::string dst;
  auto        ks      = collect_operands(dst);
  bool        has_nil = false;
  bool        bad     = false;
  for (auto k : ks) {
    if (k == Kind::nil) {
      if (!allow_nil) {
        has_nil = true;  // poison (open-range `..` nil sentinel is allowed)
      }
    } else if (k == Kind::unknown || k == required) {
      // wildcard or exact match — ok (NO bool↔int interop)
    } else {
      bad = true;
    }
  }
  if (has_nil) {
    emit_type_error("nil-operand",
                    std::format("`nil` is invalid in operator `{}` (only copy, `==nil`/`!=nil`, and `.[valid]` "
                                "are allowed)",
                                sym));
  } else if (bad) {
    std::string_view hint = (required == Kind::boolean)
                                ? "logical ops need boolean operands; use `&`/`|`/`^` for bitwise integers"
                                : "no implicit conversion — cast explicitly (e.g. `int(b)`, `int(true)==-1`)";
    emit_type_error(code, std::format("operator `{}` requires {} operands", sym, kind_name(required)), hint);
  }
  set_kind(dst, result);
}

void uPass_typecheck::require_same(Kind result, std::string_view sym, std::string_view code) {
  std::string dst;
  auto        ks      = collect_operands(dst);
  bool        any_nil = false;
  for (auto k : ks) {
    if (k == Kind::nil) {
      any_nil = true;  // `x == nil` / `x != nil`: validity probe — homogeneity skipped
    }
  }
  if (!any_nil) {
    // Operands must share an eq-class: int/bool/string distinct (no implicit
    // conversion); range and tuple inter-compare (flat tuple). unknown unifies.
    int  seen = -1;
    bool bad  = false;
    for (auto k : ks) {
      int c = eq_class(k);
      if (c >= 0) {
        if (seen < 0) {
          seen = c;
        } else if (seen != c) {
          bad = true;
        }
      }
    }
    if (bad) {
      emit_type_error(code,
                      std::format("`{}` requires both operands to be the same type", sym),
                      "no implicit conversion — cast explicitly (e.g. `int(b)`, `x == false`)");
    }
  }
  set_kind(dst, result);
}

void uPass_typecheck::stamp_result(Kind result) {
  std::string dst;
  collect_operands(dst);
  set_kind(dst, result);
}

// ── declarations: record a variable's declared kind ────────────────────────
void uPass_typecheck::process_declare() {
  // declare( ref(var), TYPE, const(mode) [, value] ) — value is a separate store.
  if (!move_to_child()) {
    return;
  }
  auto var = normalize_name(current_text());
  Kind k   = Kind::unknown;
  if (move_to_sibling()) {  // TYPE
    k = kind_of_type_at_cursor();
  }
  move_to_parent();
  if (k != Kind::unknown) {
    set_kind(var, k);  // unknown (named/none type) ⇒ leave for value inference
  }
}

void uPass_typecheck::process_type_spec() {
  // type_spec( ref(target), TYPE… ) — same kind-recording as a declare.
  if (!move_to_child()) {
    return;
  }
  auto var = normalize_name(current_text());
  Kind k   = Kind::unknown;
  if (move_to_sibling()) {
    k = kind_of_type_at_cursor();
  }
  move_to_parent();
  if (k != Kind::unknown) {
    set_kind(var, k);
  }
}

void uPass_typecheck::process_assign() {
  // 2-child store( ref(dst), value ): establish the dst kind on its first known
  // write; thereafter a write of a DIFFERENT kind-class is a compile error — a
  // variable's type cannot change (`mut d=2; d=d++60` is illegal; use a new
  // var). `= nil` is a legal copy and neither establishes nor changes the kind.
  if (!move_to_child()) {
    return;
  }
  auto dst = normalize_name(current_text());
  Kind rhs = Kind::unknown;
  if (move_to_sibling()) {
    rhs = kind_of_operand_at_cursor();
  }
  move_to_parent();
  if (dst.empty() || rhs == Kind::nil) {
    return;
  }
  Kind cur = kind_of(dst);
  if (cur == Kind::unknown) {
    if (rhs != Kind::unknown) {
      set_kind(dst, rhs);  // first establishment of an inferred/untyped var
    }
    return;
  }
  // Established kind: a known rhs of a DIFFERENT kind is a type change (exact —
  // even int→tuple, unlike the coarse `==` class). A var's type cannot change.
  if (rhs != Kind::unknown && rhs != cur) {
    emit_type_error("assign-type-mismatch",
                    std::format("cannot assign {} value to `{}` (it is {}); a variable's type cannot change",
                                kind_name(rhs),
                                dst,
                                kind_name(cur)),
                    "use a new variable, or cast explicitly (e.g. `int(x)`)");
  }
}

// ── control flow: if/elif/when/unless conditions must be boolean ────────────
void uPass_typecheck::process_if() {
  // The runner dispatches this with the cursor ON the `if` node, before its own
  // dead-branch work (upass_runner.cpp:1648), so we see the original condition.
  // Shape: (cond, stmts, [cond, stmts]…, [stmts]) scoped, or (cond, stmt…) flat.
  // Every NON-stmts child is a condition operand (a ref or const); a stmts child
  // is a branch body. Restore the cursor to the if-node before returning.
  if (!move_to_child()) {
    return;
  }
  do {
    if (!Lnast_ntype::is_stmts(get_raw_ntype())) {
      Kind k = kind_of_operand_at_cursor();
      if (k == Kind::nil) {
        emit_type_error("nil-operand", "`nil` is invalid as a condition (only copy, `==nil`/`!=nil`, `.[valid]`)");
      } else if (k != Kind::unknown && k != Kind::boolean) {
        emit_type_error("cond-not-bool",
                        std::format("condition must be boolean, got {}", kind_name(k)),
                        "compare explicitly, e.g. `if x != 0`");
      }
    }
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_typecheck::process_while() {
  // `while(cond, stmts)` — child0 is the condition (a ref/const); it must be
  // boolean. Restore the cursor to the while node before returning.
  if (!move_to_child()) {
    return;
  }
  if (!Lnast_ntype::is_stmts(get_raw_ntype())) {
    Kind k = kind_of_operand_at_cursor();
    if (k == Kind::nil) {
      emit_type_error("nil-operand", "`nil` is invalid as a condition (only copy, `==nil`/`!=nil`, `.[valid]`)");
    } else if (k != Kind::unknown && k != Kind::boolean) {
      emit_type_error("cond-not-bool",
                      std::format("while condition must be boolean, got {}", kind_name(k)),
                      "compare explicitly, e.g. `while x != 0`");
    }
  }
  move_to_parent();
}

// ── arithmetic / bitwise / shift: int operands → int (NO bool) ──────────────
void uPass_typecheck::process_plus() { require_all(Kind::integer, Kind::integer, "+", "type-mismatch-arith"); }
void uPass_typecheck::process_minus() { require_all(Kind::integer, Kind::integer, "-", "type-mismatch-arith"); }
void uPass_typecheck::process_mult() { require_all(Kind::integer, Kind::integer, "*", "type-mismatch-arith"); }
void uPass_typecheck::process_div() { require_all(Kind::integer, Kind::integer, "/", "type-mismatch-arith"); }
void uPass_typecheck::process_mod() { require_all(Kind::integer, Kind::integer, "%", "type-mismatch-arith"); }
void uPass_typecheck::process_bit_and() { require_all(Kind::integer, Kind::integer, "&", "type-mismatch-arith"); }
void uPass_typecheck::process_bit_or() { require_all(Kind::integer, Kind::integer, "|", "type-mismatch-arith"); }
void uPass_typecheck::process_bit_xor() { require_all(Kind::integer, Kind::integer, "^", "type-mismatch-arith"); }
void uPass_typecheck::process_bit_not() { require_all(Kind::integer, Kind::integer, "~", "type-mismatch-arith"); }
void uPass_typecheck::process_shl() { require_all(Kind::integer, Kind::integer, "<<", "type-mismatch-arith"); }
void uPass_typecheck::process_sra() { require_all(Kind::integer, Kind::integer, ">>", "type-mismatch-arith"); }

// ── logical keywords: bool operands → bool (NO int — use `&`/`|`) ────────────
void uPass_typecheck::process_log_and() { require_all(Kind::boolean, Kind::boolean, "and", "type-mismatch-logical"); }
void uPass_typecheck::process_log_or() { require_all(Kind::boolean, Kind::boolean, "or", "type-mismatch-logical"); }
void uPass_typecheck::process_log_not() { require_all(Kind::boolean, Kind::boolean, "not", "type-mismatch-logical"); }

// ── reductions (→ bool) / popcount (→ int): int operand ─────────────────────
void uPass_typecheck::process_red_or() { require_all(Kind::integer, Kind::boolean, "|", "type-mismatch-arith"); }
void uPass_typecheck::process_red_and() { require_all(Kind::integer, Kind::boolean, "&", "type-mismatch-arith"); }
void uPass_typecheck::process_red_xor() { require_all(Kind::integer, Kind::boolean, "^", "type-mismatch-arith"); }
void uPass_typecheck::process_popcount() { require_all(Kind::integer, Kind::integer, "#+", "type-mismatch-arith"); }

// ── comparison: eq/ne same-class → bool; ordering int → bool ────────────────
void uPass_typecheck::process_eq() { require_same(Kind::boolean, "==", "type-mismatch-eq"); }
void uPass_typecheck::process_ne() { require_same(Kind::boolean, "!=", "type-mismatch-eq"); }
void uPass_typecheck::process_lt() { require_all(Kind::integer, Kind::boolean, "<", "type-mismatch-compare"); }
void uPass_typecheck::process_le() { require_all(Kind::integer, Kind::boolean, "<=", "type-mismatch-compare"); }
void uPass_typecheck::process_gt() { require_all(Kind::integer, Kind::boolean, ">", "type-mismatch-compare"); }
void uPass_typecheck::process_ge() { require_all(Kind::integer, Kind::boolean, ">=", "type-mismatch-compare"); }

// ── bit manipulation / type-id: result kind only (operands not kind-checked) ─
void uPass_typecheck::process_get_mask() { stamp_result(Kind::unknown); }  // single-bit→bool vs range→int: ambiguous
void uPass_typecheck::process_set_mask() { stamp_result(Kind::integer); }
void uPass_typecheck::process_sext() { stamp_result(Kind::integer); }
void uPass_typecheck::process_is() { stamp_result(Kind::boolean); }

// ── aggregates: passthrough kinds, no homogeneity check ─────────────────────
void uPass_typecheck::process_range() {
  // endpoints are integers; a nil endpoint is the open-range sentinel (`0..`).
  require_all(Kind::integer, Kind::range, "..=", "type-mismatch-range", /*allow_nil=*/true);
}
void uPass_typecheck::process_tuple_add() {
  // A single-operand tuple_add is a parenthesized grouping `(expr)` — propagate
  // the inner kind so `x and (y or z)` keeps its kind. Multiple operands (or a
  // structured/keyed field payload) is a real tuple.
  std::string dst;
  auto        ks  = collect_operands(dst);
  Kind        res = (ks.size() == 1) ? ks[0] : Kind::tuple;
  set_kind(dst, res);
}
void uPass_typecheck::process_tuple_get() { stamp_result(Kind::unknown); }

void uPass_typecheck::process_tuple_concat() {
  // `++` → tuple, or string when every known operand is a string, or range when
  // every known operand is a range (optable.md). Not homogeneity-checked.
  std::string dst;
  auto        ks         = collect_operands(dst);
  bool        any_known  = false;
  bool        all_string = true;
  bool        all_range  = true;
  for (auto k : ks) {
    if (k != Kind::unknown) {
      any_known = true;
      if (k != Kind::string) {
        all_string = false;
      }
      if (k != Kind::range) {
        all_range = false;
      }
    }
  }
  Kind res = Kind::tuple;
  if (any_known && all_string) {
    res = Kind::string;
  } else if (any_known && all_range) {
    res = Kind::range;
  }
  set_kind(dst, res);
}
