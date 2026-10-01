//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_typecheck.hpp"

#include <format>

#include "decl_facts.hpp"
#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "lnast.hpp"
#include "op_kind_rules.hpp"
#include "range_bits.hpp"

// Registered once here (static-init at link time; alwayslink keeps it alive).
// depends_on {"attributes"} so the resolver runs attributes first.
static upass::uPass_plugin plugin_typecheck("typecheck", upass::uPass_wrapper<uPass_typecheck>::get_upass, {"attributes"});

namespace {
// `x___ssa_2` is a VERSION of the user variable `x` (upass.ssa versions every
// re-assigned `mut`). A read-modify-write reads one version and writes the
// next, so its two ends only ever agree on the base name.
std::string_view ssa_base(std::string_view name) {
  if (const auto pos = name.find("___ssa_"); pos != std::string_view::npos) {
    return name.substr(0, pos);
  }
  return name;
}

// The bool<->integer hint (upass::kBoolIntCastHint) after an operator's lead-in.
std::string cast_hint() { return std::format("no implicit conversion — cast explicitly: {}", upass::kBoolIntCastHint); }
}  // namespace

const char* uPass_typecheck::kind_name(Kind k) { return upass::op_kind::kind_name(k).data(); }

int uPass_typecheck::eq_class(Kind k) { return upass::op_kind::eq_class(k); }

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

uPass_typecheck::Kind uPass_typecheck::kind_of_bundle(const Bundle& b) { return upass::decl_facts::bundle_kind(b); }

uPass_typecheck::Kind uPass_typecheck::kind_of(std::string_view name) const {
  if (name.empty() || runner_st == nullptr) {
    return Kind::unknown;
  }
  if (const auto b = runner_st->get_bundle(name); b) {
    if (const Kind k = kind_of_bundle(*b); k != Kind::unknown) {
      return k;
    }
  }
  // A module IO PORT is never a table-backed value: its declared type lives on
  // the Lnast io_meta side-channel, so the bundle above reports `unknown` and
  // `if en { … }` on a `u1` port used to slip through the condition check that
  // the identical `mut en:u1` local trips. decl_facts is the single source of
  // truth for "what was `name` declared as" — ask it before giving up.
  return declared_kind(name);
}

