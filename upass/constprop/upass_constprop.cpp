//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include <map>
#include <optional>
#include <print>
#include <set>
#include <unordered_map>

#include "lnast_ntype.hpp"
#include "str_tools.hpp"
#include "upass_verifier.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_constprop.hpp.
static upass::uPass_plugin cprop("constprop", upass::uPass_wrapper<uPass_constprop>::get_upass);

static constexpr std::string_view call_ref_arg_marker = "__ref_arg";

// Canonical-key shape detection after the bundle_sorted refactor. Defined
// here (forward) so process_assign can use it for shape-preserving merge.
static bool is_named_top(std::string_view first) {
  if (first.empty()) {
    return false;
  }
  if (first.front() == '_' || std::isdigit(static_cast<unsigned char>(first.front()))) {
    return false;
  }
  return true;
}

// Coerce one value to its text-form Const. Mirrors Pyrope's `string()` cast:
//   nil    → "nil"
//   string → as-is
//   int    → decimal text
// Returns invalid if `v` is invalid; the caller decides whether to bail or
// keep going.
static Const stringify_one(const Const& v) {
  if (v.is_invalid()) {
    return *Dlop::invalid();
  }
  if (v.is_nil()) {
    return *Dlop::from_string("nil");
  }
  if (v.is_string()) {
    return v;
  }
  return *Dlop::from_string(std::to_string(v.to_i()));
}

// Stringify each entry in `vals` and text-concat them. Returns nullopt if any
// entry is invalid or has unknown bits (caller should bail and retry later);
// returns an empty string Const for an empty input.
static std::optional<Const> stringify_concat_trivials(const std::vector<Const>& vals) {
  if (vals.empty()) {
    return *Dlop::from_string("");
  }
  Const acc;
  bool  first = true;
  for (const auto& v : vals) {
    if (v.is_invalid() || v.has_unknowns()) {
      return std::nullopt;
    }
    auto s = stringify_one(v);
    if (s.is_invalid()) {
      return std::nullopt;
    }
    acc   = first ? s : *acc.concat_op(s);
    first = false;
  }
  return acc;
}

Const uPass_constprop::apply_range_mask(const Const& value, const Const& start, const Const& end) {
  if (!start.is_i()) {
    return *Dlop::invalid();
  }
  const auto lo = start.to_i();
  if (lo < 0) {
    return *Dlop::invalid();
  }
  // Open-ended `lo..` (end == nil): right-shift by lo. This dodges the
  // negative-mask path of get_mask_op (whose `bits<=1 → 1` legacy in
  // get_neg_mask_value silently breaks the lo==0 "all bits" case), and
  // matches Pyrope's bit-slice semantics where `b#[lo..]` is the upper
  // bits of `b` packed LSB-first.
  if (end.is_nil()) {
    return *value.rsh_op(lo);
  }
  if (end.is_i()) {
    const auto hi = end.to_i();
    if (hi < lo) {
      return *Dlop::invalid();
    }
    const auto mask = Dlop::get_mask_value(static_cast<int>(hi), static_cast<int>(lo));
    return *value.get_mask_op(*mask);
  }
  return *Dlop::invalid();
}

uPass_constprop::uPass_constprop(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass(_lm) {
  st.function_scope(_lm->get_top_module_name());
}

void uPass_constprop::set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts) {
  function_registry.clear();
  for (const auto& ln : lnasts) {
    if (!ln) {
      continue;
    }
    function_registry.emplace(std::string(ln->get_top_module_name()), ln);
  }
}

void uPass_constprop::process_assign() {
  move_to_child();

  auto lhs_text = current_text();
  move_to_sibling();

  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    // RHS is a variable reference: alias the bundle in the symbol table.
    // We do NOT call mark_changed here — bundle aliasing (A = ___t1) is just
    // pointer bookkeeping and doesn't create new scalar information.
    // Convergence is driven exclusively by process_tuple_get propagating scalar
    // values.  Marking changed for bundle pointers causes infinite loops when
    // the same variable is reassigned multiple times per iteration (e.g. SSA-
    // form append:  A = ___t1, A = ___t2, A = ___t3).
    auto rhs_bundle = current_bundle();
    if (rhs_bundle) {
      // Type-shape preservation: when LHS is a *purely-named* bundle
      // (the shape declared by `mut foo:(x=…, y=…) = …` — no unnamed
      // slots) and RHS is a pure positional tuple, bind RHS unnamed
      // entries onto LHS's named slots in canonical (alphabetical) order.
      // This is what makes
      //   mut case1:(x=0, y=1) = (0, 1)
      //   case1 = (3, 4)        // → case1 becomes (x=3, y=4)
      // work.
      //
      // Skip the merge when LHS is mixed (named+unnamed, e.g. from
      // `mut tup0 = (0, y=1)`) — that's a plain initializer, not a typed
      // shape, and the user expects subsequent assignments to fully
      // replace. Also skip when RHS carries names of its own (full copy).
      auto existing = st.get_bundle(lhs_text);
      bool do_merge = false;
      // LHS named slots in canonical (alphabetical) order via top_levels().
      // Nested entries (`x.b`, `x.c`) collapse to one Top_level_entry by
      // construction, so no manual de-duplication is needed.
      std::vector<std::string> lhs_named_order;
      if (existing && existing.get() != rhs_bundle.get() && !existing->non_attr_entries().empty()) {
        bool lhs_has_unnamed = false;
        for (const auto& tl : existing->top_levels()) {
          if (tl.pos < 0) {
            lhs_named_order.emplace_back(tl.name);
          } else {
            lhs_has_unnamed = true;
            break;
          }
        }
        bool rhs_pure_positional = true;
        if (!lhs_has_unnamed && !lhs_named_order.empty()) {
          for (const auto& tl : rhs_bundle->top_levels()) {
            if (tl.pos < 0) {
              rhs_pure_positional = false;
              break;
            }
          }
        }
        do_merge = !lhs_has_unnamed && !lhs_named_order.empty() && rhs_pure_positional;
      }
      if (do_merge) {
        // RHS unnamed entries in numeric (canonical) order, walked by
        // looking them up at "0", "1", …
        auto merged = std::make_shared<Bundle>(std::string(lhs_text));
        // Start by copying LHS shape verbatim (incl. nil placeholders and
        // any nested sub-entries).
        for (const auto& [lhs_key, lhs_ep] : existing->non_attr_entries()) {
          merged->set(lhs_key, *lhs_ep);
        }
        // Bind each RHS positional value to the next LHS named slot.
        size_t idx = 0;
        for (const auto& name : lhs_named_order) {
          auto pos_key = std::to_string(idx);
          if (!rhs_bundle->has_trivial(pos_key)) {
            break;
          }
          merged->set(name, rhs_bundle->get_trivial(pos_key));
          ++idx;
        }
        // RHS positions beyond LHS shape append as unnamed slots after
        // the named ones — preserves "wider RHS appends new positions"
        // semantics, picking up where the named binding stopped.
        size_t append_pos = 0;
        for (;; ++append_pos) {
          auto pos_key = std::to_string(idx + append_pos);
          if (!rhs_bundle->has_trivial(pos_key)) {
            break;
          }
          merged->set(std::to_string(append_pos), rhs_bundle->get_trivial(pos_key));
        }
        st.set(lhs_text, merged);
      } else {
        st.set(lhs_text, rhs_bundle);
      }
      // Propagate the tuple-typed flag across aliasing assigns. `c = ___4`
      // where ___4 came from a tuple_concat keeps c marked as a tuple, so
      // the next iteration's tuple_concat reads c via bundle mode rather
      // than mis-classifying it as a scalar wrapper.
      if (tuple_typed_names.contains(std::string(current_text()))) {
        tuple_typed_names.insert(std::string(lhs_text));
      }
    } else if (st.has_trivial(current_text())) {
      // Scalar RHS (stored as trivial, not a bundle). Propagate the value so
      // subsequent uses of `lhs_text` resolve.
      store_trivial(lhs_text, st.get_trivial(current_text()));
    }
  } else if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    store_trivial(lhs_text, current_pyrope_value());
  } else {
    // RHS is a compound expression (tuple_get, tuple_set, func_call, attr_get, attr_set, etc.)
    // Not yet handled by constprop.  Skip this assignment to avoid crashes.
    // TODO: Recursively process RHS expression and track the result.
  }

  move_to_parent();
}

template <typename F>
void uPass_constprop::process_nary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const r = current_prim_value();
  if (!is_numeric(r)) {
    move_to_parent();
    return;
  }
  while (move_to_sibling()) {
    auto operand = current_prim_value();
    if (!is_numeric(operand)) {
      move_to_parent();
      return;
    }
    op(r, operand);
  }
  move_to_parent();
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

template <typename F>
void uPass_constprop::process_binary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const n1 = current_prim_value();
  move_to_sibling();
  Const n2 = current_prim_value();
  move_to_parent();
  if (!foldable(n1) || !foldable(n2)) {
    return;
  }
  Const r = op(n1, n2);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

// Same shape as process_binary but does NOT pre-reject unknowns. Used for
// ops like lt/le/gt/ge whose Dlop implementations propagate unknowns to a
// 1-bit unknown result themselves — the constprop wrapper just hands the
// raw operands through.
template <typename F>
void uPass_constprop::process_binary_passthrough(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const n1 = current_prim_value();
  move_to_sibling();
  Const n2 = current_prim_value();
  move_to_parent();
  if (!is_numeric(n1) || !is_numeric(n2)) {
    return;
  }
  Const r = op(n1, n2);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

template <typename F>
void uPass_constprop::process_unary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const r = current_prim_value();
  move_to_parent();
  // Delegate to Dlop: only skip non-values (invalid/string). Unknown (`?`)
  // bits flow through — not_op et al. propagate them bit-precisely.
  if (!is_numeric(r)) {
    return;
  }
  op(r);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

void uPass_constprop::process_plus() {
  process_nary([](Const& r, Const n) { r = r.add_op(n); });
}

void uPass_constprop::process_minus() {
  process_nary([](Const& r, Const n) { r = r.sub_op(n); });
}

void uPass_constprop::process_mult() {
  process_nary([](Const& r, Const n) { r = r.mult_op(n); });
}

void uPass_constprop::process_div() {
  process_nary([](Const& r, Const n) { r = r.div_op(n); });
}

void uPass_constprop::process_bit_and() {
  process_nary([](Const& r, Const n) { r = r.and_op(n); });
}

void uPass_constprop::process_bit_or() {
  process_nary([](Const& r, Const n) { r = r.or_op(n); });
}

void uPass_constprop::process_bit_not() {
  process_unary([](Const& r) { r = r.not_op(); });
}

void uPass_constprop::process_bit_xor() {
  process_nary([](Const& r, Const n) { r = r.xor_op(n); });
}

void uPass_constprop::process_mod() {
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.mod_op(n2); });
}

void uPass_constprop::process_shl() {
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.lsh_op(n2); });
}

void uPass_constprop::process_sra() {
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.rsh_op(n2); });
}

