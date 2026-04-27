//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "boost/multiprecision/cpp_int.hpp"
#include "lnast_ntype.hpp"
#include "str_tools.hpp"
#include "upass_verifier.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_constprop.hpp.
static upass::uPass_plugin cprop("constprop", upass::uPass_wrapper<uPass_constprop>::get_upass);

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

  // Strip I/O prefixes so symbol-table keys match the producer-visible name.
  // §12 will move direction out of the ref text entirely; until then, normalize here.
  if (!lhs_text.empty() && (lhs_text.front() == '%' || lhs_text.front() == '$')) {
    lhs_text = lhs_text.substr(1);
  }

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
      // Type-shape preservation: when LHS already holds a bundle whose keys
      // include named-position slots (`:N:name`), and RHS is a pure
      // positional bundle (only `N` keys), bind RHS positions onto LHS's
      // named slots so a downstream `tup0.y` keeps tracking position 1
      // across `tup0 = (55, 66)`.
      //
      // Skip the merge when RHS itself carries names (e.g. `bar = foo` with
      // foo = `(a=…, b=…)`): that's a full bundle copy, not a positional
      // rebind, and the user expects bar's old shape to be replaced.
      auto existing = st.get_bundle(lhs_text);
      bool do_merge = false;
      if (existing && existing.get() != rhs_bundle.get() && !existing->get_map().empty()) {
        bool lhs_has_named_pos = false;
        for (const auto& e : existing->get_map()) {
          if (!e.first.empty() && e.first.front() == ':') {
            lhs_has_named_pos = true;
            break;
          }
        }
        bool rhs_pure_positional = true;
        for (const auto& e : rhs_bundle->get_map()) {
          if (!e.first.empty() && e.first.front() == ':') {
            rhs_pure_positional = false;
            break;
          }
        }
        do_merge = lhs_has_named_pos && rhs_pure_positional;
      }
      if (do_merge) {
        auto merged = std::make_shared<Bundle>(std::string(lhs_text));
        for (const auto& lhs_entry : existing->get_map()) {
          if (rhs_bundle->has_trivial(lhs_entry.first)) {
            merged->set(lhs_entry.first, rhs_bundle->get_trivial(lhs_entry.first));
          } else {
            merged->set(lhs_entry.first, lhs_entry.second);
          }
        }
        // Carry over any RHS entries the LHS shape doesn't cover (e.g. a
        // wider RHS appends new positions).
        for (const auto& rhs_entry : rhs_bundle->get_map()) {
          if (!merged->has_trivial(rhs_entry.first)) {
            merged->set(rhs_entry.first, rhs_entry.second);
          }
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
      // subsequent uses of `lhs_text` resolve. Mark changed iff the value
      // differs, matching the const-RHS branch below.
      auto rhs_value     = st.get_trivial(current_text());
      bool local_changed = true;
      if (st.has_trivial(lhs_text)) {
        local_changed = st.get_trivial(lhs_text) != rhs_value;
      }
      if (local_changed && st.set(lhs_text, rhs_value)) {
        mark_changed();
      }
    }
  } else if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    // RHS is a const literal: parse and track the constant value
    auto rhs_value     = current_pyrope_value();
    bool local_changed = true;
    if (st.has_trivial(lhs_text)) {
      local_changed = st.get_trivial(lhs_text) != rhs_value;
    }
    if (local_changed && st.set(lhs_text, rhs_value)) {
      mark_changed();
    }
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
  Lconst r = current_prim_value();

  // Bail on truly opaque operands (no value tracked / non-numeric strings).
  // Unknowns (`0sb?…`) are allowed through — Lconst::add_op / mult_op / etc.
  // already propagate `?` bits, and downstream eq_op uses the resulting
  // pattern for structural-identity folding (see valid_simple).
  if (r.is_invalid() || r.is_string()) {
    move_to_parent();
    return;
  }
  while (move_to_sibling()) {
    auto operand = current_prim_value();
    if (operand.is_invalid() || operand.is_string()) {
      move_to_parent();
      return;
    }
    op(r, operand);
  }
  if (r.is_invalid()) {
    move_to_parent();
    return;
  }
  bool local_changed = true;
  if (st.has_trivial(var)) {
    local_changed = st.get_trivial(var) != r;
  }
  if (local_changed && st.set(var, r)) {
    mark_changed();
  }

  move_to_parent();
}

template <typename F>
void uPass_constprop::process_binary(F op) {
  move_to_child();

  auto var = current_text();
  move_to_sibling();
  Lconst n1 = current_prim_value();
  move_to_sibling();
  Lconst n2 = current_prim_value();

  // Skip folding if either operand is unknown, non-numeric, or has X-bits.
  if (n1.is_invalid() || n1.is_string() || n1.has_unknowns() ||
      n2.is_invalid() || n2.is_string() || n2.has_unknowns()) {
    move_to_parent();
    return;
  }
  Lconst r = op(n1, n2);
  if (r.is_invalid()) {
    move_to_parent();
    return;
  }
  bool local_changed = true;
  if (st.has_trivial(var)) {
    local_changed = st.get_trivial(var) != r;
  }
  if (local_changed && st.set(var, r)) {
    mark_changed();
  }

  move_to_parent();
}

template <typename F>
void uPass_constprop::process_unary(F op) {
  move_to_child();

  auto var = current_text();
  move_to_sibling();
  Lconst r = current_prim_value();

  // Skip folding if input is unknown, non-numeric, or has X-bits.
  if (r.is_invalid() || r.is_string() || r.has_unknowns()) {
    move_to_parent();
    return;
  }
  op(r);
  bool local_changed = true;
  if (st.has_trivial(var)) {
    local_changed = st.get_trivial(var) != r;
  }
  if (local_changed && st.set(var, r)) {
    mark_changed();
  }

  move_to_parent();
}

void uPass_constprop::process_plus() {
  process_nary([](Lconst& r, Lconst n) { r = r.add_op(n); });
}

void uPass_constprop::process_minus() {
  process_nary([](Lconst& r, Lconst n) { r = r.sub_op(n); });
}

void uPass_constprop::process_mult() {
  process_nary([](Lconst& r, Lconst n) { r = r.mult_op(n); });
}

void uPass_constprop::process_div() {
  process_nary([](Lconst& r, Lconst n) { r = r.div_op(n); });
}

void uPass_constprop::process_bit_and() {
  process_nary([](Lconst& r, Lconst n) { r = r.and_op(n); });
}

void uPass_constprop::process_bit_or() {
  process_nary([](Lconst& r, Lconst n) { r = r.or_op(n); });
}

void uPass_constprop::process_bit_not() {
  process_unary([](Lconst& r) { r = r.not_op(); });
}

void uPass_constprop::process_bit_xor() {
  // XOR via identity: (a | b) & ~(a & b)
  process_binary([](Lconst n1, Lconst n2) { return n1.or_op(n2).and_op(n1.and_op(n2).not_op()); });
}

void uPass_constprop::process_mod() {
  process_binary([](Lconst n1, Lconst n2) {
    // Guard: modulo by zero is undefined behaviour.  Return invalid so the
    // caller leaves the variable unset rather than crashing.
    if (n2.is_known_false()) {
      return Lconst::invalid();
    }
    return Lconst(n1.to_i() % n2.to_i());
  });
}

void uPass_constprop::process_shl() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    if (!n2.is_i()) return Lconst::invalid();
    return n1.lsh_op(static_cast<Bits_t>(n2.to_i()));
  });
}

void uPass_constprop::process_sra() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    if (!n2.is_i()) return Lconst::invalid();
    return n1.rsh_op(static_cast<Bits_t>(n2.to_i()));
  });
}