uPass_typecheck::Kind uPass_typecheck::declared_kind(std::string_view name) const {
  if (name.empty() || runner_st == nullptr) {
    return Kind::unknown;
  }
  return upass::decl_facts::operand_kind(*runner_st, lm ? lm->get_lnast().get() : nullptr, name);
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
  // Anchor on the current op/store node when the caller passes no explicit span.
  // current_span() resolves the cursor's def node (or its nearest srcid-bearing
  // ancestor), so the diagnostic gets a file/line even from an operand child.
  // Null only when nothing in the cursor's ancestry carries a location, so this
  // can only add location info, never remove it.
  if (span.is_null()) {
    span = lm->current_span();
  }
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

bool uPass_typecheck::is_clock_port(std::string_view name) const {
  if (name.empty() || lm == nullptr) {
    return false;
  }
  const auto ln = lm->get_lnast();
  if (!ln || ln->is_verilog_origin()) {
    return false;  // Verilog has no Clock type: its clk is stamped by name and may be data
  }
  const auto* pe = ln->io_meta().find(name);
  return pe != nullptr && pe->is_clock();
}

bool uPass_typecheck::debug_only(std::string_view name) {
  if (name.empty() || !Lnast::is_tmp(name) || lm == nullptr) {
    return false;
  }
  const auto ln = lm->get_lnast();
  if (!ln) {
    return false;
  }
  if (debug_uses_of_ != ln.get()) {
    // Every READ of a name, by its consuming node: an occurrence that is not
    // the destination (first child) of a value-producing node.
    debug_uses_of_ = ln.get();
    debug_uses_.clear();
    debug_memo_.clear();
    for (const auto& n : ln->depth_preorder()) {
      const auto k = Lnast_nid(n);
      if (!Lnast_ntype::is_ref(ln->get_type(k))) {
        continue;
      }
      const auto parent = ln->get_parent(k);
      if (parent.is_invalid()) {
        continue;
      }
      const auto pt       = ln->get_type(parent);
      const bool is_first = ln->get_first_child(parent) == k;
      const bool reads_first
          = Lnast_ntype::is_cassert(pt) || Lnast_ntype::is_if_like(pt) || Lnast_ntype::is_while(pt) || Lnast_ntype::is_stmts(pt);
      if (is_first && !reads_first) {
        continue;  // the definition
      }
      debug_uses_[std::string(ln->get_name(k))].push_back(parent);
    }
  }
  if (const auto it = debug_memo_.find(name); it != debug_memo_.end()) {
    return it->second;
  }
  debug_memo_[std::string(name)] = false;  // a cycle is not debug-only
  const auto it = debug_uses_.find(name);
  if (it == debug_uses_.end()) {
    return false;  // unread: nothing proves it debug-only
  }
  bool ok = true;
  for (const auto& use : it->second) {
    const auto t = ln->get_type(use);
    if (Lnast_ntype::is_cassert(t)) {
      continue;
    }
    const auto dst = ln->get_first_child(use);
    if (Lnast_ntype::is_func_call(t)) {
      const auto callee = dst.is_invalid() ? dst : ln->get_sibling_next(dst);
      const auto cname  = callee.is_invalid() ? std::string_view{} : ln->get_name(callee);
      if (cname == "puts" || cname == "print" || cname == "assert" || cname == "cover") {
        continue;
      }
      if ((cname == "String" || cname == "__fmt") && !dst.is_invalid() && debug_only(ln->get_name(dst))) {
        continue;
      }
      ok = false;
      break;
    }
    const bool value_op = Lnast_ntype::is_plus(t) || Lnast_ntype::is_minus(t) || Lnast_ntype::is_mult(t) || Lnast_ntype::is_div(t)
                          || Lnast_ntype::is_mod(t) || Lnast_ntype::is_eq(t) || Lnast_ntype::is_ne(t) || Lnast_ntype::is_lt(t)
                          || Lnast_ntype::is_le(t) || Lnast_ntype::is_gt(t) || Lnast_ntype::is_ge(t) || Lnast_ntype::is_log_and(t)
                          || Lnast_ntype::is_log_or(t) || Lnast_ntype::is_log_not(t) || Lnast_ntype::is_bit_and(t)
                          || Lnast_ntype::is_bit_or(t) || Lnast_ntype::is_bit_xor(t) || Lnast_ntype::is_bit_not(t)
                          || Lnast_ntype::is_get_mask(t) || Lnast_ntype::is_store(t);
    if (value_op && !dst.is_invalid() && debug_only(ln->get_name(dst))) {
      continue;
    }
    ok = false;
    break;
  }
  debug_memo_[std::string(name)] = ok;
  return ok;
}

bool uPass_typecheck::reject_clock_data(std::string_view name, std::string_view what, std::string_view dst) {
  if (!is_clock_port(name)) {
    return false;
  }
  if (debug_only(dst)) {
    return false;  // the debug cycle-count view (`assert(clk < 1000)`)
  }
  emit_type_error("clock-as-data",
                  std::format("`{}` is a `Clock`, and a Clock is not data ({})", upass::Lnast_manager::user_name(name), what),
                  "a Clock only drives register clock pins (`clock_pin=clk`) or a child's `Clock` input; use an enable "
                  "for clock-dependent logic");
  return true;
}

void uPass_typecheck::require_rule(Lnast_ntype::Lnast_ntype_int op, Bundle& dst, upass::Src_span src, std::string_view dst_name) {
  const auto r = upass::op_kind::rule_of(op);
  if (!r) {
    return;
  }
  for (const auto& o : src) {
    if (reject_clock_data(o.name, std::format("operator `{}`", r->sym), dst_name)) {
      set_dst_kind(dst, r->result);
      return;
    }
    if (is_clock_port(o.name)) {
      // The debug cycle-count view (`assert(clk < 1000)`): a Clock reads as
      // its cycle count there, so the Bool kind rule does not apply.
      set_dst_kind(dst, r->result);
      return;
    }
  }
  if (r->required == Kind::unknown) {
    require_same(r->result, r->sym, r->code, dst, src);
  } else {
    require_all(r->required, r->result, r->sym, r->code, dst, src);
  }
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
    const std::string hint = required == Kind::boolean ? std::string(upass::op_kind::kLogicalOperandHint) : cast_hint();
    emit_type_error(code,
                    std::format("operator `{}` requires {} operands ({})", sym, kind_name(required), name_operands(src)),
                    hint);
  }
  set_dst_kind(dst, result);
}