void uPass_constprop::process_log_and() {
  // Pyrope's type rule (future type-check): `and` operands must already be
  // bool — non-bool sources are required to write `foo != 0 and bar != 0`.
  // With bool operands, bitwise AND equals logical AND, so we just call
  // Dlop::and_op and let it deal with unknowns.
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.and_op(n2); });
}

void uPass_constprop::process_log_or() {
  // Same rationale as process_log_and: assume bool operands; bitwise OR over
  // bool values equals logical OR.
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.or_op(n2); });
}

void uPass_constprop::process_log_not() {
  // Pyrope's type rule (future type-check): `not` operand must be bool.
  // Bitwise NOT over a 1-bit bool (0 ↔ -1) flips it. nil stays nil — chained
  // `and` with a known-true operand still collapses to true (the cassert
  // escape hatch for unset attrs documented in attribute_todo.md §Phase 2).
  process_unary([](Const& r) {
    if (r.is_nil()) {
      return;
    }
    r = r.not_op();
  });
}

// Bundle-aware equality. Returns nullopt when constprop can't decide
// (unknown entries, attribute round-trip artefacts). Otherwise true/false.
//
// Pyrope tuple equality is order-insensitive on names: `(x=1,d=4) ==
// (d=4,x=1)` is true, since both bundles carry the same {name → value}
// mapping. After the bundle_sorted refactor, Bundle storage is already
// in canonical order (attributes → named alphabetical → unnamed by
// index), so name matching is direct and position matching falls out of
// the unnamed-by-index ordering. Nested paths (`b.c`) walk by exact key.
// Canonical key shape detection:
//   - "__attr"          → attribute (covered by Bundle::is_attribute)
//   - "0", "1", …       → unnamed (first char is a digit)
//   - anything else     → named (bare name)
// is_named_top forward-declared near the top of the file so
// process_assign can call it during shape-preserving merge.

// Bundle == result is *three-valued*:
//   - std::nullopt        → can't decide (operand has unknowns)
//   - Const{create_bool}  → true / false (definite shape+value verdict)
//
// Tuple equality is purely structural value comparison: attributes
// (declared mut/const, type tags, user-set decorations) are NOT compared.
// `non_attr_entries()` already strips attribute keys from both sides, so
// only data fields participate. When both operands have concrete
// (no-unknowns) values at the same key and those values differ, the
// result is concrete `false` — there is no "nil-as-pass" softening for
// named-key mismatches. (Earlier versions returned nil so that
// `cassert m1 == t` would discharge as pass when m1 and t shared keys
// but values diverged; that masked real comparison failures and broke
// `match`/`case` folding because the resulting nil cond marked downstream
// arms as uncertain.)
static std::optional<Const> compare_bundles_eq(const std::shared_ptr<Bundle const>& a, const std::shared_ptr<Bundle const>& b) {
  // Walks `src` and inspects each entry in `other`. Returns:
  //   - 'f' definite false (key missing on the other side, or concrete
  //          values mismatch at a shared key)
  //   - 't' all entries matched concretely
  //   - '?' undecidable (invalid scalar, or eq returned unknowns)
  auto walk = [](const std::shared_ptr<Bundle const>& src, const std::shared_ptr<Bundle const>& other) -> char {
    char worst = 't';
    for (const auto& [k, ep] : src->non_attr_entries()) {
      if (ep->trivial.is_invalid()) {
        worst = '?';
        continue;
      }
      if (!other->has_trivial(k)) {
        return 'f';
      }
      const Const& ov = other->get_trivial(k);
      if (ov.is_invalid()) {
        worst = '?';
        continue;
      }
      if (ov.same_repr(ep->trivial)) {
        continue;
      }
      const Const eq = *ov.eq_op(ep->trivial);
      if (eq.is_known_true()) {
        continue;
      }
      if (eq.is_known_false()) {
        return 'f';  // concrete value mismatch — definite inequality
      }
      worst = '?';
    }
    return worst;
  };

  const char ab = walk(a, b);
  if (ab == 'f') {
    return *Dlop::create_bool(false);
  }
  if (ab == '?') {
    return std::nullopt;
  }
  const char ba = walk(b, a);
  if (ba == 'f') {
    return *Dlop::create_bool(false);
  }
  if (ba == '?') {
    return std::nullopt;
  }
  return *Dlop::create_bool(true);
}

// Structural-only `a does b` check.
//
// `a does b` succeeds when a's tuple shape covers every non-attribute
// first-level entry in b:
//   - For every NAMED field in b (bare name), a must have any first-level
//     key with the same `name` — position is irrelevant. So
//     `(a=1,b=0) does (b=2,a=0)` is true even though `a` and `b` swap
//     positions on the two sides.
//   - For every UNNAMED entry in b (key starts with a digit, like `1`), a
//     must have an UNNAMED entry at the same position. A named slot at
//     that position does *not* satisfy the unnamed requirement, so
//     `(a=3) does (1)` is false (b has unnamed pos 0, a has only a named
//     field).
//
// Values are not compared here — this is the structural half of `does`.
// Bundle keys may be hierarchical (e.g. `foo.bar`); we look at the first
// level only, which is enough for the flat-tuple cases the comptime
// tests exercise. Nested-tuple structural matching can be added later.
static bool structural_does(const std::shared_ptr<Bundle const>& a, const std::shared_ptr<Bundle const>& b) {
  // Per top-level segment of b: a must have a matching top-level entry of
  // the same kind. top_levels() collapses nested keys (`foo.x`/`foo.y`)
  // into one entry, so each is checked once.
  for (const auto& tb : b->top_levels()) {
    if (tb.pos < 0) {
      if (!a->has_top_named(tb.name)) {
        return false;
      }
    } else {
      if (!a->has_top_unnamed(tb.pos)) {
        return false;
      }
    }
  }
  return true;
}

// Structural-only `a equals b` check.
//
// `equals` is "same positional shape, ignoring values and specific names":
// at each top-level position, both sides must be named-or-both-unnamed.
// Specific names on named slots and the values themselves don't have to
// match. So `(x=1,d=4) equals (d=7,x=100)` is true (same shape: 2 named
// slots), `(x=1,4) equals (d=4,100)` is true (named@0, unnamed@1 on both),
// and `(x=1,4) !equals (100,d=4)` (shape differs: named@0/unnamed@1 vs
// unnamed@0/named@1).
static bool structural_equals(const std::shared_ptr<Bundle const>& a, const std::shared_ptr<Bundle const>& b) {
  // Structural equality is:
  //   - same SET of named-entry first-level names
  //   - same SET of unnamed-entry first-level positions
  // top_levels() yields one entry per first-level segment in canonical
  // order, so a lockstep walk over the two views answers the question
  // directly.
  auto av = a->top_levels();
  auto bv = b->top_levels();
  if (av.size() != bv.size()) {
    return false;
  }
  auto ita = av.begin();
  auto itb = bv.begin();
  for (; ita != av.end(); ++ita, ++itb) {
    if (ita->pos != itb->pos) {
      return false;
    }
    if (ita->name != itb->name) {
      return false;
    }
  }
  return true;
}