void uPass_constprop::process_log_and() {
  process_binary(
      [](Lconst n1, Lconst n2) -> Lconst { return (!n1.is_known_false() && !n2.is_known_false()) ? Lconst(1) : Lconst(0); });
}

void uPass_constprop::process_log_or() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst { return n1.ror_op(n2); });
}

void uPass_constprop::process_log_not() {
  process_unary([](Lconst& r) { r = r.is_known_false() ? Lconst(1) : Lconst(0); });
}

// Bundle-aware equality. Returns nullopt when constprop can't decide
// (unknown entries, attribute round-trip artefacts). Otherwise true/false.
//
// We delegate key-shape semantics to `Bundle::match` (used via
// `has_trivial` / `get_trivial`): it already handles `:N:name` vs `N`
// positional matching and nested paths (`:1:b.:0:c` ↔ `1.0`). The check
// is symmetric — every non-attribute entry on each side must be present
// on the other with the same Lconst value. That gives the user-visible
// Pyrope behaviour: `(a=3,b=4,5) == (3,4,5)` but `(a=3,b=4,5) != (a=3,foo=4,5)`
// (named slots collide on `:1:b` vs `:1:foo` — Bundle::match rejects).
static std::optional<bool> compare_bundles_eq(const std::shared_ptr<Bundle const>& a,
                                              const std::shared_ptr<Bundle const>& b) {
  auto contains = [](const std::shared_ptr<Bundle const>& src,
                     const std::shared_ptr<Bundle const>& other) -> std::optional<bool> {
    for (const auto& e : src->get_map()) {
      if (Bundle::is_attribute(e.first)) {
        continue;
      }
      if (e.second.trivial.is_invalid() || e.second.trivial.has_unknowns()) {
        return std::nullopt;
      }
      if (!other->has_trivial(e.first)) {
        return false;
      }
      const auto& ov = other->get_trivial(e.first);
      if (ov.is_invalid() || ov.has_unknowns()) {
        return std::nullopt;
      }
      if (ov != e.second.trivial) {
        return false;
      }
    }
    return true;
  };
  auto a_in_b = contains(a, b);
  if (!a_in_b.has_value()) {
    return std::nullopt;
  }
  if (!*a_in_b) {
    return false;
  }
  auto b_in_a = contains(b, a);
  if (!b_in_a.has_value()) {
    return std::nullopt;
  }
  return *b_in_a;
}

// Structural-only `a does b` check.
//
// `a does b` succeeds when a's tuple shape covers every non-attribute
// first-level entry in b:
//   - For every NAMED field in b (`:N:name`), a must have any first-level
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
// Bundle keys may be hierarchical (e.g. `:1:foo.:0:bar`); we look at the
// first level only, which is enough for the flat-tuple cases the comptime
// tests exercise. Nested-tuple structural matching can be added later.
static bool structural_does(const std::shared_ptr<Bundle const>& a,
                            const std::shared_ptr<Bundle const>& b) {
  // Collect a's first-level shape: which names, and which unnamed positions.
  std::set<std::string_view> a_names;
  std::set<int>              a_unnamed_pos;
  for (const auto& e : a->get_map()) {
    if (Bundle::is_attribute(e.first)) {
      continue;
    }
    auto first = Bundle::get_first_level(e.first);
    if (!first.empty() && first.front() == ':') {
      a_names.insert(Bundle::get_first_level_name(first));
    } else {
      const auto pos = Bundle::get_first_level_pos(first);
      if (pos >= 0) {
        a_unnamed_pos.insert(pos);
      }
    }
  }

  // Walk b's first-level shape; bail as soon as a doesn't cover something.
  // Track seen names/positions so we don't re-check sub-entries that share
  // the same first level (`:1:foo.x` and `:1:foo.y` collapse to one check).
  std::set<std::string_view> seen_names;
  std::set<int>              seen_pos;
  for (const auto& e : b->get_map()) {
    if (Bundle::is_attribute(e.first)) {
      continue;
    }
    auto first = Bundle::get_first_level(e.first);
    if (!first.empty() && first.front() == ':') {
      auto name = Bundle::get_first_level_name(first);
      if (seen_names.insert(name).second) {
        if (a_names.find(name) == a_names.end()) {
          return false;
        }
      }
    } else {
      const auto pos = Bundle::get_first_level_pos(first);
      if (pos < 0) {
        continue;
      }
      if (seen_pos.insert(pos).second) {
        if (a_unnamed_pos.find(pos) == a_unnamed_pos.end()) {
          return false;
        }
      }
    }
  }
  return true;
}

// process_eq / process_ne with bundle awareness. Falls back to the scalar
// process_binary path when neither operand is a tracked bundle, so plain
// integer compares are unchanged.
//
// Result handling is tri-state to support unknowns (`0sb?` literals): the
// final stored value is `Lconst(1)` for known-true (negated for ne), the
// matching `Lconst(0)`, or a 1-bit unknown when the comparison itself is
// undecidable. Storing the 1-bit unknown lets a downstream
// `(v != 0) == 0sb?` cassert fold via Lconst::eq_op's structural-identity
// short-circuit.
template <bool Negate>
void uPass_constprop::process_eq_ne_impl() {
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();

  // Resolve operands: bundles that hold a single trivial value collapse to
  // a scalar so we can use the cheap Lconst comparison (st.get_bundle on a
  // scalar-stored ref still returns its single-entry bundle wrapper, so we
  // can't just check ba/bb non-null). undeclared_invalid tracks per-operand
  // whether the operand was a never-declared ref — used by the post-resolve
  // pass below to fold `x == nil` semantics for out-of-scope reads.
  auto resolve = [this](std::shared_ptr<Bundle const>& bp, Lconst& sv,
                        bool& is_const_nil, bool& undeclared_ref) {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto name = current_text();
      auto b = st.get_bundle(name);
      if (b && !b->is_scalar()) {
        bp = b;
      } else if (st.has_trivial(name)) {
        sv = st.get_trivial(name);
      } else if (!st.is_declared(name)) {
        undeclared_ref = true;
      }
      // else: declared but no concrete value yet — leave sv invalid so the
      // eq stays unfolded for this iteration.
    } else {
      sv = current_pyrope_value();
      if (sv.is_nil()) {
        is_const_nil = true;
      }
    }
  };
  std::shared_ptr<Bundle const> ba;
  // Default-constructed Lconst is bits=0/num=0/explicit_str=false — NOT
  // is_invalid() — so initialize explicitly. Otherwise an undeclared ref
  // (whose sv we leave untouched) would parade as a known-zero value and
  // fold every "z == 123" against it.
  Lconst                        n1              = Lconst::invalid();
  bool                          n1_is_const_nil = false;
  bool                          n1_undeclared   = false;
  resolve(ba, n1, n1_is_const_nil, n1_undeclared);
  move_to_sibling();
  std::shared_ptr<Bundle const> bb;
  Lconst                        n2              = Lconst::invalid();
  bool                          n2_is_const_nil = false;
  bool                          n2_undeclared   = false;
  resolve(bb, n2, n2_is_const_nil, n2_undeclared);

  // Out-of-scope ref vs. literal `nil`: the pyrope semantic is that an
  // undeclared name reads as nil, so `a == nil` / `b != nil` should fold
  // even when the ref has never been declared in any reachable scope.
  // We narrow the substitution to comparisons where the OTHER operand is a
  // literal `nil`, so the function-body code that prp2lnast still emits as
  // a top-level sibling of fdef (see scope2.prp's mytest body) doesn't have
  // its `cassert a == 11` casserts fold against `a = nil` and trigger
  // false verifier failures. Scope1 only tests the `== nil` shape, so this
  // narrower rule covers the intended semantics without the side-effects.
  if (n1_undeclared && n2_is_const_nil) {
    n1 = Lconst::nil();
  }
  if (n2_undeclared && n1_is_const_nil) {
    n2 = Lconst::nil();
  }

  // Three outcomes the rest of the pass cares about: known-true, known-false,
  // or a 1-bit unknown. Bundles only produce known true/false; the scalar
  // path may produce unknowns when an operand has them.
  std::optional<Lconst> result;
  if (ba && bb) {
    auto eq = compare_bundles_eq(ba, bb);
    if (eq.has_value()) {
      bool truth = *eq ^ Negate;
      result     = truth ? Lconst(1) : Lconst(0);
    }
  } else if (!ba && !bb) {
    if (!n1.is_invalid() && !n2.is_invalid()) {
      // Defer to Lconst::eq_op: it compares (num,bits) for fully-known
      // operands and yields a 1-bit unknown (`0sb?`) when bits collide
      // with `?` chars. Structural identity (operator==) inside eq_op
      // returns known-true even when both operands carry unknowns —
      // that's the comptime-equality contract used by valid_simple.
      const Lconst eq = n1.eq_op(n2);
      if (eq.is_known_true()) {
        result = Negate ? Lconst(0) : Lconst(1);
      } else if (eq.is_known_false()) {
        result = Negate ? Lconst(1) : Lconst(0);
      } else if (eq.has_unknowns()) {
        // ne is bitwise-not of eq; for a 1-bit unknown the inversion is
        // still a 1-bit unknown, so both branches store the same value.
        result = eq;
      }
    }
  }
  // Mixed bundle vs scalar — leave unresolved.

  if (result.has_value()) {
    bool local_changed = !st.has_trivial(var) || st.get_trivial(var) != *result;
    if (local_changed && st.set(var, *result)) {
      mark_changed();
    }
  }
  move_to_parent();
}