// How an operand reads in a message: a literal is `<const>`, a compiler temp
// that read a field (`c.rdy`, `t.f`) names that field, a call-result temp
// names the call (`cmp(…).lt`), and an inlined callee's `inl3_x` is its `x`.
std::string_view uPass_typecheck::operand_label(const upass::Operand& o) const {
  if (o.name.empty()) {
    return "<const>";
  }
  if (runner_st != nullptr && Lnast::is_tmp(o.name)) {
    if (const auto it = runner_st->tget_origin.find(o.name); it != runner_st->tget_origin.end()) {
      return upass::Lnast_manager::user_name(it->second);
    }
    if (const auto it = runner_st->call_result_label.find(o.name); it != runner_st->call_result_label.end()) {
      return it->second;
    }
  }
  return upass::Lnast_manager::user_name(o.name);
}

// Format the operands as "name:kind, name:kind" so an op with no source span
// (most lowered arithmetic) can still be localized by the operand names.
std::string uPass_typecheck::name_operands(upass::Src_span src) const {
  std::string ops;
  for (const auto& o : src) {
    if (!ops.empty()) {
      ops += ", ";
    }
    ops += absl::StrCat(operand_label(o), ":", upass::op_kind::kind_annot(kind_of_operand(o)));
  }
  return ops;
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
                    std::format("`nil` is invalid in operator `{}` (only copy, `==nil`/`!=nil`, and `.[valid]` are allowed)", sym));
  } else if (bad) {
    emit_type_error("type-mismatch-arith",
                    std::format("operator `{}` requires integer operands ({})", sym, name_operands(src)),
                    cast_hint());
  }
  set_dst_kind(dst, Kind::integer);
}