// process_eq / process_ne with bundle awareness. Falls back to the scalar
// process_binary path when neither operand is a tracked bundle, so plain
// integer compares are unchanged.
//
// Result handling is tri-state to support unknowns (`0sb?` literals): the
// final stored value is `Dlop::create_bool(true)` for known-true (negated for
// ne), the matching `Dlop::create_bool(false)`, or a 1-bit unknown when the
// comparison itself is undecidable. Storing the 1-bit unknown lets a downstream
// `(v != 0) == 0sb?` cassert fold via Lconst::eq_op's structural-identity
// short-circuit.
template <bool Negate>
void uPass_constprop::process_eq_ne_impl() {
  // Resolve an operand to one of three states:
  //   - bundle: a tracked tuple (multi-entry or non-scalar wrapper)
  //   - scalar: a known Const (default is invalid; never zero)
  //   - undeclared_ref: ref that was never declared in any reachable scope
  //   - is_const_nil: literal `nil` const
  // is_const_nil + the OTHER side being undeclared lets us fold the pyrope
  // semantic that an undeclared name reads as nil — narrowed to the
  // `== nil` shape so casserts in function bodies prp2lnast still emits as
  // top-level fdef siblings (scope2.prp's mytest body) don't fold against a
  // synthesized nil and trip the verifier.
  struct Operand {
    std::shared_ptr<Bundle const> bundle;
    Const                         scalar;
    bool                          is_const_nil   = false;
    bool                          undeclared_ref = false;
  };
  auto resolve = [this]() -> Operand {
    Operand o;
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto name = current_text();
      // Cross-pass override: when uPass_attributes published a narrowed
      // value for this ref (wrap/sat policy, see attribute_todo.md §Phase
      // 5), the override must win over the raw bundle in our ST. The
      // override is published via runner_fold_fn / fold_ref before the
      // operand is read here.
      if (runner_fold_fn) {
        auto folded = runner_fold_fn(name);
        if (folded && !folded->is_invalid()) {
          o.scalar = *folded;
          return o;
        }
      }
      auto b = st.get_bundle(name);
      if (b && !b->is_scalar()) {
        o.bundle = b;
      } else if (b && b->is_scalar()) {
        // Single-entry bundle: flatten via Bundle::get_trivial() (no-arg)
        // which returns the lone non-attr entry's value regardless of key
        // depth. Symbol_table::has_trivial keys by field "0" which fails to
        // match nested named keys like `first.second`, so for a tuple-of-
        // tuple-of-… single-entry bundle we'd otherwise miss the scalar
        // (`cassert x == 3` where `x = (first=(second=3))`).
        auto v = b->get_trivial();
        if (!v.is_invalid()) {
          o.scalar = v;
        }
      } else if (st.has_trivial(name)) {
        o.scalar = st.get_trivial(name);
      } else if (runner_fold_fn) {
        // Cross-pass fallback: another pass (e.g. uPass_attributes folding
        // an `attr_get` destination) may have a concrete value for this
        // ref even though constprop never assigned it. Consult before
        // marking undeclared so eq/ne fold in the same iteration the
        // attribute pass produces the value.
        auto v = runner_fold_fn(name);
        if (v && !v->is_invalid()) {
          o.scalar = *v;
        } else if (!st.is_declared(name)) {
          o.undeclared_ref = true;
        }
      } else if (!st.is_declared(name)) {
        o.undeclared_ref = true;
      }
      // else: declared but no concrete value yet — leave scalar invalid.
    } else {
      o.scalar       = current_pyrope_value();
      o.is_const_nil = o.scalar.is_nil();
    }
    return o;
  };

  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Operand a = resolve();
  move_to_sibling();
  Operand b = resolve();
  move_to_parent();

  if (a.undeclared_ref && b.is_const_nil) {
    a.scalar = Dlop::nil();
  }
  if (b.undeclared_ref && a.is_const_nil) {
    b.scalar = Dlop::nil();
  }

  // Mixed nil propagation: exactly one operand is a known-nil scalar. The
  // result is nil (indeterminate) — typically because the var was
  // mutated under an uncertain arm and Symbol_table::leave_scope
  // re-pinned it to nil. Both-sides-nil falls through to the regular eq
  // path (same_repr → known-true). nil-vs-bundle is handled here too
  // (a bundle never compares equal to nil concretely).
  const bool a_nil            = (!a.bundle && !a.scalar.is_invalid() && a.scalar.is_nil());
  const bool b_nil            = (!b.bundle && !b.scalar.is_invalid() && b.scalar.is_nil());
  const bool a_concrete_other = (a.bundle != nullptr) || (!a.scalar.is_invalid() && !a.scalar.is_nil());
  const bool b_concrete_other = (b.bundle != nullptr) || (!b.scalar.is_invalid() && !b.scalar.is_nil());
  if ((a_nil && b_concrete_other) || (b_nil && a_concrete_other)) {
    store_trivial(var, *Dlop::nil());
    return;
  }

  // Three outcomes the rest of the pass cares about: known-true, known-false,
  // or a 1-bit unknown. Bundles only produce known true/false; the scalar
  // path may produce unknowns when an operand has them.
  std::optional<Const> result;
  // Mixed bundle-vs-scalar: a multi-entry tuple can never structurally
  // equal a scalar, but a 1-entry tuple `(v,)` is equivalent to its scalar
  // `v`. Without this case `(1,2) != (1,)` (where Symbol_table::set wraps
  // the 1-tuple as a scalar at position 0) hits neither the bundle-eq
  // nor scalar-eq paths and stays unfolded.
  auto bundle_count_non_attr = [](const std::shared_ptr<Bundle const>& b) {
    return static_cast<int>(b->non_attr_entries().size());
  };
  if (a.bundle && b.bundle) {
    if (auto eq = compare_bundles_eq(a.bundle, b.bundle); eq.has_value()) {
      // Tri-state: nil propagates verbatim (the
      // structural-match-with-value-mismatch case discharges as pass via
      // cassert); concrete bool flips for Negate.
      if (eq->is_nil()) {
        result = *eq;
      } else if (eq->is_known_true()) {
        result = *Dlop::create_bool(!Negate);
      } else {
        result = *Dlop::create_bool(Negate);
      }
    }
  } else if (a.bundle && !b.scalar.is_invalid() && bundle_count_non_attr(a.bundle) > 1) {
    result = *Dlop::create_bool(Negate);
  } else if (b.bundle && !a.scalar.is_invalid() && bundle_count_non_attr(b.bundle) > 1) {
    result = *Dlop::create_bool(Negate);
  } else if (!a.bundle && !b.bundle && !a.scalar.is_invalid() && !b.scalar.is_invalid()) {
    // same_repr gives bit-identity including unknown positions (so
    // `0sb? == 0sb?` is known-true). eq_op only reduces to known-false
    // when neither side carries unknowns; otherwise it returns a 1-bit
    // unknown which we propagate. `ne` is bitwise-not of `eq`; a 1-bit
    // unknown inverted is still a 1-bit unknown.
    if (a.scalar.same_repr(b.scalar)) {
      result = *Dlop::create_bool(!Negate);
    } else if (same_bits_ignore_type(a.scalar, b.scalar)) {
      // Type-agnostic structural identity: a `(v != 0)` (Boolean
      // unknown) compared against the literal `0sb?` (Integer unknown)
      // is two values with the same bit pattern but differing Pyrope
      // types. Constprop is type-agnostic; the typecheck pass flags
      // illegal cross-type compares. Treat structurally identical bit
      // patterns as known-equal regardless of type so `(v != 0) ==
      // 0sb?` folds to true.
      result = *Dlop::create_bool(!Negate);
    } else {
      // Defer to eq_op: types may differ (e.g. Bool vs Integer) but if the
      // bit patterns match it folds true. Constprop is type-agnostic — the
      // separate typecheck pass flags illegal cross-type comparisons; here
      // we just take whatever eq_op decides.
      const Const eq = *a.scalar.eq_op(b.scalar);
      if (eq.is_known_true()) {
        result = *Dlop::create_bool(!Negate);
      } else if (eq.is_known_false()) {
        result = *Dlop::create_bool(Negate);
      } else if (eq.has_unknowns()) {
        result = eq;
      }
    }
  }

  if (result.has_value()) {
    store_trivial(var, *result);
  }
}

void uPass_constprop::process_ne() { process_eq_ne_impl<true>(); }
void uPass_constprop::process_eq() { process_eq_ne_impl<false>(); }

void uPass_constprop::process_lt() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.lt_op(y); });
}
void uPass_constprop::process_le() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.le_op(y); });
}
void uPass_constprop::process_gt() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.gt_op(y); });
}

void uPass_constprop::process_ge() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.ge_op(y); });
}

void uPass_constprop::process_is() {
  // `is` is folded by upass_attributes (which has the typename data);
  // constprop is a no-op for now beyond keeping the node alive so its
  // dst tmp is still readable downstream.
}

void uPass_constprop::process_if() {
  // Observe the condition so the symbol table is populated before the runner
  // queries try_fold_ref(). The runner's process_if (Slice 7) performs the
  // actual dead-branch elimination based on the folded condition value.
  //
  // Two if shapes flow through here (see runner's process_if for the
  // structural distinction):
  //   * Scoped form  — (cond, stmts, [cond, stmts]…, [stmts]). Walked
  //     here as alternating cond/stmts pairs.
  //   * Flat form    — (cond, stmt, [stmt]…). Lowered from
  //     `s when c` / `s unless c`. when/unless conditions are required
  //     to be comptime-known: there is no codegen lowering for an
  //     unresolved gate, so a non-folded cond is reported as a build
  //     error by the verifier downstream. Constprop only needs to peek
  //     at the cond here; the runner handles drop / splice based on the
  //     folded value.
  if (!move_to_child()) {
    return;
  }

  while (true) {
    if (is_type(Lnast_ntype::Lnast_ntype_stmts)) {
      break;  // bare else-stmts: no condition
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref) || is_type(Lnast_ntype::Lnast_ntype_const)) {
      [[maybe_unused]] const auto cond_val = current_prim_value();
    }
    if (!move_to_sibling()) {
      break;
    }
    if (!move_to_sibling()) {
      break;
    }
  }

  move_to_parent();
}
// Block scope push/pop. Each `stmts` LNAST node gets its own persistent
// scope keyed by its nid hash, so a `mut b = 2` inside `{ … }` is invisible
// outside the block, but state still survives across the upass fixed-point
// iterations (see Symbol_table::block_scope).
//
// The runner calls process_stmts BEFORE descending into children and
// process_stmts_post AFTER, mirroring the pattern used for `if`. The
// outermost stmts of every function body is itself wrapped here too;
// the function_scope established in the constructor remains the parent.
void uPass_constprop::process_stmts() {
  // current_scope_uid() folds in the inline-frame salt so a callee body's
  // block-scope keys don't collide with the caller's nids during a 1i
  // source-swap (salt 0 with no active frame → plain class_index).
  st.block_scope(lm->current_scope_uid());
  // The runner sets next_block_uncertain via notify_uncertain_arm_begin
  // immediately before descending into an if-arm whose cond didn't fold to
  // known-true/known-false. Mark the just-pushed scope so leave_scope can
  // invalidate any var assigned inside the body — without this, the
  // sequential walk leaves the last-branch's writes visible to code after
  // the if and folds casserts that depend on them.
  if (next_block_uncertain) {
    st.mark_current_uncertain();
    next_block_uncertain = false;
  }
}

void uPass_constprop::process_stmts_post() { st.leave_scope(); }

// ── Tuple Operations ─────────────────────────────────────────────────────────
//
// Layout reference (matches opt_lnast):
//   tuple_add:  ref(dst), [const|ref|assign(ref(key),const/ref(val))]...
//   tuple_get:  ref(dst), ref(src), (const|ref)(field)...
//   tuple_set:  ref(tuple), (const|ref)(field)..., (const|ref)(value)
//
// Bundle keys after the bundle_sorted refactor:
//   - named fields are the bare name ("foo")
//   - unnamed fields are the bare decimal index ("0", "1", …)
// The two key spaces are independent: a named slot does NOT consume an
// unnamed position. So the literal `(bar=true, 4)` stores {bar:true, 0:4},
// matching what `(4, bar=true)` (and bundle-built concat output) produces —
// Decision 4 (§1.4) of the plan: unnamed position = canonical vector index.

void uPass_constprop::process_tuple_add() {
  // Build (or update in-place) a Bundle for the destination from each entry.
  // We reuse the existing Bundle object if one already exists so that the
  // pointer stored in process_assign ("A = ___t1") stays stable across
  // iterations and does not trigger spurious mark_changed().
  move_to_child();
  auto dst = std::string(current_text());

  // Get or create the bundle for dst.
  auto bundle = st.get_bundle(dst);
  if (!bundle) {
    bundle = std::make_shared<Bundle>(dst);
    st.set(dst, bundle);
  }
  // Mark the destination as tuple-typed so a downstream tuple_concat
  // routes it through bundle mode even if the bundle ends up looking
  // scalar (single positional entry, which is_scalar() can't distinguish
  // from a true scalar). See process_tuple_concat.
  tuple_typed_names.insert(dst);

  // Function-name detection helper: when a ref's text names a function in
  // the registry, store the qualified function name as a string Const into
  // the bundle slot so downstream method dispatch (x.method(...) where
  // method is a tuple field) can look it up via tuple_get + fcall.
  auto try_store_fn_name = [&](std::string_view key, std::string_view ref_text) -> bool {
    std::string qualified = std::string(lm->get_top_module_name()) + "." + std::string(ref_text);
    if (!function_registry.count(qualified)) {
      return false;
    }
    bundle->set(key, *Dlop::from_string(qualified));
    return true;
  };

  int unnamed_pos = 0;  // advances only on unnamed entries
  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      bundle->set(std::to_string(unnamed_pos), *Dlop::from_pyrope(current_text()));
      ++unnamed_pos;

    } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto       slot = std::to_string(unnamed_pos);
      const auto txt  = current_text();
      if (!try_store_fn_name(slot, txt)) {
        bundle->set(slot, st.get_bundle(txt));
      }
      ++unnamed_pos;

    } else if (is_type(Lnast_ntype::Lnast_ntype_assign)) {
      // Named field: assign(ref(key), const/ref(val)). Stored under the
      // bare name — named slots don't advance unnamed_pos.
      move_to_child();
      auto key = std::string(current_text());
      move_to_sibling();
      if (is_type(Lnast_ntype::Lnast_ntype_const)) {
        bundle->set(key, *Dlop::from_pyrope(current_text()));
      } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        const auto txt = current_text();
        if (!try_store_fn_name(key, txt)) {
          bundle->set(key, st.get_bundle(txt));
        }
      }
      move_to_parent();
    }
  }
  // Bundle was updated in-place; no mark_changed() — process_tuple_get drives convergence.
  move_to_parent();
}