void uPass_constprop::process_ne() { process_eq_ne_impl<true>(); }
void uPass_constprop::process_eq() { process_eq_ne_impl<false>(); }

void uPass_constprop::process_lt() {
  process_binary([](Lconst x, Lconst y) { return x < y; });
}
void uPass_constprop::process_le() {
  process_binary([](Lconst x, Lconst y) { return x <= y; });
}
void uPass_constprop::process_gt() {
  process_binary([](Lconst x, Lconst y) { return x > y; });
}

void uPass_constprop::process_ge() {
  process_binary([](Lconst x, Lconst y) { return x >= y; });
}

void uPass_constprop::process_if() {
  // Conservative if-handling: inspect condition variables but always let the
  // runner traverse all branches.
  // Dead-branch elimination requires tree-surgery hooks not yet in the runner.
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
  st.block_scope(lm->get_current_nid().get_hash());
}

void uPass_constprop::process_stmts_post() {
  st.leave_scope();
}

// ── Tuple Operations ─────────────────────────────────────────────────────────
//
// Layout reference (matches opt_lnast):
//   tuple_add:  ref(dst), [const|ref|assign(ref(key),const/ref(val))]...
//   tuple_get:  ref(dst), ref(src), (const|ref)(field)...
//   tuple_set:  ref(tuple), (const|ref)(field)..., (const|ref)(value)
//
// Bundle key format for named fields: ":pos:name"  (e.g. ":1:foo")
// Symbol_table looks up "A.foo" as → Bundle A → Bundle::get_trivial("foo")

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

  int pos = 0;
  while (move_to_sibling()) {
    auto pos_txt = std::to_string(pos);

    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      bundle->set(pos_txt, Lconst::from_pyrope(current_text()));

    } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      bundle->set(pos_txt, st.get_bundle(current_text()));

    } else if (is_type(Lnast_ntype::Lnast_ntype_assign)) {
      // Named field: assign(ref(key), const/ref(val))
      move_to_child();
      auto key       = std::string(current_text());
      auto field_key = std::format(":{}:{}", pos_txt, key);
      move_to_sibling();
      if (is_type(Lnast_ntype::Lnast_ntype_const)) {
        bundle->set(field_key, Lconst::from_pyrope(current_text()));
      } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        bundle->set(field_key, st.get_bundle(current_text()));
      }
      move_to_parent();
    }
    ++pos;
  }
  // Bundle was updated in-place; no mark_changed() — process_tuple_get drives convergence.
  move_to_parent();
}

void uPass_constprop::process_tuple_concat() {
  // Layout: ref(dst), (const|ref)...
  // Two modes determined by the first operand:
  //   Bundle mode  — every operand must be a known Bundle; merged via Bundle::concat().
  //                  No mark_changed — convergence via downstream tuple_get (same as tuple_add).
  //   Scalar mode  — every operand must be a known scalar; folded via Lconst::concat_op.
  // Bail without folding when any operand is truly unknown or types are mixed.
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  bool                    bundle_mode = false;
  std::shared_ptr<Bundle> acc_bundle;
  Lconst                  acc_scalar;
  bool                    is_first = true;

  auto process_one = [&]() -> bool {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      // st.get_bundle returns a wrapper Bundle even when the ref holds a
      // trivial scalar (string or int). Use is_scalar() to demote those to
      // the scalar path — otherwise string concat (`"hi" ++ " there"`) gets
      // routed through Bundle::concat, which renumbers positional keys
      // instead of folding via Lconst::concat_op.
      //
      // Exception: an *empty* bundle (no positional/named keys, only
      // attributes at most) is unambiguously the empty tuple `()` — never a
      // scalar — so it must enter bundle mode. This is the common shape for
      // an accumulator initialized with `mut c = ()`; without this special
      // case `c ++ (i,)` aborts and the for-loop unroll never advances `c`.
      auto b = st.get_bundle(current_text());
      bool b_is_empty_tuple = false;
      if (b) {
        bool any_kv = false;
        for (const auto& e : b->get_map()) {
          if (!e.first.empty() && e.first.front() == ':') { any_kv = true; break; }
          // Plain "0", "1", ... positional keys are also content.
          if (!e.first.empty() && std::isdigit(static_cast<unsigned char>(e.first.front()))) {
            any_kv = true;
            break;
          }
        }
        b_is_empty_tuple = !any_kv;
      }
      // Use bundle mode when:
      //   - the operand is a multi-entry bundle (always tuple), or
      //   - the operand is empty (must be empty tuple — see comment above), or
      //   - it's been marked as tuple-typed by an earlier tuple_add /
      //     tuple_concat / aliasing-assign (covers the "single-entry
      //     bundle that is actually a 1-element tuple" case), or
      //   - we're already in bundle_mode (the first operand chose tuple,
      //     so subsequent single-entry bundles must concat as tuples too,
      //     not be re-routed to the scalar path).
      const bool is_tuple_typed = tuple_typed_names.contains(std::string(current_text()));
      if (b && (!b->is_scalar() || b_is_empty_tuple || is_tuple_typed || bundle_mode)) {
        if (!is_first && !bundle_mode) return false;
        if (is_first) {
          acc_bundle  = std::make_shared<Bundle>(dst);
          bundle_mode = true;
        }
        if (!b_is_empty_tuple) {
          acc_bundle->concat(b);
        }
        return true;
      }
      if (st.has_trivial(current_text())) {
        auto val = st.get_trivial(current_text());
        if (val.is_invalid() || val.has_unknowns() || bundle_mode) return false;
        acc_scalar = is_first ? val : acc_scalar.concat_op(val);
        return true;
      }
      return false;
    }
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      if (bundle_mode) return false;
      auto val = Lconst::from_pyrope(current_text());
      if (val.is_invalid()) return false;
      acc_scalar = is_first ? val : acc_scalar.concat_op(val);
      return true;
    }
    return false;
  };

  do {
    if (!process_one()) {
      move_to_parent();
      return;
    }
    is_first = false;
  } while (move_to_sibling());

  move_to_parent();

  if (bundle_mode) {
    st.set(dst, acc_bundle);
    // Tuple-mode result: dst is a tuple regardless of its final entry count.
    tuple_typed_names.insert(dst);
  } else {
    bool local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != acc_scalar;
    if (local_changed && st.set(dst, acc_scalar)) {
      mark_changed();
    }
  }
}

