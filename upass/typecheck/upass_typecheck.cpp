//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_typecheck.hpp"

#include <format>

#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "lnast.hpp"

// Registered once here (static-init at link time; alwayslink keeps it alive).
// depends_on {"attributes"} so the resolver runs attributes first.
static upass::uPass_plugin plugin_typecheck("typecheck", upass::uPass_wrapper<uPass_typecheck>::get_upass, {"attributes"});

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
  // optable.md §Inference: literal text → kind. (The runner's operand
  // resolution applies the same rules when minting const-operand bundles;
  // this stays for the nullary control-flow checks that read raw children.)
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
  auto v = Dlop::from_pyrope(t);
  if (v && v->is_integer()) {
    return Kind::integer;  // includes multi-bit unknown literals like `0sb??`
  }
  return Kind::unknown;
}

uPass_typecheck::Kind uPass_typecheck::kind_of_bundle(const Bundle& b) {
  // Shape FIRST: a multi-entry / named-field bundle is a tuple regardless of
  // what field 0's entry-kind says (constprop copies field entries wholesale,
  // so `(a<b, a>b)`'s slot 0 carries the lt tmp's boolean kind — that
  // describes the FIELD, not the bundle). Then the bundle-level value kind
  // (producer-stamped; covers 0/1-entry tuples the shape can't express).
  // Last, the "0" Entry's kind (declared by the runner's bake or preserved
  // through value writes).
  if (b.has_named_top() || b.unnamed_top_count() > 1) {
    return Kind::tuple;
  }
  if (b.get_value_kind() != Kind::unknown) {
    return b.get_value_kind();
  }
  return b.get_entry("0").kind;
}

uPass_typecheck::Kind uPass_typecheck::kind_of(std::string_view name) const {
  if (name.empty() || runner_st == nullptr) {
    return Kind::unknown;
  }
  const auto b = runner_st->get_bundle(name);
  return b ? kind_of_bundle(*b) : Kind::unknown;
}

void uPass_typecheck::set_dst_kind(Bundle& dst, Kind k) {
  // Stamp the BUNDLE-level value kind: no entry interplay, so constprop's
  // wholesale field-entry copies can never confuse a field's kind with the
  // bundle's, and the stamp survives its value writes (which only touch
  // entries).
  if (k == Kind::unknown) {
    return;
  }
  dst.set_value_kind(k);
}

uPass_typecheck::Kind uPass_typecheck::kind_of_operand_at_cursor() {
  if (Lnast_ntype::is_const(get_raw_ntype())) {
    return seed_kind_from_const(current_text());
  }
  // ref (or anything unexpected) → table lookup; absent ⇒ unknown (wildcard).
  return kind_of(current_text());
}

void uPass_typecheck::emit_type_error(std::string_view code, const std::string& msg, std::string_view hint,
                                      livehd::diag::Span span) {
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = std::string{code},
      .category = "type",
      .pass     = "upass.typecheck",
      .message  = msg,
      .span     = std::move(span),
      .hint     = std::string{hint},
  });
}