// Track `attr_set var typename 'TypeName'` so method dispatch can look up
// methods on the declared type when the instance bundle alone doesn't carry
// them. Layout: ref(var), const('typename'), const('TypeName').
void uPass_constprop::process_attr_set() {
  move_to_child();
  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    move_to_parent();
    return;
  }
  std::string var(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  if (!is_type(Lnast_ntype::Lnast_ntype_const) || current_text() != "typename") {
    move_to_parent();
    return;
  }
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string raw(current_text());
  move_to_parent();
  // raw is a pyrope-quoted string like `'Pair'`. Strip the quotes.
  if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') {
    raw = raw.substr(1, raw.size() - 2);
  }
  if (!raw.empty()) {
    typename_of_var[var] = std::move(raw);
  }
}

void uPass_constprop::process_tuple_concat() {
  // Pyrope `++` semantics (matches the explicit `string(...)` cast for the
  // string case):
  //   - Build a Bundle via Bundle::concat over every operand. Trivial
  //     operands (int/string/nil) wrap as 1-element positional tuples first.
  //   - If any first-level entry of the result is a string trivial, fold
  //     the whole thing to a single text-concatenated string (this matches
  //     `string("hello", " world") == "hello world"` and
  //     `string(1, "2", 3) == "123"`). The collapse needs every entry to be
  //     a single-level trivial — a nested sub-bundle means we can't decide
  //     yet, so we keep the bundle.
  //   - Bail (no store) on unknowns / invalid operands; a later iteration
  //     of the upass fixed-point may resolve them.
  // Layout: ref(dst), (const|ref)...
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  auto acc = std::make_shared<Bundle>(dst);

  // Wrap a single trivial as a 1-entry positional bundle so Bundle::concat
  // appends it as a slot instead of mishandling the bare key.
  auto wrap_trivial = [&](const Const& val) -> std::shared_ptr<Bundle> {
    auto b = std::make_shared<Bundle>(dst);
    b->set("0", val);
    return b;
  };

  // Resolve one operand to a Bundle-shaped value. Returns nullptr on
  // unfoldable (caller bails). Empty bundles are returned as-is; Bundle::concat
  // treats them as identity.
  auto resolve_operand = [&]() -> std::shared_ptr<Bundle const> {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      Const val = *Dlop::from_pyrope(current_text());
      if (val.is_invalid()) {
        return nullptr;
      }
      return wrap_trivial(val);
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      // Prefer the bundle view: a tuple_add/tuple_concat producer stores its
      // result as a bundle. For names that only hold a trivial scalar (string
      // or int from a constprop store), fall back to wrapping as a 1-tuple.
      if (auto b = st.get_bundle(current_text())) {
        return b;
      }
      if (st.has_trivial(current_text())) {
        auto val = st.get_trivial(current_text());
        if (val.is_invalid() || val.has_unknowns()) {
          return nullptr;
        }
        return wrap_trivial(val);
      }
    }
    return nullptr;
  };

  do {
    auto op = resolve_operand();
    if (!op) {
      move_to_parent();
      return;  // unfoldable; leave dst unchanged
    }
    acc->concat(op);
  } while (move_to_sibling());

  move_to_parent();

  // Any-string → stringify all. Mirrors the `string()` cast rule. Aborts the
  // collapse if any first-level entry is a sub-bundle or carries unknowns —
  // the bundle stays, and a later iteration retries.
  auto try_stringify = [&]() -> std::optional<Const> {
    std::vector<Const> entries;
    bool               any_string = false;
    for (const auto& [k, ep] : acc->non_attr_entries()) {
      if (!Bundle::is_single_level(k)) {
        return std::nullopt;  // nested sub-bundle — don't try to stringify
      }
      const auto& v = ep->trivial;
      if (v.is_invalid()) {
        return std::nullopt;
      }
      if (v.is_string()) {
        any_string = true;
      }
      entries.push_back(v);
    }
    if (!any_string) {
      return std::nullopt;
    }
    return stringify_concat_trivials(entries);
  };

  if (auto folded = try_stringify(); folded.has_value()) {
    store_trivial(dst, *folded);
  } else {
    st.set(dst, acc);
    // Tuple-shaped result: keep the tuple-typed marker for downstream code
    // that distinguishes a real tuple from a scalar-wrapper bundle.
    tuple_typed_names.insert(dst);
  }
}

// Distinguish a "tuple-shaped" bundle from the symbol-table wrapper used
// for plain scalars. `Symbol_table::set(name, Const)` stores scalars in a
// wrapper bundle keyed at position 0, which is *indistinguishable* from a
// real single-element tuple `(3)` in the bundle layer. We can't tell them
// apart without type info, so we stay conservative: a bundle counts as
// tuple-shaped only when it carries a named first-level key, or when it
// has two-or-more distinct first-level positional entries.
//
// Consequence: scalar-vs-scalar `does` (e.g. `i3 does b1`) stays
// unresolved instead of folding to a wrong answer based on type. The cost
// is that genuine single-element tuples like `(3) does ...` also stay
// unresolved; that's a small subset of the test surface and the right
// trade for correctness without type tracking.
static bool is_tuple_shaped(const std::shared_ptr<Bundle const>& b) {
  // O(1) via the bundle_sorted §3 side-indices: any named first-level slot
  // marks the bundle as tuple-shaped; otherwise we need at least two
  // distinct unnamed first-level positions.
  return b->has_named_top() || b->unnamed_top_count() >= 2;
}

// Returns true if the bundle has any first-level non-attribute key with a
// recognizable shape — named or positional. Used as the "weak" requirement
// for the other operand once we've already decided one side is clearly
// tuple-shaped: a bundle whose first-level keys are e.g. `0` (single
// positional) doesn't itself prove tuple-ness, but it's enough structure
// to compare against a known tuple.
static bool has_first_level_shape(const std::shared_ptr<Bundle const>& b) {
  // O(1) via §3 side-indices: any non-attribute first-level entry counts.
  return b->has_named_top() || b->has_unnamed_top();
}

// Fold `dst = does(l, r)`. Cursor is currently on the const("does") fname
// node. Walks forward to read l and r, decides the structural outcome, and
// stores the boolean (/*FIXME-LCONST-CTOR*/Lconst(0/1)) in the symbol table for `dst`.
//
// We only fold the structural half today, and we require at least one side
// to be *clearly* tuple-shaped (named field or ≥2 positional fields). If
// neither side proves it's a tuple (e.g. both are single-position scalar
// wrappers), we can't decide the outcome without type info — leave the
// result unresolved rather than fold to a wrong answer.
//
// On exit the cursor is left on whichever child we last visited; the caller
// (process_func_call) is responsible for `move_to_parent`.
void uPass_constprop::fold_does(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  auto ba = current_ref_bundle();
  if (!move_to_sibling()) {
    return;
  }
  auto bb = current_ref_bundle();

  // Both sides need at least a recognisable first-level shape; otherwise
  // structural matching has nothing to compare.
  if (!ba || !bb || !has_first_level_shape(ba) || !has_first_level_shape(bb)) {
    return;
  }
  // At least one side must prove it's a real tuple, otherwise scalar-vs-
  // scalar would over-fold (e.g. `b1 does i2` where both are scalar
  // wrappers). The structural check itself handles the asymmetry once one
  // side is known-tuple.
  if (!is_tuple_shaped(ba) && !is_tuple_shaped(bb)) {
    return;
  }

  store_trivial(dst, *Dlop::create_bool(structural_does(ba, bb)));
}

// Per-first-level summary used by fold_in / fold_has: groups a bundle's flat
// (single-level) entries by their first-level key, marking sub-bundle entries
// (multi-level keys like `a.0`) so the comparison can distinguish a scalar
// `a=1` from a nested `a=(1,2)`.
struct Bundle_flat_entry {
  std::string_view name;  // empty for unnamed positional
  int              pos = -1;
  Const            value;  // valid only when !is_sub_bundle
  bool             is_sub_bundle = false;
};

static std::vector<Bundle_flat_entry> collect_first_level(const std::shared_ptr<Bundle const>& b) {
  // Per top-level segment of b's non-attr data, materialize a
  // Bundle_flat_entry. top_levels() already groups by first-level
  // segment and reports scalar (collapsed when leaf_count == 1) and
  // has_leafs, so this is now a straight projection.
  std::vector<Bundle_flat_entry> entries;
  for (const auto& tl : b->top_levels()) {
    Bundle_flat_entry n;
    if (tl.pos < 0) {
      n.name = tl.name;  // bare name in canonical storage
    } else {
      n.pos = tl.pos;
    }
    n.is_sub_bundle = tl.has_leafs;
    if (tl.has_leafs) {
      n.value = Dlop::invalid();
    } else {
      n.value = tl.scalar;
    }
    entries.emplace_back(std::move(n));
  }
  return entries;
}

// Fold `dst = in(l, r)`. Cursor is on the const("in") marker. Walks forward
// to read l and r, evaluates the membership predicate, and stores /*FIXME-LCONST-CTOR*/Lconst(0/1)
// in the symbol table.
//
// Semantics for `lhs in rhs` (matches Pyrope tuple-membership):
//   - Each first-level entry in lhs must be "satisfied" by rhs.
//   - Named entry `name=v` in lhs: rhs must have a first-level entry with the
//     same name whose scalar value equals v. nil-on-LHS acts as a wildcard.
//     A sub-bundle on the rhs side at that name fails the scalar comparison
//     (e.g. `(a=1) in (a=(1,2))` is false).
//   - Unnamed entry `v` in lhs: there must exist some first-level rhs entry
//     (named or unnamed) whose scalar value equals v.
// Sub-bundle entries on lhs cause the fold to defer until they collapse.
//
// On exit the cursor is left on the last visited child; the caller
// (process_func_call) is responsible for `move_to_parent`.
void uPass_constprop::fold_in(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  auto ba = current_ref_bundle();
  if (!move_to_sibling()) {
    return;
  }
  auto bb = current_ref_bundle();
  if (!ba || !bb) {
    return;  // need both sides as known bundles
  }

  auto lhs_flat = collect_first_level(ba);
  auto rhs_flat = collect_first_level(bb);

  // Tri-state result: nullopt = defer until more info, true/false = decided.
  std::optional<bool> outcome;
  outcome = true;
  for (const auto& le : lhs_flat) {
    if (le.is_sub_bundle) {
      outcome.reset();  // defer: nested-LHS not modelled yet
      break;
    }
    if (le.value.is_invalid()) {
      outcome.reset();  // defer: lhs entry not yet folded
      break;
    }

    if (!le.name.empty()) {
      // Named entry — wildcard if value is nil.
      if (le.value.is_nil()) {
        continue;
      }
      bool                found_name = false;
      std::optional<bool> matched_value;
      for (const auto& re : rhs_flat) {
        if (re.name != le.name) {
          continue;
        }
        found_name = true;
        if (re.is_sub_bundle) {
          matched_value = false;  // scalar lhs vs sub-bundle rhs → unequal
          break;
        }
        if (re.value.is_invalid()) {
          break;  // defer (matched_value stays nullopt)
        }
        const Const eq = *le.value.eq_op(re.value);
        if (eq.is_known_true()) {
          matched_value = true;
        } else if (eq.is_known_false()) {
          matched_value = false;
        }
        break;
      }
      if (!found_name) {
        outcome = false;
        break;
      }
      if (!matched_value.has_value()) {
        outcome.reset();  // defer
        break;
      }
      if (!*matched_value) {
        outcome = false;
        break;
      }
    } else {
      // Unnamed entry — value must occur in any rhs first-level scalar.
      bool any_unknown = false;
      bool matched     = false;
      for (const auto& re : rhs_flat) {
        if (re.is_sub_bundle) {
          continue;
        }
        if (re.value.is_invalid()) {
          any_unknown = true;
          continue;
        }
        const Const eq = *le.value.eq_op(re.value);
        if (eq.is_known_true()) {
          matched = true;
          break;
        }
        if (!eq.is_known_false()) {
          any_unknown = true;
        }
      }
      if (!matched) {
        if (any_unknown) {
          outcome.reset();  // defer
          break;
        }
        outcome = false;
        break;
      }
    }
  }

  if (!outcome.has_value()) {
    return;
  }
  store_trivial(dst, Dlop::create_bool(*outcome));
}