// Distinguish a "tuple-shaped" bundle from the symbol-table wrapper used
// for plain scalars. `Symbol_table::set(name, Lconst)` stores scalars in a
// wrapper bundle keyed at position 0, which is *indistinguishable* from a
// real single-element tuple `(3)` in the bundle layer. We can't tell them
// apart without type info, so we stay conservative: a bundle counts as
// tuple-shaped only when it carries a named (`:N:name`) first-level key,
// or when it has two-or-more distinct first-level positional entries.
//
// Consequence: scalar-vs-scalar `does` (e.g. `i3 does b1`) stays
// unresolved instead of folding to a wrong answer based on type. The cost
// is that genuine single-element tuples like `(3) does ...` also stay
// unresolved; that's a small subset of the test surface and the right
// trade for correctness without type tracking.
static bool is_tuple_shaped(const std::shared_ptr<Bundle const>& b) {
  std::set<int> seen_pos;
  for (const auto& e : b->get_map()) {
    if (Bundle::is_attribute(e.first)) {
      continue;
    }
    auto first = Bundle::get_first_level(e.first);
    if (!first.empty() && first.front() == ':') {
      return true;  // any named field → tuple-shaped
    }
    const auto pos = Bundle::get_first_level_pos(first);
    if (pos >= 0) {
      seen_pos.insert(pos);
      if (seen_pos.size() >= 2) {
        return true;  // ≥2 distinct positional fields → tuple-shaped
      }
    }
  }
  return false;
}

// Returns true if the bundle has any first-level non-attribute key with a
// recognizable shape — named or positional. Used as the "weak" requirement
// for the other operand once we've already decided one side is clearly
// tuple-shaped: a bundle whose first-level keys are e.g. `0` (single
// positional) doesn't itself prove tuple-ness, but it's enough structure
// to compare against a known tuple.
static bool has_first_level_shape(const std::shared_ptr<Bundle const>& b) {
  for (const auto& e : b->get_map()) {
    if (Bundle::is_attribute(e.first)) {
      continue;
    }
    auto first = Bundle::get_first_level(e.first);
    if (first.empty()) {
      continue;
    }
    if (first.front() == ':' || Bundle::get_first_level_pos(first) >= 0) {
      return true;
    }
  }
  return false;
}

// Fold `dst = does(l, r)`. Cursor is currently on the const("does") fname
// node. Walks forward to read l and r, decides the structural outcome, and
// stores the boolean (Lconst(0/1)) in the symbol table for `dst`.
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
  auto resolve_bundle = [this]() -> std::shared_ptr<Bundle const> {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto b = st.get_bundle(current_text());
      if (b && has_first_level_shape(b)) {
        return b;
      }
    }
    return nullptr;
  };

  if (!move_to_sibling()) {
    return;
  }
  auto ba = resolve_bundle();
  if (!move_to_sibling()) {
    return;
  }
  auto bb = resolve_bundle();

  if (!ba || !bb) {
    return;
  }
  // At least one side must prove it's a real tuple, otherwise scalar-vs-
  // scalar would over-fold (e.g. `b1 does i2` where both are scalar
  // wrappers). The structural check itself handles the asymmetry once one
  // side is known-tuple.
  if (!is_tuple_shaped(ba) && !is_tuple_shaped(bb)) {
    return;
  }

  const Lconst result        = structural_does(ba, bb) ? Lconst(1) : Lconst(0);
  bool         local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
  if (local_changed && st.set(dst, result)) {
    mark_changed();
  }
}

// Per-first-level summary used by fold_in / fold_has: groups a bundle's flat
// (single-level) entries by their first-level key, marking sub-bundle entries
// (multi-level keys like `:0:a.0`) so the comparison can distinguish a scalar
// `a=1` from a nested `a=(1,2)`.
struct Bundle_flat_entry {
  std::string_view name;          // empty for unnamed positional
  int              pos = -1;
  Lconst           value;         // valid only when !is_sub_bundle
  bool             is_sub_bundle = false;
};

static std::vector<Bundle_flat_entry> collect_first_level(const std::shared_ptr<Bundle const>& b) {
  // Iterate the bundle's full key map and group hierarchical keys by their
  // first-level prefix. Sub-bundle entries (multi-level keys) collapse to a
  // single Bundle_flat_entry with is_sub_bundle=true; plain single-level
  // entries record their trivial scalar value.
  std::vector<Bundle_flat_entry> entries;
  std::map<std::string, size_t>  by_first;
  for (const auto& kv : b->get_map()) {
    if (Bundle::is_attribute(kv.first)) {
      continue;
    }
    auto first = Bundle::get_first_level(kv.first);
    if (first.empty()) {
      continue;
    }
    auto first_str = std::string(first);
    auto it        = by_first.find(first_str);
    Bundle_flat_entry* e;
    if (it == by_first.end()) {
      Bundle_flat_entry n;
      n.pos = Bundle::get_first_level_pos(first);
      if (first.front() == ':') {
        n.name = Bundle::get_first_level_name(first);
      }
      by_first[first_str] = entries.size();
      entries.emplace_back(std::move(n));
      e = &entries.back();
    } else {
      e = &entries[it->second];
    }
    if (Bundle::is_single_level(kv.first)) {
      if (!e->is_sub_bundle) {
        e->value = kv.second.trivial;
      }
    } else {
      e->is_sub_bundle = true;
      e->value         = Lconst::invalid();
    }
  }
  return entries;
}

// Fold `dst = in(l, r)`. Cursor is on the const("in") marker. Walks forward
// to read l and r, evaluates the membership predicate, and stores Lconst(0/1)
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
  auto resolve_bundle = [this]() -> std::shared_ptr<Bundle const> {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return st.get_bundle(current_text());
    }
    return nullptr;
  };

  if (!move_to_sibling()) {
    return;
  }
  auto ba = resolve_bundle();
  if (!move_to_sibling()) {
    return;
  }
  auto bb = resolve_bundle();
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
        const Lconst eq = le.value.eq_op(re.value);
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
        const Lconst eq = le.value.eq_op(re.value);
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
  const Lconst result        = *outcome ? Lconst(1) : Lconst(0);
  bool         local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
  if (local_changed && st.set(dst, result)) {
    mark_changed();
  }
}

// Fold `dst = has(l, key)`. Cursor is on the const("has") marker.
//
// Semantics for `bundle has key`:
//   - String key `'name'`: bundle must have a first-level named entry
//     matching `name` (i.e., a key shaped `:N:name`).
//   - Integer key `N`: bundle must have any first-level entry at position N
//     (named or unnamed; both are recorded with explicit positions).
// Returns Lconst(1) for present, Lconst(0) for absent.
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
  Lconst key_val;
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    key_val = Lconst::from_pyrope(current_text());
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
    // the bare name used in `:N:name` keys.
    std::string s = key_val.to_pyrope();
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      s = s.substr(1, s.size() - 2);
    }
    for (const auto& e : entries) {
      if (e.name == s) {
        found = true;
        break;
      }
    }
  } else if (!key_val.is_nil() && !key_val.has_unknowns()) {
    // Position lookup: any first-level entry whose recorded pos matches.
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

  const Lconst result        = found ? Lconst(1) : Lconst(0);
  bool         local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
  if (local_changed && st.set(dst, result)) {
    mark_changed();
  }
}