void uPass_typecheck::require_concat(Bundle& dst, upass::Src_span src) {
  // `concat(msb, …, lsb)`: each lane is an integer bit window, so the rule is
  // require_all(integer) — EXCEPT that an ORDERED positional tuple/array lane
  // is also legal: `concat(t)` splices its fields (field 0 most significant).
  // A multi-field NAMED bundle is rejected by the runner's concat shape check:
  // names have identity but no order, so the caller must select its fields as
  // separate lanes. All this kind pass does is let tuple-shaped operands reach
  // that structural check. Booleans stay errors (no bool<->int interop).
  //
  // Nothing here looks at a lane's WIDTH: the declared-width rule that sizes
  // each window belongs to upass.bitwidth + upass.tolg, not to a kind check.
  // Operands are INTERLEAVED (value, width) pairs. Only the EVEN ones are
  // lanes; the odd ones are the window widths, which are `nil` until an upass
  // pass binds them -- so walking every operand would report that pending
  // `nil` as a nil-in-concat type error.
  //
  // ONE EXCEPTION to "booleans stay errors": a SINGLE-lane concat is not a
  // packing of several values, it is a reinterpret of ONE -- there is nothing
  // to interop with. uPass_runner lowers the sanctioned bool->int cast
  // `unsigned(b)`/`signed(b)` through a get_mask, and the bit-select handler
  // wraps the value in exactly such a one-lane concat (upass_runner.cpp,
  // "bitsel-pack"). Rejecting a boolean lane there made that cast IMPOSSIBLE to
  // write: the hint below offers a bool->int cast, and the cast lands right
  // back here -- a circular diagnostic. The runner already
  // treats the case as legal ("a bool operand always fits") and emits a 1-bit
  // mask for it. A MULTI-lane packing keeps the strict no-bool<->int rule.
  const bool single_lane = src.size() <= 2;
  bool       bad         = false;
  bool       has_nil     = false;
  for (std::size_t i = 0; i < src.size(); i += 2) {
    const Kind k = kind_of_operand(src[i]);
    if (k == Kind::nil) {
      has_nil = true;
    } else if (k == Kind::unknown || k == Kind::integer || k == Kind::tuple) {
      // wildcard, a scalar lane, or a tuple lane awaiting the shape check — ok
    } else if (k == Kind::boolean && single_lane) {
      // one-lane reinterpret of a boolean: a 1-bit window (see above)
    } else {
      bad = true;
    }
  }
  if (has_nil) {
    emit_type_error("nil-operand", "`nil` is invalid in `concat` (only copy, `==nil`/`!=nil`, and `.[valid]` are allowed)");
  } else if (bad) {
    emit_type_error("type-mismatch-concat",
                    std::format("`concat` requires integer lanes ({})", name_operands(src)),
                    std::format("a lane is an integer bit window (an ordered positional tuple/array lane splices its "
                                "fields) — no implicit conversion, cast explicitly: {}",
                                upass::kBoolIntCastHint));
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
      // Name the operands + their inferred kinds — the comparison has no source
      // span here, so the operand names are the only handle for localizing it.
      std::string ops;
      for (const auto& o : src) {
        if (!ops.empty()) {
          ops += " vs ";
        }
        ops += absl::StrCat(operand_label(o), ":", upass::op_kind::kind_annot(kind_of_operand(o)));
      }
      emit_type_error(code, std::format("`{}` requires both operands to be the same type ({})", sym, ops), cast_hint());
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
  if (dst_name.empty() || src.size() != 1 || !bundle_key::is_single_level(dst_name)) {  // backtick-aware
    return Vote::keep;
  }
  const Kind rhs = kind_of_operand(src.front());
  if (rhs == Kind::nil) {
    return Vote::keep;  // `= nil` neither establishes nor changes the kind
  }
  Kind cur = kind_of_bundle(dst);
  if (cur == Kind::unknown) {
    // An OUTPUT port's declared kind rides io_meta, never its bundle, yet it is
    // a typed destination like a typed local: a `u1` output takes no bool and
    // a `bool` output no bit.
    cur = declared_kind(ssa_base(dst_name));
  }
  // "an unset/nil scalar destination does not infer a new tuple shape
  // from a tuple RHS": a never-typed dst whose current VALUE is the nil it
  // was initialized with cannot become an aggregate. The runner's inliner
  // marks its own synthesized nil seeds (Symbol_table::nil_seeded) — those
  // prologues legally bind a tuple over the seed.
  if (cur == Kind::unknown && rhs == Kind::tuple && runner_st != nullptr) {
    const auto& t0 = dst.get_entry(bundle_path::of_string("0")).trivial;
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
    // A typed positional array has an integer packed-bit view. A bit-range
    // update lowers to set_mask(integer) followed by a whole store back into
    // the array; that store changes representation, not the source type.
    // ONLY that store: the RHS must be the round trip's own temp, cut from THIS
    // array (process_set_mask records it). Every other integer RHS — plainly
    // `arr = 5` — stays a type error, or it would silently broadcast the scalar
    // into every lane. Named tuples have no implicit bit order: also rejected.
    if (cur == Kind::tuple && rhs == Kind::integer && !dst.has_named_top() && !dst.get_attr("__elem_max").is_invalid()
        && !src.front().name.empty()) {
      const auto it = bitview_tmp_.find(src.front().name);
      if (it != bitview_tmp_.end() && it->second == ssa_base(dst_name)) {
        return Vote::keep;
      }
    }
    // The cast advice only exists between bool and integer; a tuple or string
    // mismatch has no cast to offer.
    const bool bool_int = (rhs == Kind::boolean && cur == Kind::integer) || (rhs == Kind::integer && cur == Kind::boolean);
    emit_type_error("assign-type-mismatch",
                    std::format("cannot assign {} value to `{}` (it is {}); a variable's type cannot change",
                                kind_name(rhs),
                                upass::Lnast_manager::user_name(dst_name),
                                kind_name(cur)),
                    bool_int ? std::format("use a new variable, or cast explicitly: {}", upass::kBoolIntCastHint)
                             : std::string{"use a new variable"});
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
      if (Lnast_ntype::is_ref(get_raw_ntype()) && reject_clock_data(current_text(), "an `if` condition")) {
        continue;
      }
      Kind k = kind_of_operand_at_cursor();
      if (k == Kind::nil) {
        emit_type_error("nil-operand",
                        "`nil` is invalid as a condition (only copy, `==nil`/`!=nil`, `.[valid]`)",
                        "",
                        span_from_nid(if_nid));
      } else if (k != Kind::unknown && k != Kind::boolean) {
        emit_type_error("cond-not-bool",
                        std::format("condition must be boolean, got {}", kind_name(k)),
                        k == Kind::integer ? "an integer (e.g. a bit select `x#[0]`) is a value, not a condition — "
                                             "did you mean `!= 0`? (write `if x#[0] != 0`)"
                                           : "compare explicitly, e.g. `if x != 0`",
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
                      k == Kind::integer ? "an integer (e.g. a bit select `x#[0]`) is a value, not a condition — "
                                           "did you mean `!= 0`? (write `while x#[0] != 0`)"
                                         : "compare explicitly, e.g. `while x != 0`",
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
  // child1 may be the obligation-KIND sentinel (`__fkind__assume` /
  // `__fkind__assert_always` / `__fkind__cassert`) that prp2lnast inserts ahead
  // of the message; skip it, or the message never gets type-checked. Guarded:
  // the sentinel is the last child when there is no message.
  bool at_msg = move_to_sibling();
  if (at_msg && current_text().rfind("__fkind__", 0) == 0) {
    at_msg = move_to_sibling();
  }
  if (at_msg) {
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
    emit_type_error("type-mismatch-range", "operator `..=` requires integer operands", cast_hint());
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
    return ln->span_of_nearest(nid);  // fall back to the nearest located ancestor
  }
  return {};
}

// ── arithmetic / bitwise / shift: int operands → int (NO bool) ──────────────
// clang-format off
upass::Vote uPass_typecheck::process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_plus, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_minus, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_mult, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_div, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_mod, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_bit_and, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_bit_or, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_bit_xor, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_bit_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_bit_not, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_shl(std::string_view, Bundle& dst, upass::Src_span src) { require_shift("<<", dst, src); return Vote::keep; }
upass::Vote uPass_typecheck::process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_sra, dst, src, dst_name); return Vote::keep; }

// ── logical keywords: bool operands → bool (NO int — use `&`/`|`) ────────────
upass::Vote uPass_typecheck::process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_log_and, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_log_or, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_log_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_log_not, dst, src, dst_name); return Vote::keep; }