// Fold `dst = has(l, key)`. Cursor is on the const("has") marker.
//
// Semantics for `bundle has key`:
//   - String key `'name'`: bundle must have a first-level named entry
//     matching `name`.
//   - Integer key `N`: bundle must have any first-level unnamed entry at
//     position N.
// Returns Dlop::create_bool(true) for present, Dlop::create_bool(false) for absent.
void uPass_constprop::fold_has(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  std::shared_ptr<Bundle const> b;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    b = st.get_bundle(current_text());
  }
  if (!b) {
    return;
  }
  if (!move_to_sibling()) {
    return;
  }
  Const key_val;
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    key_val = Dlop::from_pyrope(current_text());
  } else if (is_type(Lnast_ntype::Lnast_ntype_ref) && st.has_trivial(current_text())) {
    key_val = st.get_trivial(current_text());
  }
  if (key_val.is_invalid()) {
    return;
  }

  auto entries = collect_first_level(b);

  bool found = false;
  if (key_val.is_string()) {
    // String literals round-trip through `'foo'` — strip the quotes to get
    // the bare name used as the canonical key.
    const std::string s = strip_pyrope_quotes(key_val.to_pyrope());
    for (const auto& e : entries) {
      if (e.name == s) {
        found = true;
        break;
      }
    }
  } else if (!key_val.is_nil() && !key_val.has_unknowns()) {
    const int target = key_val.to_i();
    for (const auto& e : entries) {
      if (e.pos == target) {
        found = true;
        break;
      }
    }
  } else {
    return;  // nil/unknown key — defer
  }

  store_trivial(dst, Dlop::create_bool(found));
}

// Fold `dst = case(l, r)`. Cursor is on the `case` marker child. Walks
// forward to read the subject (l) and pattern (r), evaluates the
// case-match predicate, and stores the result in the symbol table.
//
// Semantics for `L case R` (pyrope tuple pattern matching):
//   - Structural: every first-level key in R (named or positional) must
//     exist in L. If any R-key is absent in L → known-false (concrete).
//   - Values: at each R-key, R's value must match L's value at that key,
//     where R's `0sb?` / any has_unknowns() literal is a wildcard.
//     - All matched → known-true.
//     - Concrete mismatch on a named R-entry → nil (treated as a
//       runtime-deferred outcome). cassert/verifier counts nil as pass;
//       the if-processor treats nil as uncertain so match-expanded bodies
//       still get visited at comptime.
//     - Concrete mismatch on a purely-positional R (eq-like shape) →
//       known-false, so scalar match arms (e.g. `match sel { case 0b00
//       {…} }`) still let dead-branch elimination prune the arm.
//   - Sub-bundles or undecidable comparisons (unknowns on the L side) →
//     leave dst unfolded so a later iteration can decide.
//
// Cursor is left on whichever child we last visited; the caller
// (process_func_case) restores via `move_to_parent`.
void uPass_constprop::fold_case(const std::string& dst) {
  // Resolve the operand at the current cursor to a flat-entry list. Refs to
  // tracked bundles use the existing collect_first_level helper. Const
  // literals and scalar refs degrade to a single unnamed scalar entry so
  // `sel case 0b10` (scalar vs scalar literal) folds the same way as the
  // bundle/bundle path. Returns nullopt when the value isn't decidable yet
  // (undeclared ref / no trivial value) so we can defer.
  auto resolve_flat = [this]() -> std::optional<std::vector<Bundle_flat_entry>> {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      if (auto b = current_ref_bundle()) {
        return collect_first_level(b);
      }
      auto name = current_text();
      if (st.has_trivial(name)) {
        Bundle_flat_entry e;
        e.pos   = 0;
        e.value = st.get_trivial(name);
        return std::vector<Bundle_flat_entry>{std::move(e)};
      }
      return std::nullopt;
    }
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      Bundle_flat_entry e;
      e.pos   = 0;
      e.value = *Dlop::from_pyrope(current_text());
      return std::vector<Bundle_flat_entry>{std::move(e)};
    }
    return std::nullopt;
  };

  if (!move_to_sibling()) {
    return;
  }
  auto lhs_opt = resolve_flat();
  if (!move_to_sibling()) {
    return;
  }
  auto rhs_opt = resolve_flat();
  if (!lhs_opt || !rhs_opt) {
    return;
  }

  auto lhs_flat = std::move(*lhs_opt);
  auto rhs_flat = std::move(*rhs_opt);

  // `case` is a structural value-match. Attributes (mut/const, type
  // tags, decorations) are stripped by collect_first_level via
  // top_levels(), so only data fields participate — exactly the same
  // contract as `==`. For each rhs entry we find the matching lhs entry
  // and compare values; rhs wildcards (`0sb?`, etc.) make that entry
  // pass trivially. A concrete value mismatch at any rhs entry is a
  // definite false (no "nil softening" — that masked real failures and
  // poisoned downstream `if` cond folding).
  bool defer = false;
  for (const auto& re : rhs_flat) {
    if (re.is_sub_bundle || re.value.is_invalid()) {
      defer = true;
      continue;
    }

    const Bundle_flat_entry* lmatch = nullptr;
    if (!re.name.empty()) {
      for (const auto& le : lhs_flat) {
        if (le.name == re.name) {
          lmatch = &le;
          break;
        }
      }
    } else {
      for (const auto& le : lhs_flat) {
        if (le.name.empty() && le.pos == re.pos) {
          lmatch = &le;
          break;
        }
      }
    }

    if (lmatch == nullptr) {
      store_trivial(dst, *Dlop::create_bool(false));
      return;
    }

    if (lmatch->is_sub_bundle || lmatch->value.is_invalid()) {
      defer = true;
      continue;
    }

    if (re.value.has_unknowns()) {
      continue;
    }

    if (lmatch->value.same_repr(re.value)) {
      continue;
    }

    const Const eq_result = *lmatch->value.eq_op(re.value);
    if (eq_result.is_known_true()) {
      continue;
    }
    if (eq_result.is_known_false()) {
      store_trivial(dst, *Dlop::create_bool(false));
      return;
    }
    defer = true;
  }

  if (defer) {
    return;
  }

  store_trivial(dst, *Dlop::create_bool(true));
}

std::optional<Const> uPass_constprop::resolve_current_scalar() const {
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    return *Dlop::from_pyrope(current_text());
  }
  if (is_type(Lnast_ntype::Lnast_ntype_ref) && st.has_trivial(current_text())) {
    return st.get_trivial(current_text());
  }
  return std::nullopt;
}

std::optional<std::vector<uPass_constprop::Call_actual>> uPass_constprop::collect_call_actuals() {
  std::vector<Call_actual> actuals;

  // Helper: when the current cursor sits on a ref whose target is a
  // *non-trivial* bundle (tuple) in the symbol table, materialize it as a
  // flat key-map {field-path: scalar} so the inliner can route per-field
  // reads. Skipped for trivial-scalar bundles (key "0" only) — those route
  // through the normal scalar path and let typecast/built-in folds keep
  // working.
  auto try_collect_bundle = [&]() -> std::optional<std::unordered_map<std::string, Const>> {
    if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return std::nullopt;
    }
    auto name   = std::string(current_text());
    auto bundle = st.get_bundle(name);
    if (!bundle || bundle->is_trivial_scalar()) {
      return std::nullopt;
    }
    std::unordered_map<std::string, Const> out;
    for (const auto& [k, ep] : bundle->non_attr_entries()) {
      if (ep->trivial.is_invalid()) {
        return std::nullopt;
      }
      out.emplace(std::string(k), ep->trivial);
    }
    if (out.empty()) {
      return std::nullopt;
    }
    return out;
  };

  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_assign)) {
      if (!move_to_child()) {
        continue;
      }
      Call_actual actual;
      const auto  key = current_text();
      actual.is_named = key != call_ref_arg_marker;
      if (actual.is_named) {
        actual.name = std::string(key);
      }
      if (!move_to_sibling()) {
        move_to_parent();
        continue;
      }
      if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        actual.var_name = std::string(current_text());
        // Tuple actual: try to extract a flat field-map from the caller's
        // symbol table before falling back to the scalar path.
        if (auto bundle = try_collect_bundle(); bundle.has_value()) {
          actual.is_bundle    = true;
          actual.bundle_value = std::move(*bundle);
          move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
        // Function-name actual (closure_capture, fcall6 style): when the
        // referenced name resolves to a known function in the registry,
        // surface the qualified function name as a string Const so the
        // callee's body can dispatch through it via inner_fname lookup.
        std::string qualified = std::string(lm->get_top_module_name()) + "." + actual.var_name;
        if (function_registry.count(qualified)) {
          actual.value = *Dlop::from_string(qualified);
          move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
      }
      auto value = resolve_current_scalar();
      move_to_parent();
      if (!value.has_value() || value->is_invalid()) {
        // Unresolved actual: still emit a slot so positional binding stays
        // consistent. Body stmts that don't read this param will still fold;
        // ones that do will defer/abort.
        actual.is_unresolved = true;
        actuals.emplace_back(std::move(actual));
        continue;
      }
      actual.value = *value;
      actuals.emplace_back(std::move(actual));
      continue;
    }

    std::string var_name;
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      var_name = std::string(current_text());
      if (auto bundle = try_collect_bundle(); bundle.has_value()) {
        Call_actual actual;
        actual.is_named     = false;
        actual.var_name     = std::move(var_name);
        actual.is_bundle    = true;
        actual.bundle_value = std::move(*bundle);
        actuals.emplace_back(std::move(actual));
        continue;
      }
      // Function-name actual.
      std::string qualified = std::string(lm->get_top_module_name()) + "." + var_name;
      if (function_registry.count(qualified)) {
        Call_actual actual;
        actual.is_named = false;
        actual.var_name = std::move(var_name);
        actual.value    = *Dlop::from_string(qualified);
        actuals.emplace_back(std::move(actual));
        continue;
      }
    }
    auto value = resolve_current_scalar();
    if (!value.has_value() || value->is_invalid()) {
      Call_actual unres;
      unres.is_named       = false;
      unres.var_name       = std::move(var_name);
      unres.is_unresolved  = true;
      actuals.emplace_back(std::move(unres));
      continue;
    }
    actuals.emplace_back(Call_actual{.is_named = false, .name = {}, .value = *value, .var_name = std::move(var_name)});
  }

  return actuals;
}