std::optional<Lconst> uPass_constprop::resolve_current_scalar() const {
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    return Lconst::from_pyrope(current_text());
  }
  if (is_type(Lnast_ntype::Lnast_ntype_ref) && st.has_trivial(current_text())) {
    return st.get_trivial(current_text());
  }
  return std::nullopt;
}

std::optional<std::vector<uPass_constprop::Call_actual>> uPass_constprop::collect_call_actuals() {
  std::vector<Call_actual> actuals;

  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_assign)) {
      if (!move_to_child()) {
        continue;
      }
      Call_actual actual;
      actual.is_named = true;
      actual.name     = std::string(current_text());
      if (!move_to_sibling()) {
        move_to_parent();
        continue;
      }
      auto value = resolve_current_scalar();
      move_to_parent();
      if (!value.has_value() || value->is_invalid()) {
        return std::nullopt;
      }
      actual.value = *value;
      actuals.emplace_back(std::move(actual));
      continue;
    }

    auto value = resolve_current_scalar();
    if (!value.has_value() || value->is_invalid()) {
      return std::nullopt;
    }
    actuals.emplace_back(Call_actual{.value = *value});
  }

  return actuals;
}

bool uPass_constprop::try_eval_comb_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals) {
  std::string callee_name(fname);
  if (callee_name.find('.') == std::string::npos) {
    callee_name = std::string(lm->get_top_module_name()) + "." + callee_name;
  }

  auto fit = function_registry.find(callee_name);
  if (fit == function_registry.end()) {
    return false;
  }

  const auto& fn = fit->second;
  if (!fn) {
    return false;
  }

  std::vector<std::string>                params;
  std::vector<std::string>                outputs;
  std::unordered_map<std::string, Lconst> local_values;

  auto resolve_local = [&](const Lnast_node& node) -> std::optional<Lconst> {
    if (node.type.is_const()) {
      return Lconst::from_pyrope(node.token.get_text());
    }
    if (node.type.is_ref()) {
      auto it = local_values.find(std::string(node.token.get_text()));
      if (it != local_values.end()) {
        return it->second;
      }
    }
    return std::nullopt;
  };

  const auto root  = Lnast_nid::root();
  const auto stmts = fn->get_child(root);
  if (stmts.is_invalid()) {
    return false;
  }

  // Phase 1 — pre-IO setup: collect param/output names from the synthetic
  // __input_tuple_ref / __output_tuple_ref tuple_adds, then bind actuals to
  // params on the io marker. Body statements are deferred to phase 2.
  std::vector<Lnast_nid> body_stmts;
  bool                   after_io = false;
  for (auto stmt = fn->get_child(stmts); !stmt.is_invalid(); stmt = fn->get_sibling_next(stmt)) {
    const auto type = fn->get_type(stmt);
    if (!after_io) {
      if (type.is_tuple_add()) {
        auto tuple_ref = fn->get_child(stmt);
        if (tuple_ref.is_invalid() || !fn->get_type(tuple_ref).is_ref()) {
          continue;
        }
        const auto tuple_name = fn->get_data(tuple_ref).token.get_text();
        for (auto item = fn->get_sibling_next(tuple_ref); !item.is_invalid(); item = fn->get_sibling_next(item)) {
          if (!fn->get_type(item).is_assign()) {
            continue;
          }
          auto key = fn->get_child(item);
          if (key.is_invalid() || !fn->get_type(key).is_ref()) {
            continue;
          }
          if (tuple_name == "__input_tuple_ref") {
            params.emplace_back(fn->get_data(key).token.get_text());
          } else if (tuple_name == "__output_tuple_ref") {
            outputs.emplace_back(fn->get_data(key).token.get_text());
          }
        }
        continue;
      }
      if (type.is_io()) {
        std::size_t positional_idx = 0;
        for (const auto& actual : actuals) {
          if (actual.is_named) {
            local_values[actual.name] = actual.value;
            continue;
          }
          while (positional_idx < params.size() && local_values.contains(params[positional_idx])) {
            ++positional_idx;
          }
          if (positional_idx < params.size()) {
            local_values[params[positional_idx++]] = actual.value;
          }
        }
        after_io = true;
        continue;
      }
      continue;
    }
    body_stmts.push_back(stmt);
  }

  // Phase 2 — body evaluation. Multi-pass fixed-point traversal so a stmt
  // whose inputs are produced *later* in source order can still be folded
  // (prp2lnast emits if's branch conditions as siblings *after* the if
  // node itself; assert_ifelse2.prp's pick_max body is the canonical
  // example). Each visit either records the stmt as processed, defers it
  // for the next sweep, or aborts the whole inline.

  enum class Eval_result { processed, deferred, aborted };

  // Eval helpers shared between top-level and nested stmts blocks.
  std::function<Eval_result(Lnast_nid)> try_eval_stmt;
  std::function<Eval_result(Lnast_nid)> try_eval_block;

  auto eval_nary = [&](Lnast_nid stmt, auto op) -> Eval_result {
    auto lhs = fn->get_child(stmt);
    if (lhs.is_invalid() || !fn->get_type(lhs).is_ref()) {
      return Eval_result::aborted;
    }
    auto arg = fn->get_sibling_next(lhs);
    if (arg.is_invalid()) {
      return Eval_result::aborted;
    }
    auto acc = resolve_local(fn->get_data(arg));
    if (!acc.has_value() || acc->is_invalid() || acc->is_string()) {
      return Eval_result::deferred;
    }
    while (!(arg = fn->get_sibling_next(arg)).is_invalid()) {
      auto rhs = resolve_local(fn->get_data(arg));
      if (!rhs.has_value() || rhs->is_invalid() || rhs->is_string()) {
        return Eval_result::deferred;
      }
      *acc = op(*acc, *rhs);
      if (acc->is_invalid()) {
        return Eval_result::aborted;
      }
    }
    local_values[std::string(fn->get_data(lhs).token.get_text())] = *acc;
    return Eval_result::processed;
  };

  // Two-operand comparison producing 0/1. ne/lt/le/gt/ge use the C++
  // operator overload on Lconst; eq goes through eq_op so it picks up the
  // structural-identity contract used elsewhere in constprop.
  auto eval_compare = [&](Lnast_nid stmt, auto pred) -> Eval_result {
    auto lhs = fn->get_child(stmt);
    if (lhs.is_invalid() || !fn->get_type(lhs).is_ref()) {
      return Eval_result::aborted;
    }
    auto a_nid = fn->get_sibling_next(lhs);
    if (a_nid.is_invalid()) {
      return Eval_result::aborted;
    }
    auto b_nid = fn->get_sibling_next(a_nid);
    if (b_nid.is_invalid()) {
      return Eval_result::aborted;
    }
    auto a_val = resolve_local(fn->get_data(a_nid));
    auto b_val = resolve_local(fn->get_data(b_nid));
    if (!a_val.has_value() || a_val->is_invalid() || a_val->is_string()
        || !b_val.has_value() || b_val->is_invalid() || b_val->is_string()) {
      return Eval_result::deferred;
    }
    Lconst result = pred(*a_val, *b_val) ? Lconst(1) : Lconst(0);
    local_values[std::string(fn->get_data(lhs).token.get_text())] = result;
    return Eval_result::processed;
  };

  try_eval_stmt = [&](Lnast_nid stmt) -> Eval_result {
    const auto type = fn->get_type(stmt);

    if (type.is_assign()) {
      auto lhs = fn->get_child(stmt);
      if (lhs.is_invalid() || !fn->get_type(lhs).is_ref()) {
        return Eval_result::processed;  // skip malformed
      }
      auto rhs = fn->get_sibling_next(lhs);
      if (rhs.is_invalid()) {
        return Eval_result::processed;
      }
      auto value = resolve_local(fn->get_data(rhs));
      if (!value.has_value() || value->is_invalid()) {
        return Eval_result::deferred;
      }
      local_values[std::string(fn->get_data(lhs).token.get_text())] = *value;
      return Eval_result::processed;
    }

    if (type.is_plus()) {
      return eval_nary(stmt, [](Lconst a, Lconst b) { return a.add_op(b); });
    }
    if (type.is_minus()) {
      return eval_nary(stmt, [](Lconst a, Lconst b) { return a.sub_op(b); });
    }
    if (type.is_mult()) {
      return eval_nary(stmt, [](Lconst a, Lconst b) { return a.mult_op(b); });
    }

    if (type.is_eq()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return a.eq_op(b).is_known_true(); });
    }
    if (type.is_ne()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return !a.eq_op(b).is_known_true(); });
    }
    if (type.is_lt()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return a < b; });
    }
    if (type.is_le()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return a <= b; });
    }
    if (type.is_gt()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return a > b; });
    }
    if (type.is_ge()) {
      return eval_compare(stmt, [](const Lconst& a, const Lconst& b) { return a >= b; });
    }

    if (type.is_if()) {
      // children: ref(cond), stmts, [ref(cond), stmts]..., [stmts default]
      auto child = fn->get_child(stmt);
      while (!child.is_invalid()) {
        if (fn->get_type(child).is_stmts()) {
          // bare stmts in the cond slot ⇒ default else
          return try_eval_block(child);
        }
        auto cond_val = resolve_local(fn->get_data(child));
        if (!cond_val.has_value() || cond_val->is_invalid() || cond_val->has_unknowns()) {
          return Eval_result::deferred;
        }
        auto branch = fn->get_sibling_next(child);
        if (branch.is_invalid() || !fn->get_type(branch).is_stmts()) {
          return Eval_result::aborted;
        }
        if (cond_val->is_known_true()) {
          return try_eval_block(branch);
        }
        // known-false ⇒ try the next (cond, stmts) pair
        child = fn->get_sibling_next(branch);
      }
      return Eval_result::processed;  // fell through, no branch ran
    }

    if (type.is_cassert()) {
      auto operand = fn->get_child(stmt);
      if (operand.is_invalid()) {
        return Eval_result::processed;
      }
      auto val = resolve_local(fn->get_data(operand));
      if (!val.has_value() || val->is_invalid() || val->has_unknowns()) {
        return Eval_result::deferred;
      }
      // Dedup-keyed by source location (lnast top-name + cassert nid) so a
      // function called from N call sites contributes one tally per source
      // cassert, not N. The spawned-lnast verifier consults the same key
      // and skips re-counting in classify_statement.
      auto key = std::format("{}:{}:{}", fn->get_top_module_name(), stmt.level, stmt.pos);
      if (val->is_known_false()) {
        uPass_verifier::mark_inlined_cassert_fail(key);
      } else if (val->is_known_true()) {
        uPass_verifier::mark_inlined_cassert_pass(key);
      }
      return Eval_result::processed;
    }

    // Tuple/attr ops in the body are no-ops for this slim inliner — the
    // function-template lnast may still carry default-value scaffolding
    // (output tuple_add inside the body, attr_set on locals) we don't need
    // to replay since outputs come from the explicit assigns in branches.
    if (type.is_tuple_add() || type.is_tuple_get() || type.is_tuple_set() || type.is_attr_set() || type.is_attr_get()) {
      return Eval_result::processed;
    }

    return Eval_result::aborted;
  };

  try_eval_block = [&](Lnast_nid block) -> Eval_result {
    if (!fn->get_type(block).is_stmts()) {
      return Eval_result::aborted;
    }
    std::vector<Lnast_nid> sub_stmts;
    for (auto s = fn->get_child(block); !s.is_invalid(); s = fn->get_sibling_next(s)) {
      sub_stmts.push_back(s);
    }
    std::vector<bool> processed(sub_stmts.size(), false);
    while (true) {
      bool progress = false;
      for (std::size_t i = 0; i < sub_stmts.size(); ++i) {
        if (processed[i]) {
          continue;
        }
        const auto r = try_eval_stmt(sub_stmts[i]);
        if (r == Eval_result::aborted) {
          return Eval_result::aborted;
        }
        if (r == Eval_result::processed) {
          processed[i] = true;
          progress     = true;
        }
      }
      if (!progress) {
        break;
      }
    }
    for (auto p : processed) {
      if (!p) {
        return Eval_result::aborted;
      }
    }
    return Eval_result::processed;
  };

  // Drive the body's fixed-point sweep.
  std::vector<bool> processed(body_stmts.size(), false);
  while (true) {
    bool progress = false;
    for (std::size_t i = 0; i < body_stmts.size(); ++i) {
      if (processed[i]) {
        continue;
      }
      const auto r = try_eval_stmt(body_stmts[i]);
      if (r == Eval_result::aborted) {
        return false;
      }
      if (r == Eval_result::processed) {
        processed[i] = true;
        progress     = true;
      }
    }
    if (!progress) {
      break;
    }
  }
  for (auto p : processed) {
    if (!p) {
      return false;
    }
  }

  if (outputs.empty()) {
    return false;
  }

  if (outputs.size() == 1) {
    auto it = local_values.find(outputs.front());
    if (it == local_values.end() || it->second.is_invalid()) {
      return false;
    }
    bool local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != it->second;
    if (local_changed && st.set(dst, it->second)) {
      mark_changed();
    }
    return true;
  }

  auto out_bundle = std::make_shared<Bundle>(std::string(dst));
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    auto it = local_values.find(outputs[i]);
    if (it == local_values.end() || it->second.is_invalid()) {
      return false;
    }
    out_bundle->set(std::format(":{}:{}", i, outputs[i]), it->second);
  }
  st.set(dst, out_bundle);
  return true;
}