void uPass_typecheck::require_all(Kind required, Kind result, std::string_view sym, std::string_view code, Bundle& dst,
                                  upass::Src_span src, bool allow_nil) {
  bool has_nil = false;
  bool bad     = false;
  for (const auto& o : src) {
    const Kind k = kind_of_operand(o);
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
  set_dst_kind(dst, result);
}

void uPass_typecheck::require_shift(std::string_view sym, Bundle& dst, upass::Src_span src) {
  // `a << b`: `a` must be integer; the amount `b` is integer OR a tuple of bit
  // positions (the documented one-hot construction `1 << (1,4,3)`,
  // 04-variables.md). constprop folds the tuple form; a non-integer leaf simply
  // leaves it unresolved. Result is integer. (Only `<<` — not `>>` — per spec.)
  bool bad     = false;
  bool has_nil = false;
  for (std::size_t i = 0; i < src.size(); ++i) {
    const Kind k = kind_of_operand(src[i]);
    if (k == Kind::nil) {
      has_nil = true;
    } else if (k == Kind::unknown || k == Kind::integer) {
      // ok
    } else if (i == 1 && k == Kind::tuple) {
      // shift-by-tuple one-hot amount — accepted
    } else {
      bad = true;
    }
  }
  if (has_nil) {
    emit_type_error("nil-operand",
                    std::format("`nil` is invalid in operator `{}` (only copy, `==nil`/`!=nil`, and `.[valid]` are allowed)",
                                sym));
  } else if (bad) {
    emit_type_error("type-mismatch-arith",
                    std::format("operator `{}` requires integer operands", sym),
                    "no implicit conversion — cast explicitly (e.g. `int(b)`, `int(true)==-1`)");
  }
  set_dst_kind(dst, Kind::integer);
}

void uPass_typecheck::require_same(Kind result, std::string_view sym, std::string_view code, Bundle& dst, upass::Src_span src) {
  bool any_nil = false;
  for (const auto& o : src) {
    if (kind_of_operand(o) == Kind::nil) {
      any_nil = true;  // `x == nil` / `x != nil`: validity probe — homogeneity skipped
    }
  }
  if (!any_nil) {
    // Operands must share an eq-class: int/bool/string distinct (no implicit
    // conversion); range and tuple inter-compare (flat tuple). unknown unifies.
    int  seen = -1;
    bool bad  = false;
    for (const auto& o : src) {
      int c = eq_class(kind_of_operand(o));
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
  set_dst_kind(dst, result);
}

// ── store: establish the dst kind, or reject a kind change ──────────────────
upass::Vote uPass_typecheck::process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // Scalar store( ref(dst), value ): src = {value}. Establish the dst kind on
  // its first known write; thereafter a write of a DIFFERENT kind is a compile
  // error — a variable's type cannot change (`mut d=2; d=d++60` is illegal;
  // use a new var). `= nil` is a legal copy and neither establishes nor
  // changes the kind. Field-path stores — selector children (src.size() > 1)
  // or a dotted dst ref (`store(CFG.gain, v)`) — pass through: `dst` is the
  // whole destination bundle there, and per-field checks are follow-up work.
  if (dst_name.empty() || src.size() != 1 || dst_name.find('.') != std::string_view::npos) {
    return Vote::keep;
  }
  const Kind rhs = kind_of_operand(src.front());
  if (rhs == Kind::nil) {
    return Vote::keep;  // `= nil` neither establishes nor changes the kind
  }
  const Kind cur = kind_of_bundle(dst);
  // "an unset/nil scalar destination does not infer a new tuple shape
  // from a tuple RHS": a never-typed dst whose current VALUE is the nil it
  // was initialized with cannot become an aggregate. The runner's inliner
  // marks its own synthesized nil seeds (Symbol_table::nil_seeded) — those
  // prologues legally bind a tuple over the seed.
  if (cur == Kind::unknown && rhs == Kind::tuple && runner_st != nullptr) {
    const auto& t0 = dst.get_entry("0").trivial;
    if (!t0.is_invalid() && t0.is_nil() && !runner_st->nil_seeded.contains(std::string(dst_name))) {
      emit_type_error("nil-shape-infer",
                      std::format("`{}` was initialized to nil as a scalar; a tuple value cannot re-shape it", dst_name),
                      "declare the tuple shape up front (e.g. `mut x = (a=nil, b=nil)`), or use a new variable");
      return Vote::keep;
    }
  }
  // "typecheck rejects RHS fields not present in the destination
  // shape": a DECLARED shape (named type on the binding) is closed — a tuple
  // RHS may only re-bind existing fields (subset is fine; untouched fields
  // keep their values). INFERRED shapes stay open (the corpus relies on
  // wholesale re-shape and `++=` extension), and a dst with no named tops
  // yet (init before the named-type skeleton materializes) is still
  // inferring — skip both.
  if (rhs == Kind::tuple && dst.has_named_top() && !dst.get_type_name().empty() && init_construction_depth_ == 0) {
    if (const auto& rb = src.front().bundle; rb) {
      for (const auto& tl : rb->top_levels()) {
        if (!tl.name.empty() && !dst.has_top_named(tl.name)) {
          emit_type_error(
              "unknown-field-store",
              std::format("cannot assign field `{}` to `{}`: it is not part of the destination's shape", tl.name, dst_name),
              "a tuple assignment only re-binds existing fields; declare the field at the destination's initializer");
          return Vote::keep;
        }
      }
    }
  }
  if (cur == Kind::unknown) {
    if (rhs != Kind::unknown) {
      set_dst_kind(dst, rhs);  // first establishment of an inferred/untyped var
    }
    return Vote::keep;
  }
  // Established kind: a known rhs of a DIFFERENT kind is a type change (exact —
  // even int→tuple, unlike the coarse `==` class). A var's type cannot change.
  if (rhs != Kind::unknown && rhs != cur) {
    emit_type_error("assign-type-mismatch",
                    std::format("cannot assign {} value to `{}` (it is {}); a variable's type cannot change",
                                kind_name(rhs),
                                dst_name,
                                kind_name(cur)),
                    "use a new variable, or cast explicitly (e.g. `int(x)`)");
  }
  return Vote::keep;
}

// ── control flow: if/elif/when/unless conditions must be boolean ────────────
void uPass_typecheck::process_if() {
  // The runner dispatches this with the cursor ON the `if` node, before its own
  // dead-branch work, so we see the original condition.
  // Shape: (cond, stmts, [cond, stmts]…, [stmts]) scoped, or (cond, stmt…) flat.
  // Every NON-stmts child is a condition operand (a ref or const); a stmts child
  // is a branch body. Restore the cursor to the if-node before returning.
  const auto if_nid = lm->get_current_nid();  // if node carries the source loc
  if (!move_to_child()) {
    return;
  }
  do {
    if (!Lnast_ntype::is_stmts(get_raw_ntype())) {
      Kind k = kind_of_operand_at_cursor();
      if (k == Kind::nil) {
        emit_type_error("nil-operand",
                        "`nil` is invalid as a condition (only copy, `==nil`/`!=nil`, `.[valid]`)",
                        "",
                        span_from_nid(if_nid));
      } else if (k != Kind::unknown && k != Kind::boolean) {
        emit_type_error("cond-not-bool",
                        std::format("condition must be boolean, got {}", kind_name(k)),
                        "compare explicitly, e.g. `if x != 0`",
                        span_from_nid(if_nid));
      }
    }
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_typecheck::process_while() {
  // `while(cond, stmts)` — child0 is the condition (a ref/const); it must be
  // boolean. Restore the cursor to the while node before returning.
  const auto while_nid = lm->get_current_nid();  // while node carries the source loc
  if (!move_to_child()) {
    return;
  }
  if (!Lnast_ntype::is_stmts(get_raw_ntype())) {
    Kind k = kind_of_operand_at_cursor();
    if (k == Kind::nil) {
      emit_type_error("nil-operand",
                      "`nil` is invalid as a condition (only copy, `==nil`/`!=nil`, `.[valid]`)",
                      "",
                      span_from_nid(while_nid));
    } else if (k != Kind::unknown && k != Kind::boolean) {
      emit_type_error("cond-not-bool",
                      std::format("while condition must be boolean, got {}", kind_name(k)),
                      "compare explicitly, e.g. `while x != 0`",
                      span_from_nid(while_nid));
    }
  }
  move_to_parent();
}

// ── builtin cassert: type-check the optional message argument ───────────────
void uPass_typecheck::process_cassert() {
  // cassert(<cond>[, <msg>]) — the builtin signature is
  //   comb cassert(cond:bool=nil, msg:string="")
  // Unnamed builtin arguments bind by type, so the 2nd argument is the
  // diagnostic MESSAGE and must be a string.
  //
  // The condition (child0) is intentionally NOT kind-checked here: it is usually
  // an `in`/`does`/comparison fold temp whose kind this pass does not stamp, so
  // it reads as `unknown` and a bool check would only ever false-negative.
  // Folding/verification of the condition is constprop + verifier's job.
  const auto nid = lm->get_current_nid();  // cassert nodes carry a source loc
  if (!move_to_child()) {
    return;  // malformed cassert (no operands) — nothing to check
  }
  if (move_to_sibling()) {  // child1 = optional message
    Kind mk = kind_of_operand_at_cursor();
    if (mk != Kind::unknown && mk != Kind::string) {
      emit_type_error("cassert-msg-not-string",
                      std::format("cassert message must be a string, got {} (expected string)", kind_name(mk)),
                      "cassert's second argument is a diagnostic message string; remove it or quote it",
                      span_from_nid(nid));
    }
  }
  move_to_parent();  // restore cursor to the cassert node
}

// ── range (verbatim-dispatched): endpoints are integers ─────────────────────
void uPass_typecheck::process_range() {
  // range( ref(dst), lo, hi ) — endpoints must be integers; a nil endpoint is
  // the open-range sentinel (`0..`). The dst kind (range) is stamped through
  // the table when the dst is already bound; the producer that binds it later
  // re-derives tuple/range shape itself, so a miss here is benign.
  if (!move_to_child()) {
    return;
  }
  const std::string dst_name{current_text()};
  bool              has_nil = false;
  bool              bad     = false;
  while (move_to_sibling()) {
    const Kind k = kind_of_operand_at_cursor();
    if (k == Kind::nil) {
      // allowed: open-end sentinel
    } else if (k != Kind::unknown && k != Kind::integer) {
      bad = true;
    }
  }
  move_to_parent();
  (void)has_nil;
  if (bad) {
    emit_type_error("type-mismatch-range",
                    "operator `..=` requires integer operands",
                    "no implicit conversion — cast explicitly (e.g. `int(b)`, `int(true)==-1`)");
  }
  if (!dst_name.empty() && runner_st != nullptr) {
    if (auto b = runner_st->get_bundle_for_write(dst_name); b) {
      set_dst_kind(*b, Kind::range);
    }
  }
}

// Build a located Span from an LNAST nid: its SourceId resolved through the
// owning Lnast's locator. Mirrors uPass_verifier::span_from_nid.
livehd::diag::Span uPass_typecheck::span_from_nid(const Lnast_nid& nid) const {
  if (const auto& ln = lm->get_lnast()) {
    return ln->span_of(nid);
  }
  return {};
}

// ── arithmetic / bitwise / shift: int operands → int (NO bool) ──────────────
// clang-format off
upass::Vote uPass_typecheck::process_plus(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "+", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_minus(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "-", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_mult(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "*", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_div(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "/", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_mod(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "%", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_and(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "&", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_or(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "|", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_xor(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "^", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_not(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "~", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_shl(std::string_view, Bundle& dst, upass::Src_span src) { require_shift("<<", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_sra(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, ">>", "type-mismatch-arith", dst, src); return Vote::keep; }

// ── logical keywords: bool operands → bool (NO int — use `&`/`|`) ────────────
upass::Vote uPass_typecheck::process_log_and(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::boolean, Kind::boolean, "and", "type-mismatch-logical", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_log_or(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::boolean, Kind::boolean, "or", "type-mismatch-logical", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_log_not(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::boolean, Kind::boolean, "not", "type-mismatch-logical", dst, src); return Vote::keep; }

// ── reductions (→ bool) / popcount (→ int): int operand ─────────────────────
upass::Vote uPass_typecheck::process_red_or(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, "|", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_red_and(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, "&", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_red_xor(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, "^", "type-mismatch-arith", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_popcount(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::integer, "#+", "type-mismatch-arith", dst, src); return Vote::keep; }

// ── comparison: eq/ne same-class → bool; ordering int → bool ────────────────
upass::Vote uPass_typecheck::process_eq(std::string_view, Bundle& dst, upass::Src_span src) { require_same(Kind::boolean, "==", "type-mismatch-eq", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_ne(std::string_view, Bundle& dst, upass::Src_span src) { require_same(Kind::boolean, "!=", "type-mismatch-eq", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_lt(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, "<", "type-mismatch-compare", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_le(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, "<=", "type-mismatch-compare", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_gt(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, ">", "type-mismatch-compare", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_ge(std::string_view, Bundle& dst, upass::Src_span src) { require_all(Kind::integer, Kind::boolean, ">=", "type-mismatch-compare", dst, src); return Vote::keep; }

// ── bit manipulation / type-id: result kind only (operands not kind-checked) ─
upass::Vote uPass_typecheck::process_set_mask(std::string_view, Bundle& dst, upass::Src_span) { set_dst_kind(dst, Kind::integer); return Vote::keep; }
upass::Vote uPass_typecheck::process_sext(std::string_view, Bundle& dst, upass::Src_span) { set_dst_kind(dst, Kind::integer); return Vote::keep; }
// clang-format on

// ── aggregates: passthrough kinds, no homogeneity check ─────────────────────
upass::Vote uPass_typecheck::process_tuple_add(std::string_view, Bundle& dst, upass::Src_span src) {
  // Every tuple_add in the tree is a REAL tuple literal: `(expr)` groupings
  // were unwrapped at parse (db87b5908 — `(x)` no-comma unwraps, `(x,)` is
  // kept), so even a single-operand node is a 1-tuple.
  if (!src.empty()) {
    set_dst_kind(dst, Kind::tuple);
  }
  return Vote::keep;
}

upass::Vote uPass_typecheck::process_tuple_concat(std::string_view, Bundle& dst, upass::Src_span src) {
  // `++` → tuple, or string when every known operand is a string, or range when
  // every known operand is a range (optable.md). Not homogeneity-checked.
  bool any_known  = false;
  bool all_string = true;
  bool all_range  = true;
  for (const auto& o : src) {
    const Kind k = kind_of_operand(o);
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
  if (any_known && all_string) {
    set_dst_kind(dst, Kind::string);
  } else if (any_known && all_range) {
    set_dst_kind(dst, Kind::range);
  } else if (any_known) {
    // Mixed/known operands: a real tuple. The bundle-level stamp matters for
    // the scalar-SHAPED 0/1-entry results the shape can't express
    // (`mut d = (); d = d ++ (i,)` accumulator). ALL-unknown operands stamp
    // nothing — `string(a) ++ string(b)` with unfolded cast tmps must not be
    // pinned tuple, or the chained `== "lit"` comparison spuriously errors.
    set_dst_kind(dst, Kind::tuple);
  }
  return Vote::keep;
}