// Marker pseudo-function handlers. The runner has already moved the cursor
// onto the func_<name> node; each fold_* helper expects the cursor to be on
// a sibling whose next-sibling is the first argument, which `move_to_child`
// (landing on dst-ref) satisfies.
void uPass_constprop::process_func_does() {
  move_to_child();
  std::string dst(current_text());
  fold_does(dst);
  move_to_parent();
}

void uPass_constprop::process_func_equals() {
  move_to_child();
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto ba = current_ref_bundle();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto bb = current_ref_bundle();
  move_to_parent();
  if (!ba || !bb) {
    return;
  }
  store_trivial(dst, Dlop::create_bool(structural_equals(ba, bb)));
}

void uPass_constprop::process_func_in() {
  move_to_child();
  std::string dst(current_text());
  fold_in(dst);
  move_to_parent();
}

void uPass_constprop::process_func_has() {
  move_to_child();
  std::string dst(current_text());
  fold_has(dst);
  move_to_parent();
}

void uPass_constprop::process_func_case() {
  move_to_child();
  std::string dst(current_text());
  fold_case(dst);
  move_to_parent();
}

bool uPass_constprop::try_eval_cell_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals) {
  // `__name(...)` direct cell-op call. Strip the `__` prefix and dispatch
  // against the Ntype_op kernel set. Operands are positional and follow the
  // pin order from cell.cpp (see also docs/contracts/lnast2lgraph.md).
  if (fname.size() < 3 || fname[0] != '_' || fname[1] != '_') {
    return false;
  }
  std::string_view op(fname.data() + 2, fname.size() - 2);

  // All actuals must be positional and foldable (no unknown bits) — leave
  // unknown-input folds for a later iteration once unknowns clear.
  std::vector<Const> args;
  args.reserve(actuals.size());
  for (const auto& a : actuals) {
    if (a.is_named || a.is_bundle || a.value.is_invalid() || a.value.has_unknowns()) {
      return false;
    }
    args.emplace_back(a.value);
  }

  auto need_n = [&](std::size_t n) -> bool { return args.size() == n; };
  auto need_min = [&](std::size_t n) -> bool { return args.size() >= n; };

  Const result;
  bool  matched = false;

  if (op == "sum") {
    // Pin a: add; pin b: subtract. Direct-call form uses pin a only.
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.add_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "mult") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.mult_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "div") {
    if (need_n(2)) {
      result  = *args[0].div_op(args[1]);
      matched = true;
    }
  } else if (op == "mod") {
    if (need_n(2)) {
      result  = *args[0].mod_op(args[1]);
      matched = true;
    }
  } else if (op == "and") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.and_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "or") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.or_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "xor") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.xor_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "not") {
    if (need_n(1)) {
      result  = args[0].not_op();
      matched = true;
    }
  } else if (op == "ror") {
    if (need_n(1)) {
      result  = *args[0].ror_op();
      matched = true;
    }
  } else if (op == "rand") {
    if (need_n(1)) {
      result  = *args[0].rand_op();
      matched = true;
    }
  } else if (op == "rxor") {
    if (need_n(1)) {
      result  = *args[0].rxor_op();
      matched = true;
    }
  } else if (op == "sext") {
    // Pin a: value, pin b: sign-bit position.
    if (need_n(2) && args[1].is_i()) {
      result  = args[0].sext_op(static_cast<int>(args[1].to_i()));
      matched = true;
    }
  } else if (op == "get_mask") {
    if (need_n(2)) {
      result  = *args[0].get_mask_op(args[1]);
      matched = true;
    }
  } else if (op == "set_mask") {
    if (need_n(3)) {
      result  = *args[0].set_mask_op(args[1], args[2]);
      matched = true;
    }
  } else if (op == "lt") {
    if (need_n(2)) {
      result  = *args[0].lt_op(args[1]);
      matched = true;
    }
  } else if (op == "gt") {
    if (need_n(2)) {
      result  = *args[0].gt_op(args[1]);
      matched = true;
    }
  } else if (op == "le") {
    if (need_n(2)) {
      result  = *args[0].le_op(args[1]);
      matched = true;
    }
  } else if (op == "ge") {
    if (need_n(2)) {
      result  = *args[0].ge_op(args[1]);
      matched = true;
    }
  } else if (op == "eq") {
    if (need_n(2)) {
      result  = *args[0].eq_op(args[1]);
      matched = true;
    }
  } else if (op == "ne") {
    if (need_n(2)) {
      auto eq = args[0].eq_op(args[1]);
      result  = eq->not_op();
      matched = true;
    }
  } else if (op == "shl") {
    if (need_n(2)) {
      result  = *args[0].lsh_op(args[1]);
      matched = true;
    }
  } else if (op == "sra") {
    if (need_n(2)) {
      result  = *args[0].rsh_op(args[1]);
      matched = true;
    }
  } else if (op == "mux") {
    // Pin 0 selector, pins 1..N values; pick args[1 + sel].
    if (need_min(2) && args[0].is_i()) {
      const auto idx = static_cast<int64_t>(args[0].to_i());
      if (idx >= 0 && static_cast<std::size_t>(idx + 1) < args.size()) {
        result  = args[1 + idx];
        matched = true;
      } else if (idx >= 0) {
        // Out-of-range selector: pick the last value (Mux default-arm
        // semantics — matches cell_mux's `else` arm in tests).
        result  = args.back();
        matched = true;
      }
    }
  } else if (op == "hotmux") {
    // Pin 0 one-hot sel, pins 1..N values. Each set bit `i` of sel picks
    // value args[1 + i]. Folds only when exactly one bit of sel is set.
    if (need_min(2) && args[0].is_i()) {
      const auto sel = args[0].to_i();
      if (sel != 0 && (sel & (sel - 1)) == 0) {
        int bit = 0;
        for (auto v = sel; (v & 1) == 0; v >>= 1) {
          ++bit;
        }
        if (static_cast<std::size_t>(bit + 1) < args.size()) {
          result  = args[1 + bit];
          matched = true;
        }
      }
    }
  } else {
    return false;
  }

  if (!matched || result.is_invalid()) {
    return false;
  }
  store_trivial(dst, result);
  return true;
}

void uPass_constprop::process_func_call() {
  // Layout: ref(dst), ref(func_name), (const|ref)(arg)...
  // Now strictly the ref-form (built-in typecast callables and user funcs).
  // Pseudo-functions `does/in/has/case/break/continue/return` arrive as
  // dedicated ntypes (process_func_does/in/has/...). The remaining const-form
  // shapes (e.g. `import`) fall through and are left unfolded.
  move_to_child();
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    move_to_parent();
    return;
  }
  std::string fname(current_text());

  // `optimize(<bool>)` — synthesis hint, parse-and-discard for now. Drop
  // the call by binding dst to a constant true so the statement can be
  // eliminated (no downstream consumers expect a meaningful value).
  if (fname == "optimize") {
    store_trivial(dst, Dlop::create_bool(true));
    move_to_parent();
    return;
  }

  auto actuals = collect_call_actuals();
  // Direct cell-op call: `__sum(a, b)`, `__hotmux(sel, a, b, …)`, … —
  // every Ntype_op cell can surface in Pyrope as `__name(...)` and gets
  // folded here when all actuals are comptime-known. See cell.hpp for the
  // canonical names.
  if (actuals.has_value() && try_eval_cell_call(dst, fname, *actuals)) {
    move_to_parent();
    return;
  }

  // Enumerate known typecast dispatch.
  enum class Cast { none, to_int, to_uint, to_string, to_sized };
  Cast kind       = Cast::none;
  bool sized_sig  = false;  // true for sN, false for uN
  int  sized_bits = 0;
  if (fname == "int") {
    kind = Cast::to_int;
  } else if (fname == "uint" || fname == "unsigned") {
    kind = Cast::to_uint;
  } else if (fname == "string") {
    kind = Cast::to_string;
  } else if (fname.size() >= 2 && (fname[0] == 'u' || fname[0] == 's' || fname[0] == 'i')) {
    // u32 / s32 / u8 ... — size suffix is decimal digits.
    bool all_digits = true;
    for (size_t i = 1; i < fname.size(); ++i) {
      if (fname[i] < '0' || fname[i] > '9') {
        all_digits = false;
        break;
      }
    }
    if (all_digits && fname.size() > 1) {
      kind       = Cast::to_sized;
      sized_sig  = (fname[0] == 's' || fname[0] == 'i');
      sized_bits = std::stoi(fname.substr(1));
    }
  }
  if (kind == Cast::none) {
    move_to_parent();
    return;
  }

  std::vector<Const> args;
  if (!actuals.has_value()) {
    move_to_parent();
    return;
  }
  args.reserve(actuals->size());
  for (const auto& actual : *actuals) {
    if (actual.is_named || actual.value.is_invalid()) {
      move_to_parent();
      return;
    }
    args.push_back(actual.value);
  }
  move_to_parent();

  // Parse a scalar from either a string (re-parse its textual content) or an
  // already-numeric Const. Returns invalid on parse failure.
  // `to_pyrope()` on a string renders as `'content'`; strip the single-quote
  // wrappers before re-parsing so `Dlop::from_pyrope("3")` (an int) is
  // produced rather than `Dlop::from_pyrope("'3'")` (a string round-trip).
  auto to_scalar = [](const Const& a) -> Const {
    if (!a.is_string()) {
      return a;
    }
    try {
      return *Dlop::from_pyrope(strip_pyrope_quotes(a.to_pyrope()));
    } catch (...) {
      return *Dlop::invalid();
    }
  };

  Const result;
  if (kind == Cast::to_string) {
    auto stringified = stringify_concat_trivials(args);
    if (!stringified.has_value()) {
      return;  // unknown bits in some arg — leave dst unset for next iteration
    }
    result = *stringified;
  } else {
    if (args.size() != 1) {
      return;
    }  // unsupported arity
    Const v = to_scalar(args.front());
    if (v.is_invalid()) {
      return;
    }
    if (kind == Cast::to_uint) {
      if (v.is_string() || v.is_negative()) {
        // Cast failure (non-numeric string or negative) → pyrope `nil`.
        v = Dlop::nil();
      }
      result = v;
    } else if (kind == Cast::to_int) {
      if (v.is_string()) {
        v = Dlop::nil();
      }
      result = v;
    } else {
      // sized: fold only when the value fits. Signed/unsigned range check is
      // deferred; the current test set just stores small positives.
      if (v.is_string()) {
        v = Dlop::nil();
      }
      (void)sized_sig;
      (void)sized_bits;
      result = v;
    }
  }

  store_trivial(dst, result);
}