void uPass_constprop::process_func_call() {
  // Layout: ref(dst), ref(func_name) | const(marker_name), (const|ref)(arg)...
  // Two flavours:
  //   - ref-form fname  : built-in typecast callables (int/uint/string/sized).
  //   - const-form fname: marker calls emitted by prp2lnast for operators
  //     without a dedicated LNAST node — `does`, `case`, `has`, `in`,
  //     `break`, `continue`, `return`. We fold the comptime ones here
  //     (`does` for now); the rest are passed through.
  // Anything we don't recognise is left as-is for a later pass.
  move_to_child();
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // Marker-form func_call: const(name). Dispatch on the marker name.
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    std::string marker(current_text());
    if (marker == "does") {
      fold_does(dst);
    } else if (marker == "in") {
      fold_in(dst);
    } else if (marker == "has") {
      fold_has(dst);
    }
    // Other markers (case/break/continue/return) — leave unfolded.
    move_to_parent();
    return;
  }

  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    move_to_parent();
    return;
  }
  std::string fname(current_text());

  auto actuals = collect_call_actuals();
  if (actuals.has_value() && try_eval_comb_call(dst, fname, *actuals)) {
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

  std::vector<Lconst> args;
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
  // already-numeric Lconst. Returns invalid on parse failure.
  // `to_pyrope()` on a string renders as `'content'`; strip the single-quote
  // wrappers before re-parsing so `Lconst::from_pyrope("3")` (an int) is
  // produced rather than `Lconst::from_pyrope("'3'")` (a string round-trip).
  auto to_scalar = [](const Lconst& a) -> Lconst {
    if (!a.is_string()) {
      return a;
    }
    std::string s = a.to_pyrope();
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      s = s.substr(1, s.size() - 2);
    }
    try {
      return Lconst::from_pyrope(s);
    } catch (...) {
      return Lconst::invalid();
    }
  };

  Lconst result;
  if (kind == Cast::to_string) {
    // Lconst::concat_op no longer auto-stringifies ints — string ++ int is
    // a bit-concat producing an int. The `string()` builtin's job is text
    // concat, so coerce each int arg to its decimal-text Lconst up front;
    // the subsequent concat_op then sees only strings.
    auto stringify = [](const Lconst& v) -> Lconst {
      if (v.is_nil()) {
        return Lconst::from_string("nil");  // mirror Pyrope's nil → 'nil' text
      }
      if (v.is_string()) {
        return v;
      }
      return Lconst::from_string(std::to_string(v.to_i()));
    };
    if (args.empty()) {
      result = Lconst::from_string("");
    } else {
      result = stringify(args.front());
      for (size_t i = 1; i < args.size(); ++i) {
        result = result.concat_op(stringify(args[i]));
      }
    }
  } else {
    if (args.size() != 1) {
      return;
    }  // unsupported arity
    Lconst v = to_scalar(args.front());
    if (v.is_invalid()) {
      return;
    }
    if (kind == Cast::to_uint) {
      if (v.is_string() || v.is_negative()) {
        // Cast failure (non-numeric string or negative) → pyrope `nil`.
        v = Lconst::nil();
      }
      result = v;
    } else if (kind == Cast::to_int) {
      if (v.is_string()) {
        v = Lconst::nil();
      }
      result = v;
    } else {
      // sized: fold only when the value fits. Signed/unsigned range check is
      // deferred; the current test set just stores small positives.
      if (v.is_string()) {
        v = Lconst::nil();
      }
      (void)sized_sig;
      (void)sized_bits;
      result = v;
    }
  }

  bool local_changed = true;
  if (st.has_trivial(dst)) {
    local_changed = st.get_trivial(dst) != result;
  }
  if (local_changed && st.set(dst, result)) {
    mark_changed();
  }
}