// ── reductions / popcount: integer operand → unsigned integer ──────────────
upass::Vote uPass_typecheck::process_red_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_red_or, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_red_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_red_and, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_red_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_red_xor, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_popcount(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_popcount, dst, src, dst_name); return Vote::keep; }

// ── comparison: eq/ne same-class → bool; ordering int → bool ────────────────
upass::Vote uPass_typecheck::process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_eq, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_ne, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_lt, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_le(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_le, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_gt, dst, src, dst_name); return Vote::keep; }
upass::Vote uPass_typecheck::process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span src) { require_rule(Lnast_ntype::Lnast_ntype_ge, dst, src, dst_name); return Vote::keep; }

// ── bit manipulation / type-id: result kind only (operands not kind-checked) ─
upass::Vote uPass_typecheck::process_sext(std::string_view, Bundle& dst, upass::Src_span) { set_dst_kind(dst, Kind::integer); return Vote::keep; }
upass::Vote uPass_typecheck::process_concat(std::string_view, Bundle& dst, upass::Src_span src) { require_concat(dst, src); return Vote::keep; }
// clang-format on

upass::Vote uPass_typecheck::process_set_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  set_dst_kind(dst, Kind::integer);
  // Record the packed-bit-view round trip so process_store can admit its (and
  // only its) integer-into-array write-back. src[0] is the value being updated:
  // the array itself on the first write of a chain, the prior round-trip temp
  // afterwards. Anything else UNRECORDS the dst — a name re-defined by an
  // ordinary set_mask is no longer a bit view of an array.
  if (!dst_name.empty()) {
    // COPY the root out: it may be a view of a bitview_tmp_ VALUE, and the
    // insert below can rehash the map out from under it.
    const std::string root{src.empty() ? std::string_view{} : bitview_root_of(src.front())};
    if (root.empty()) {
      bitview_tmp_.erase(dst_name);
    } else {
      bitview_tmp_[std::string(dst_name)] = root;
    }
  }
  return Vote::keep;
}

std::string_view uPass_typecheck::bitview_root_of(const upass::Operand& o) const {
  if (o.name.empty()) {
    return {};  // a const literal is nobody's bit view
  }
  if (const auto it = bitview_tmp_.find(o.name); it != bitview_tmp_.end()) {
    return it->second;  // chained write: `set_mask(%t1, %t0, …)` keeps %t0's array
  }
  // A typed positional array: unnamed (positional) tops carrying the element
  // envelope a comp_type_array declare bakes. Named tuples have no implicit bit
  // order, so they are never a bit view.
  if (!o.bundle || o.bundle->has_named_top() || o.bundle->get_attr("__elem_max").is_invalid()) {
    return {};
  }
  return ssa_base(o.name);
}

// ── aggregates: passthrough kinds, no homogeneity check ─────────────────────
upass::Vote uPass_typecheck::process_tuple_add(std::string_view, Bundle& dst, upass::Src_span src) {
  // Every tuple_add in the tree is a REAL tuple literal: `(expr)` groupings
  // were unwrapped at parse (db87b5908 — `(x)` no-comma unwraps, `(x,)` is
  // kept), so even a single-operand node is a 1-tuple.  An OPERAND-LESS node is
  // the empty tuple `()` (that is how `mut d = ()` lowers) — it must be stamped
  // too, or `()` carries no tuple evidence and is indistinguishable from an
  // untyped runtime scalar (see constprop's empty-tuple compare fold).
  (void)src;
  set_dst_kind(dst, Kind::tuple);
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