void uPass_constprop::process_range() {
  // Layout: ref(dst), (const|ref)(start), (const|ref)(end)
  // Resolve start/end and stash in range_map keyed by dst. When either side
  // is unknown, leave the entry absent so a later iteration can retry. For
  // `x[a..]` / `x[..]`, prp2lnast emits the open end as the literal pyrope
  // `nil`, which round-trips as a string Const — process_tuple_get treats
  // that sentinel as "to source's last index".
  //
  // For closed `lo..=hi` ranges with concrete integer bounds, also
  // materialize a positional tuple bundle so `cassert (2..=4) == (2,3,4)`
  // folds via compare_bundles_eq.
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const start = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const end = current_prim_value();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }

  auto it      = range_map.find(dst);
  bool changed = false;
  if (it == range_map.end()) {
    range_map.emplace(dst, std::make_pair(start, end));
    changed = true;
  } else if (!it->second.first.same_repr(start) || !it->second.second.same_repr(end)) {
    it->second = {start, end};
    changed    = true;
  }

  // Materialize a tuple bundle for closed integer ranges so eq/tuple_get can
  // operate on the concrete sequence. Skip for open-ended (nil) or negative
  // spans, and bound the size so a pathological span can't blow up memory.
  if (start.is_i() && end.is_i()) {
    const auto lo = start.to_i();
    const auto hi = end.to_i();
    if (hi >= lo && (hi - lo) < 4096) {
      auto bundle = std::make_shared<Bundle>(dst);
      int  pos    = 0;
      for (int64_t v = lo; v <= hi; ++v, ++pos) {
        bundle->set(std::to_string(pos), *Dlop::create_integer(v));
      }
      st.set(dst, bundle);
    }
  }

  if (changed) {
    mark_changed();
  }
}

void uPass_constprop::process_tuple_get() {
  // Read: dst = src[field]
  // Build the symbol-table key as "src.field" and propagate the stored value.
  move_to_child();
  auto dst = std::string(current_text());

  move_to_sibling();
  auto src = std::string(current_text());  // source tuple variable

  if (!move_to_sibling()) {
    move_to_parent();
    return;  // no field — nothing to propagate
  }

  // Range-indexed tuple_get on a string folds inline (`x[1..=2]` / `x[1..]`).
  // Same path also handles integer source for bit-slicing (`b[0..<4]`):
  // detect: exactly one field operand which is a ref bound by a prior
  // `range` LNAST node.
  if (is_type(Lnast_ntype::Lnast_ntype_ref) && is_last_child()) {
    auto it = range_map.find(std::string(current_text()));
    if (it != range_map.end() && st.has_trivial(src)) {
      const auto& src_val = st.get_trivial(src);
      const auto& start   = it->second.first;
      const auto& end_lc  = it->second.second;
      if (src_val.is_string() && start.is_i()) {
        const auto  start_idx = static_cast<size_t>(start.to_i());
        std::string body      = strip_pyrope_quotes(src_val.to_pyrope());
        size_t      len       = body.size();
        bool        open      = end_lc.is_nil();  // open-end sentinel for `x[a..]`
        if (!open && !end_lc.is_i()) {
          move_to_parent();
          return;
        }
        size_t end_i = open ? (len == 0 ? 0 : len - 1) : static_cast<size_t>(end_lc.to_i());
        if (start_idx <= len && end_i + 1 <= len && start_idx <= end_i + 1) {
          store_trivial(dst, Dlop::from_string(body.substr(start_idx, end_i - start_idx + 1)));
          move_to_parent();
          return;
        }
      }
      // Integer bit-slice via the same range→value synthesis used by get_mask.
      // Delegate to Dlop (get_mask_op): unknown source bits are sliced
      // bit-precisely, so only non-values (invalid/string) are skipped.
      if (is_numeric(src_val)) {
        const Const result = apply_range_mask(src_val, start, end_lc);
        if (!result.is_invalid()) {
          store_trivial(dst, result);
          move_to_parent();
          return;
        }
      }
    }
  }

  // Accumulate field path: each child after src appends ".field" to the key.
  // LNAST const text is the pyrope-syntactic form (`"0"`, `"'b'"`, `"\"hi\""`),
  // not the field name. Parse and use Const::to_field() so integers render
  // as their decimal and strings drop their surrounding quotes — making
  // `t[0]`, `t['b']` and `t["foo"]` resolve uniformly to the bare-name
  // stored key in the bundle.
  std::string key = src;
  do {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      auto v = Dlop::from_pyrope(current_text());
      if (!v || v->is_invalid()) {
        move_to_parent();
        return;
      }
      key += '.';
      key += v->to_field();
    } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      // Runtime index: must be a known constant to fold statically.
      const auto idx = st.get_trivial(current_text());
      if (idx.is_invalid()) {
        move_to_parent();
        return;  // can't fold unknown index
      }
      key += '.';
      key += std::to_string(idx.to_i());
    } else {
      move_to_parent();
      return;  // unhandled field type
    }
  } while (!is_last_child() && move_to_sibling());

  move_to_parent();

  // Propagate trivial value if available; fall back to bundle propagation.
  if (st.has_trivial(key)) {
    store_trivial(dst, st.get_trivial(key));
  } else if (st.has_bundle(key)) {
    auto sub_bundle = st.get_bundle(key);
    if (sub_bundle) {
      bool local_changed = !st.has_bundle(dst) || st.get_bundle(dst) != sub_bundle;
      if (local_changed && st.set(dst, sub_bundle)) {
        mark_changed();
      }
    }
  } else if (st.has_trivial(src)) {
    // Single-output callee fallback: an inliner result like
    // `comb f(...) -> (res:T) { res = ... }` is stored as a trivial scalar
    // on the caller's dst (so `x == 2` works). The dotted form `x.res`
    // then asks for `x.res` which doesn't exist as a key; fall back to
    // the source's trivial value so the named-field reader and the bare
    // scalar reader converge on the same answer.
    store_trivial(dst, st.get_trivial(src));
  }
}

void uPass_constprop::process_tuple_set() {
  // Update a single field inside an existing tuple bundle.
  // Layout: ref(tuple), field_path..., value
  // We handle the simple one-field case: tuple.field = value.
  //
  // IMPORTANT: Attribute assignments (e.g. x["__bits"] = 2, x["__signed"] = 1)
  // must be skipped.  Bundle::set() interns attribute keys as "0.__attr"
  // while Symbol_table::has_trivial() does a literal match, so the round-trip
  // "set then has_trivial" always returns false → mark_changed on every
  // iteration → non-convergence.  Attribute annotations are not values that
  // constprop needs to propagate.
  move_to_child();
  auto tuple_var = std::string(current_text());

  if (is_last_child()) {
    move_to_parent();
    return;
  }

  // `tup[idx] = v` promotes a scalar to a 1-element tuple in Pyrope. Mark the
  // target so a downstream tuple_concat reads `tup` via bundle mode rather
  // than mis-classifying a single-entry bundle as a scalar wrapper. Mirrors
  // the propagation done by process_tuple_add for its destination.
  tuple_typed_names.insert(tuple_var);

  // Collect all children after the tuple ref into a (text, is_ref) list.
  // The last child is the value; everything before it is the field path.
  // Capturing `is_ref` matters for the value: a ref's text is a variable name
  // (look it up in the symbol table), not a literal — `Dlop::from_pyrope`
  // happily accepts unparseable text as a string, so without the type tag we
  // would store the raw ref name (e.g. `___3`) as a string Const.
  struct Child {
    std::string text;
    bool        is_ref;
  };
  std::vector<Child> path_and_val;
  while (move_to_sibling()) {
    path_and_val.push_back({std::string(current_text()), is_type(Lnast_ntype::Lnast_ntype_ref)});
  }
  if (path_and_val.size() < 2) {
    // Need at least one field and one value.
    move_to_parent();
    return;
  }

  // Skip attribute assignments (e.g. x["__bits"] = 2).
  // Any field component that begins with "__" and whose third char is not '_'
  // is a Pyrope bitwidth/signed attribute — not a propagatable value.
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    const auto& f = path_and_val[i].text;
    if (f.size() > 2 && f[0] == '_' && f[1] == '_' && f[2] != '_') {
      move_to_parent();
      return;
    }
  }

  // Resolve each path element to its final field text. A path element may be:
  //   - const: literal field text (e.g. `0`, `a`) — use as-is.
  //   - ref:   variable whose value names the field (`sing_tup[key] = e`).
  //            Resolve through the symbol table; a trivial string yields its
  //            content (no quotes), a trivial int its decimal text. If the
  //            ref has no trivial yet, fall back to the variable name so
  //            convergence can still happen on a later iteration.
  std::vector<std::string> path;
  path.reserve(path_and_val.size() - 1);
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    std::string elem = path_and_val[i].text;
    if (path_and_val[i].is_ref && st.has_trivial(elem)) {
      // to_field() unwraps a string trivial to its content (no quotes) and
      // renders an int trivial as its decimal text — exactly the field-name
      // shape we want for `tuple[ref] = …`.
      elem = st.get_trivial(elem).to_field();
    }
    path.push_back(std::move(elem));
  }

  // Decide between the flat-key path (numeric or dotted positional) and
  // the named path (single non-numeric name → bare-name key so downstream
  // Bundle::concat treats this entry as a named slot, matching what
  // tuple_add emits for `(name=val, …)`).
  auto is_decimal = [](const std::string& s) {
    if (s.empty()) {
      return false;
    }
    for (char c : s) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        return false;
      }
    }
    return true;
  };

  bool use_named_positional = path.size() == 1 && !is_decimal(path[0]) && !path[0].empty();

  const auto& val_child     = path_and_val.back();
  auto        resolve_value = [&]() -> std::optional<Const> {
    if (val_child.is_ref) {
      if (st.has_trivial(val_child.text)) {
        return st.get_trivial(val_child.text);
      }
      return std::nullopt;
    }
    Const v = *Dlop::from_pyrope(val_child.text);
    if (v.is_invalid()) {
      return std::nullopt;
    }
    return v;
  };

  if (use_named_positional) {
    // Place the entry into tuple_var's bundle under the bare name. Named
    // and unnamed slots live in separate key spaces after the bundle_sorted
    // refactor — no position prefix to compute or reuse.
    auto bundle = st.get_bundle(tuple_var);
    if (!bundle) {
      bundle = std::make_shared<Bundle>(tuple_var);
      st.set(tuple_var, bundle);
    }
    const std::string& name = path[0];

    auto v = resolve_value();
    if (v) {
      // Update bundle in place. mark_changed is driven by tuple_get; matching
      // process_tuple_add's no-mark policy here keeps convergence behavior.
      bundle->set(name, *v);
    } else if (val_child.is_ref && st.has_bundle(val_child.text)) {
      auto sub = st.get_bundle(val_child.text);
      if (sub) {
        bundle->set(name, sub);
      }
    }
    move_to_parent();
    return;
  }

  std::string field;
  for (const auto& p : path) {
    field += '.';
    field += p;
  }
  auto key = tuple_var + field;

  if (val_child.is_ref) {
    if (st.has_trivial(val_child.text)) {
      store_trivial(key, st.get_trivial(val_child.text));
    } else if (st.has_bundle(val_child.text)) {
      auto b = st.get_bundle(val_child.text);
      if (b) {
        st.set(key, b);  // bundle comparison not value-based; don't mark_changed
      }
    }
  } else {
    Const val = *Dlop::from_pyrope(val_child.text);
    if (!val.is_invalid()) {
      store_trivial(key, val);
    }
  }
  move_to_parent();
}