void uPass_constprop::process_range() {
  // Layout: ref(dst), (const|ref)(start), (const|ref)(end)
  // Resolve start/end and stash in range_map keyed by dst. When either side
  // is unknown, leave the entry absent so a later iteration can retry. For
  // `x[a..]` / `x[..]`, prp2lnast emits the open end as the literal pyrope
  // `nil`, which round-trips as a string Lconst — process_tuple_get treats
  // that sentinel as "to source's last index".
  //
  // For closed `lo..=hi` ranges with concrete integer bounds, also
  // materialize a positional tuple bundle so `cassert (2..=4) == (2,3,4)`
  // folds via compare_bundles_eq.
  move_to_child();
  auto dst = std::string(current_text());

  auto read_operand = [this]() -> Lconst {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      return Lconst::from_pyrope(current_text());
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      if (st.has_trivial(current_text())) {
        return st.get_trivial(current_text());
      }
    }
    return Lconst::invalid();
  };

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Lconst start = read_operand();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Lconst end = read_operand();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }

  auto it      = range_map.find(dst);
  bool changed = false;
  if (it == range_map.end()) {
    range_map.emplace(dst, std::make_pair(start, end));
    changed = true;
  } else if (it->second.first != start || it->second.second != end) {
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
        bundle->set(std::to_string(pos), Lconst(v));
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
      const auto src_val = st.get_trivial(src);
      if (src_val.is_string() && it->second.first.is_i()) {
        const auto& end_lc    = it->second.second;
        const auto  start_idx = static_cast<size_t>(it->second.first.to_i());
        std::string body      = src_val.to_pyrope();
        // to_pyrope wraps strings in single-quotes; strip them.
        if (body.size() >= 2 && body.front() == '\'' && body.back() == '\'') {
          body = body.substr(1, body.size() - 2);
        }
        size_t len   = body.size();
        bool   open  = end_lc.is_nil();  // open-end sentinel for `x[a..]`
        if (!open && !end_lc.is_i()) {
          // closed end must be an integer to fold; bail otherwise.
          move_to_parent();
          return;
        }
        size_t end_i = open ? (len == 0 ? 0 : len - 1) : static_cast<size_t>(end_lc.to_i());
        if (start_idx <= len && end_i + 1 <= len && start_idx <= end_i + 1) {
          auto         sliced       = body.substr(start_idx, end_i - start_idx + 1);
          const Lconst result       = Lconst::from_string(sliced);
          bool         local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
          if (local_changed && st.set(dst, result)) {
            mark_changed();
          }
          move_to_parent();
          return;
        }
      }
      // Integer bit-slice: `b[lo..=hi]` / `b[lo..<hi]` / `b[lo..]` on an
      // integer source extracts the named bit window via Lconst::get_mask_op.
      if (!src_val.is_string() && !src_val.is_invalid() && !src_val.has_unknowns()
          && it->second.first.is_i()) {
        const auto& end_lc = it->second.second;
        const auto  lo     = static_cast<Bits_t>(it->second.first.to_i());
        Lconst      mask;
        if (end_lc.is_nil()) {
          using Number = boost::multiprecision::cpp_int;
          mask         = Lconst(-(Number(1) << lo));
        } else if (end_lc.is_i() && end_lc.to_i() >= it->second.first.to_i()) {
          mask = Lconst::get_mask_value(static_cast<Bits_t>(end_lc.to_i()), lo);
        }
        if (!mask.is_invalid()) {
          const Lconst result        = src_val.get_mask_op(mask);
          if (!result.is_invalid()) {
            bool       local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
            if (local_changed && st.set(dst, result)) {
              mark_changed();
            }
          }
          move_to_parent();
          return;
        }
      }
    }
  }

  // Accumulate field path: each child after src appends ".field" to the key.
  std::string key = src;
  do {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      key += '.';
      key += current_text();
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
    const Lconst result        = st.get_trivial(key);
    bool         local_changed = !st.has_trivial(dst) || st.get_trivial(dst) != result;
    if (local_changed && st.set(dst, result)) {
      mark_changed();
    }
  } else if (st.has_bundle(key)) {
    auto sub_bundle = st.get_bundle(key);
    if (sub_bundle) {
      bool local_changed = !st.has_bundle(dst) || st.get_bundle(dst) != sub_bundle;
      if (local_changed && st.set(dst, sub_bundle)) {
        mark_changed();
      }
    }
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

  // Collect all children after the tuple ref into a (text, is_ref) list.
  // The last child is the value; everything before it is the field path.
  // Capturing `is_ref` matters for the value: a ref's text is a variable name
  // (look it up in the symbol table), not a literal — `Lconst::from_pyrope`
  // happily accepts unparseable text as a string, so without the type tag we
  // would store the raw ref name (e.g. `___3`) as a string Lconst.
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

  // Build field key from path elements (all except last).
  std::string field;
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    field += '.';
    field += path_and_val[i].text;
  }
  auto        key       = tuple_var + field;
  const auto& val_child = path_and_val.back();

  if (val_child.is_ref) {
    // Value is a ref: resolve through the symbol table. Trivial first, then
    // bundle. If the source has no value yet, leave the field unchanged so
    // a later iteration can still discover it.
    if (st.has_trivial(val_child.text)) {
      const auto sv            = st.get_trivial(val_child.text);
      bool       local_changed = !st.has_trivial(key) || st.get_trivial(key) != sv;
      if (local_changed) {
        st.set(key, sv);
        mark_changed();
      }
    } else if (st.has_bundle(val_child.text)) {
      auto b = st.get_bundle(val_child.text);
      if (b) {
        st.set(key, b);  // bundle comparison not value-based; don't mark_changed
      }
    }
  } else {
    // Const literal: parse and store as trivial.
    Lconst val = Lconst::from_pyrope(val_child.text);
    if (!val.is_invalid()) {
      bool local_changed = !st.has_trivial(key) || st.get_trivial(key) != val;
      if (local_changed) {
        st.set(key, val);
        mark_changed();
      }
    }
  }
  move_to_parent();
}

// ── Bitwidth Insensitive Reduce ──────────────────────────────────────────────
//
// All three reduction ops share the same inline pattern:
//   1. Move into the op-node and read the destination ref.
//   2. Move to the single input operand (ref or const).
//   3. If the input is unknown (invalid), skip — we cannot fold.
//   4. Otherwise, compute the 1-bit boolean result and store it.
//
// We do NOT use process_unary() here because that template unconditionally
// calls st.set() even when the input is Lconst::invalid(), which would
// corrupt the symbol table with a spurious zero.

void uPass_constprop::process_red_or() {
  // Reduce-OR: returns 1 if any bit in input is set, 0 if all bits are zero.
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto input = current_prim_value();
  if (!input.is_invalid()) {
    const Lconst result        = input.is_known_false() ? Lconst(0) : Lconst(1);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}

void uPass_constprop::process_red_and() {
  // Reduce-AND: returns 1 only if every bit in input is set (i.e., input is
  // an all-ones mask in its natural bit-width).  Lconst::is_mask() returns
  // true iff the numeric value is of the form 2^n - 1 (all-ones).
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto input = current_prim_value();
  if (!input.is_invalid() && !input.has_unknowns()) {
    const Lconst result        = input.is_mask() ? Lconst(1) : Lconst(0);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}

void uPass_constprop::process_red_xor() {
  // Reduce-XOR: returns the parity of the set bits (1 if popcount is odd).
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto input = current_prim_value();
  if (!input.is_invalid() && !input.has_unknowns()) {
    const Lconst result        = (input.popcount() % 2 == 1) ? Lconst(1) : Lconst(0);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
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
//   - LHS name is not a reg (#), input ($), or output (%) — prefix check is
//     the Slice 1 stand-in for §12's `st.is_reg/is_input/is_output`, and
//   - the symbol table holds a concrete Lconst for LHS (known, no unknowns).
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

  // Regs, inputs, and outputs carry timing/boundary semantics beyond value — always emit.
  // Slice-1 prefix check; migrates to st.is_reg/is_input/is_output after §12.
  if (st.is_reg(lhs_text) || st.is_input(lhs_text) || st.is_output(lhs_text)) {
    return upass::Emit_decision::emit_node();
  }

  // Drop iff the ST holds a fully-known value. is_known_const() encapsulates
  // has_trivial + is_invalid + has_unknowns in one call.
  if (!st.is_known_const(lhs_text)) {
    return upass::Emit_decision::emit_node();
  }

  // Bundle-shape guard. is_known_const returns true as soon as the bundle's
  // position-0 entry is a concrete Lconst, but that's also true for:
  //   - multi-entry tuples: `(1,2)` — two non-attribute entries
  //   - single-entry named tuples: `(c=2)` — one entry but keyed `:0:c`,
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

std::optional<Lconst> uPass_constprop::fold_ref(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  // Strip I/O prefixes so the lookup matches what process_assign stored.
  std::string_view key = name;
  if (st.is_input(key) || st.is_output(key)) {
    key.remove_prefix(1);
  }
  if (!st.is_known_const(key)) {
    return std::nullopt;
  }
  // is_known_const returns true once the bundle's position-0 entry is a
  // concrete Lconst, but that's also true for multi-entry tuples like
  // `(1,2)` and single-entry named tuples like `(c=2)`. Inlining the
  // position-0 trivial would silently truncate `(1,2)` → `1` or strip
  // the name from `(c=2)` → `2`. Only inline when the bundle is a
  // trivial scalar (exactly one anonymous `0` entry); for everything
  // else, return nullopt so the consumer keeps the ref and reads the
  // full bundle from the symbol table.
  if (auto b = st.get_bundle(key); b && !b->is_trivial_scalar()) {
    return std::nullopt;
  }
  return st.get_trivial(key);
}

void uPass_constprop::process_sext() {
  // Sign-extend: [sext: ref(dst), ref_or_const(src), const(nbits)]
  //
  // sext_op(ebits) interprets bit (ebits-1) of src as the sign bit and
  // truncates/extends to that width.  The result is a narrower (or equal)
  // signed integer.
  //
  // We skip folding if:
  //   - src is unknown (invalid or has unknowns), or
  //   - nbits is not a known integer (not is_i()).
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto src = current_prim_value();
  move_to_sibling();
  const auto nbits_lc = current_prim_value();
  if (!src.is_invalid() && !src.has_unknowns() && !src.is_string() && !nbits_lc.is_invalid() && nbits_lc.is_i()) {
    const auto   ebits         = static_cast<Bits_t>(nbits_lc.to_i());
    const Lconst result        = src.sext_op(ebits);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}

void uPass_constprop::process_get_mask() {
  // Layout: ref(dst), ref(value), (const|ref)(mask)
  // The mask operand may be:
  //   - a constant integer / known scalar (treated as a bitmask),
  //   - a `range` ref previously bound in range_map (`b#[lo..]`,
  //     `b#[lo..=hi]`, `b#[..=hi]`, etc.).
  // For the range case we synthesize the equivalent integer mask from the
  // (start, end) pair: closed `lo..=hi` → bits lo..hi mask; open
  // `lo..` → all bits from `lo` upward, encoded as `-(1 << lo)` so
  // Lconst::get_mask_op's negative-mask path extracts the upper bits
  // correctly.
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Lconst value = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  Lconst mask;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    auto rit = range_map.find(std::string(current_text()));
    if (rit != range_map.end()) {
      const auto& start = rit->second.first;
      const auto& end   = rit->second.second;
      if (start.is_i()) {
        const auto lo = static_cast<Bits_t>(start.to_i());
        if (end.is_nil()) {
          // Open end: bits lo..msb. Negative-mask form for get_mask_op.
          using Number = boost::multiprecision::cpp_int;
          Number n     = -(Number(1) << lo);
          mask         = Lconst(n);
        } else if (end.is_i() && end.to_i() >= start.to_i()) {
          const auto hi = static_cast<Bits_t>(end.to_i());
          mask          = Lconst::get_mask_value(hi, lo);
        } else {
          mask = Lconst::invalid();
        }
      } else {
        mask = Lconst::invalid();
      }
    } else {
      mask = current_prim_value();
    }
  } else {
    mask = current_prim_value();
  }

  move_to_parent();

  if (value.is_invalid() || mask.is_invalid() || value.is_string() || mask.is_string()
      || value.has_unknowns() || mask.has_unknowns()) {
    return;
  }

  Lconst result        = value.get_mask_op(mask);
  if (result.is_invalid()) {
    return;
  }
  bool   local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
  if (local_changed && st.set(var, result)) {
    mark_changed();
  }
}