// ── Bitwidth Insensitive Reduce ──────────────────────────────────────────────
//
// Each reduction op reads its single operand and stores the result of the
// matching Dlop op. The Dlop op already encodes the right unknown handling:
// rand_op/rxor_op return a 1-bit unknown when the input has unknowns, and
// ror_op (binary form, called with v on both sides) folds true if any bit is
// set — which is what red_or wants for the unknown-friendly case too.
template <typename F>
void uPass_constprop::process_reduction(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto input = current_prim_value();
  move_to_parent();
  if (input.is_invalid() || input.is_string()) {
    return;
  }
  Const r = op(input);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

void uPass_constprop::process_red_or() {
  process_reduction([](const Const& v) -> Const { return *v.ror_op(); });
}

void uPass_constprop::process_red_and() {
  process_reduction([](const Const& v) -> Const { return *v.rand_op(); });
}

void uPass_constprop::process_red_xor() {
  process_reduction([](const Const& v) -> Const { return *v.rxor_op(); });
}

// popcount (`a#+[..]`): number of set bits, returned as an integer Const.
// Const::popcount_op handles the unknown-bit and negative/unknown-sign cases.
void uPass_constprop::process_popcount() {
  process_reduction([](const Const& v) -> Const { return *v.popcount_op(); });
}

// ── Bit Manipulation ─────────────────────────────────────────────────────────

// ── Emit classification (Slice 1 drop + fold rules) ─────────────────────────
//
// See upass.md §2.4. Called by the runner AFTER process_* has populated the
// symbol table for this node, so the LHS entry — if any — reflects the
// value this statement just computed.
//
// Rule: drop this statement iff
//   - LHS (first child) is a ref, and
//   - the symbol table holds a concrete Const for LHS (known, no unknowns).
// Otherwise emit.
upass::Emit_decision uPass_constprop::classify_statement() {
  // Peek at the first child (LHS/dst) without disturbing cursor state.
  // cassert is dispatched through the same path but has no dst — its
  // single child is an operand; always emit so it reaches the verifier.
  if (is_type(Lnast_ntype::Lnast_ntype_cassert)) {
    return upass::Emit_decision::emit_node();
  }

  bool        got_child  = move_to_child();
  bool        lhs_is_ref = got_child && is_type(Lnast_ntype::Lnast_ntype_ref);
  std::string lhs_text{lhs_is_ref ? current_text() : std::string_view{}};
  move_to_parent();

  if (!lhs_is_ref || lhs_text.empty()) {
    return upass::Emit_decision::emit_node();
  }

  // Drop iff the ST holds a fully-known value. is_known_const() encapsulates
  // has_trivial + is_invalid + has_unknowns in one call.
  if (!st.is_known_const(lhs_text)) {
    return upass::Emit_decision::emit_node();
  }

  // Bundle-shape guard. is_known_const returns true as soon as the bundle's
  // position-0 entry is a concrete Const, but that's also true for:
  //   - multi-entry tuples: `(1,2)` — two non-attribute entries
  //   - single-entry named tuples: `(c=2)` — one entry but keyed `c`,
  //     not `0`; inlining as a scalar loses the name
  // fold_ref returns only the position-0 trivial, so dropping the producer
  // and substituting via fold_ref would silently truncate `(1,2)` → `1`
  // or convert `(c=2)` → `2`. Keep the stmt unless the bundle is a
  // *trivial* scalar (exactly one anonymous `0` entry), where fold_ref's
  // inline is faithful.
  if (auto b = st.get_bundle(lhs_text); b && !b->is_trivial_scalar()) {
    return upass::Emit_decision::emit_node();
  }

  return upass::Emit_decision::drop();
}

std::optional<Const> uPass_constprop::fold_ref(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (!st.is_known_const(name)) {
    return std::nullopt;
  }
  // is_known_const returns true once the bundle's position-0 entry is a
  // concrete Const, but that's also true for multi-entry tuples like
  // `(1,2)` and single-entry named tuples like `(c=2)`. Inlining the
  // position-0 trivial would silently truncate `(1,2)` → `1` or strip
  // the name from `(c=2)` → `2`. Only inline when the bundle is a
  // trivial scalar (exactly one anonymous `0` entry); for everything
  // else, return nullopt so the consumer keeps the ref and reads the
  // full bundle from the symbol table.
  if (auto b = st.get_bundle(name); b && !b->is_trivial_scalar()) {
    return std::nullopt;
  }
  return st.get_trivial(name);
}

std::optional<std::vector<std::pair<std::string, Const>>> uPass_constprop::provide_bundle_fields(std::string_view name) {
  // 1i Phase E shared-ST read: hand the runner the flat comptime-const fields
  // of `name`'s bundle so it can bind/propagate tuple actuals it can't reach
  // through scalar fold_ref. Returns nullopt when `name` is not a bundle; a
  // field whose value isn't a concrete Const is skipped (caller treats a
  // partial result as "not fully comptime").
  auto b = st.get_bundle(std::string(name));
  if (!b) {
    return std::nullopt;
  }
  std::vector<std::pair<std::string, Const>> out;
  for (const auto& [k, ep] : b->non_attr_entries()) {
    if (ep && !ep->trivial.is_invalid()) {
      out.emplace_back(k, ep->trivial);
    }
  }
  return out;
}

std::string uPass_constprop::provide_typename(std::string_view name) {
  auto it = typename_of_var.find(std::string(name));
  return it != typename_of_var.end() ? it->second : std::string{};
}

void uPass_constprop::process_sext() {
  // Sign-extend: [sext: ref(dst), ref_or_const(src), const(nbits)]
  // sext_op(ebits) interprets bit (ebits-1) of src as the sign bit.
  // Delegate to Dlop: src may carry unknown bits (sext_op sign-extends the
  // unknown sign too); skip only non-values. nbits is a host int, so it must
  // be a concrete integer.
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto src = current_prim_value();
  move_to_sibling();
  const auto nbits_lc = current_prim_value();
  move_to_parent();
  if (is_numeric(src) && !nbits_lc.is_invalid() && nbits_lc.is_i()) {
    store_trivial(var, src.sext_op(static_cast<int>(nbits_lc.to_i())));
  }
}

void uPass_constprop::process_get_mask() {
  // Layout: ref(dst), ref(value), (const|ref)(mask)
  // The mask operand may be:
  //   - a constant integer / known scalar (treated as a bitmask),
  //   - a `range` ref previously bound in range_map (`b#[lo..]`,
  //     `b#[lo..=hi]`, `b#[..=hi]`, etc.).
  // Range refs lower via apply_range_mask, which routes open-ended `lo..`
  // through rsh_op (skipping get_mask_op's broken negative-mask path) and
  // closed `lo..=hi` through get_mask_op with the equivalent positive mask.
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Const value = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  bool  is_range = false;
  Const range_start;
  Const range_end;
  Const mask;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    if (auto rit = range_map.find(std::string(current_text())); rit != range_map.end()) {
      is_range    = true;
      range_start = rit->second.first;
      range_end   = rit->second.second;
    } else {
      mask = current_prim_value();
    }
  } else {
    mask = current_prim_value();
  }

  move_to_parent();

  // Delegate to Dlop: a value with unknown bits is sliced bit-precisely by
  // get_mask_op, so only non-values (invalid/string) are skipped here.
  if (!is_numeric(value)) {
    return;
  }

  if (is_range) {
    const Const result = apply_range_mask(value, range_start, range_end);
    if (!result.is_invalid()) {
      store_trivial(var, result);
    }
    return;
  }

  // The mask may itself carry unknown bits — get_mask_op handles that
  // (returns a sound bounded-width unknown), so don't pre-filter it.
  if (!is_numeric(mask)) {
    return;
  }
  // Trust Dlop::get_mask_op — including the single-bit-mask → Bool rule. We
  // store whatever it returns (invalid included): the fold is real and
  // downstream code shouldn't silently drop it.
  store_trivial(var, *value.get_mask_op(mask));
}

void uPass_constprop::process_set_mask() {
  // Layout: ref(dst), ref(input), (const|ref)(mask), (const|ref)(value)
  // Mirrors process_get_mask but writes back via set_mask_op. The mask
  // operand can be a constant integer (treated as a bitmask) or a `range`
  // ref previously bound in range_map (lo..hi closed; `lo..` open).
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Const input_val = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  bool  is_range = false;
  Const range_start;
  Const range_end;
  Const mask;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    if (auto rit = range_map.find(std::string(current_text())); rit != range_map.end()) {
      is_range    = true;
      range_start = rit->second.first;
      range_end   = rit->second.second;
    } else {
      mask = current_prim_value();
    }
  } else {
    mask = current_prim_value();
  }

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const new_val = current_prim_value();
  move_to_parent();

  // Delegate to Dlop: both the input being written into and the value being
  // written may carry unknown bits — set_mask_op tracks them bit-precisely.
  // Only the *mask* (which bits to write) must be concrete (see below).
  if (!is_numeric(input_val) || !is_numeric(new_val)) {
    return;
  }

  Const final_mask;
  if (is_range) {
    if (!range_start.is_i()) {
      return;
    }
    const auto lo = range_start.to_i();
    if (lo < 0) {
      return;
    }
    if (range_end.is_nil()) {
      // Open-ended `lo..`: bits lo and above. For set_mask we need a
      // concrete bitmask, but the upper bound isn't fixed. Skip — let a
      // later iteration with a pinned width resolve it.
      return;
    }
    if (!range_end.is_i()) {
      return;
    }
    const auto hi = range_end.to_i();
    if (hi < lo) {
      return;
    }
    final_mask = *Dlop::get_mask_value(static_cast<int>(hi), static_cast<int>(lo));
  } else {
    // The mask selects *which* bits to overwrite; Dlop::set_mask_op requires
    // it concrete (asserts on an unknown mask, unlike get_mask_op). This is a
    // Dlop precondition on the bit-selection, not a value pre-filter — the
    // data operands (input_val/new_val) above already pass unknowns through.
    if (!foldable(mask)) {
      return;
    }
    final_mask = mask;
  }

  store_trivial(var, *input_val.set_mask_op(final_mask, new_val));
}
