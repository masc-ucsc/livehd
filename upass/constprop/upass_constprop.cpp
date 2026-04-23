//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include "lnast_ntype.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_constprop.hpp.
static upass::uPass_plugin cprop("constprop", upass::uPass_wrapper<uPass_constprop>::get_upass);

uPass_constprop::uPass_constprop(std::shared_ptr<upass::Lnast_manager> &_lm) : uPass(_lm) {
  st.function_scope(_lm->get_top_module_name());
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
      st.set(lhs_text, rhs_bundle);
    }
  } else if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    // RHS is a const literal: parse and track the constant value
    auto rhs_value = current_pyrope_value();
    bool local_changed   = true;
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

  // If the first operand is unknown or a non-numeric type, leave dst unknown.
  // Arithmetic on strings or invalid Lconst values is undefined and will crash.
  if (r.is_invalid() || r.is_string() || r.has_unknowns()) {
    move_to_parent();
    return;
  }

  while (move_to_sibling()) {
    auto operand = current_prim_value();
    if (operand.is_invalid() || operand.is_string() || operand.has_unknowns()) {
      move_to_parent();
      return;
    }
    op(r, operand);
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

  // Guard: if either operand is unknown, a non-numeric string, or has X-bits,
  // skip folding — leave dst unknown.  This prevents "one is a string" errors
  // and the "assertion is_i() failed" crash in shift lambdas that call .to_i().
  if (n1.is_invalid() || n1.is_string() || n1.has_unknowns() ||
      n2.is_invalid() || n2.is_string() || n2.has_unknowns()) {
    move_to_parent();
    return;
  }

  Lconst r = op(n1, n2);
  // If the operation itself produced an invalid result (e.g., divide-by-zero,
  // or shift amount not representable as integer), leave dst unknown.
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

  // Guard: leave dst unknown if input is invalid, non-numeric, or has X-bits.
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
  process_binary([](Lconst n1, Lconst n2) {
    return n1.or_op(n2).and_op(n1.and_op(n2).not_op());
  });
}

void uPass_constprop::process_mod() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    // Guard: modulo by zero or non-integer operands is undefined.
    if (!n1.is_i() || !n2.is_i() || n2.is_known_false()) {
      return Lconst::invalid();
    }
    return Lconst(n1.to_i() % n2.to_i());
  });
}

void uPass_constprop::process_shl() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    // to_i() asserts is_i() — outer guard in process_binary screens out
    // invalid/string/unknowns, but defend-in-depth for non-integer types.
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
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    return (!n1.is_known_false() && !n2.is_known_false()) ? Lconst(1) : Lconst(0);
  });
}

void uPass_constprop::process_log_or() {
  process_binary([](Lconst n1, Lconst n2) -> Lconst { return n1.ror_op(n2); });
}

void uPass_constprop::process_log_not() {
  process_unary([](Lconst &r) { r = r.is_known_false() ? Lconst(1) : Lconst(0); });
}

void uPass_constprop::process_ne() {
  process_binary([](Lconst x, Lconst y) { return x != y; });
}

void uPass_constprop::process_eq() {
  process_binary([](Lconst x, Lconst y) { return x == y; });
}

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
  if (!move_to_child()) return;

  while (true) {
    if (is_type(Lnast_ntype::Lnast_ntype_stmts)) {
      break;  // bare else-stmts: no condition
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref) || is_type(Lnast_ntype::Lnast_ntype_const)) {
      [[maybe_unused]] const auto cond_val = current_prim_value();
    }
    if (!move_to_sibling()) break;
    if (!move_to_sibling()) break;
  }

  move_to_parent();
}
void uPass_constprop::process_stmts() {}

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
    const Lconst result       = st.get_trivial(key);
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

  // Collect all children after the tuple ref into a path + value list.
  // The last child is the value; everything before it is the field path.
  std::vector<std::string> path_and_val;
  while (move_to_sibling()) {
    path_and_val.emplace_back(current_text());
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
    const auto& f = path_and_val[i];
    if (f.size() > 2 && f[0] == '_' && f[1] == '_' && f[2] != '_') {
      move_to_parent();
      return;
    }
  }

  // Build field key from path elements (all except last).
  std::string field;
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    field += '.';
    field += path_and_val[i];
  }
  auto        key       = tuple_var + field;
  const auto& val_text  = path_and_val.back();

  // Value is the last child — stored as trivial if it parses, else as a bundle ref.
  Lconst val = Lconst::from_pyrope(val_text);
  if (!val.is_invalid()) {
    bool local_changed = !st.has_trivial(key) || st.get_trivial(key) != val;
    if (local_changed) {
      st.set(key, val);
      mark_changed();
    }
  } else if (st.has_trivial(val_text)) {
    const auto sv     = st.get_trivial(val_text);
    bool local_changed = !st.has_trivial(key) || st.get_trivial(key) != sv;
    if (local_changed) {
      st.set(key, sv);
      mark_changed();
    }
  } else if (st.has_bundle(val_text)) {
    auto b = st.get_bundle(val_text);
    if (b) {
      st.set(key, b);  // bundle comparison not value-based; don't mark_changed
    }
  }
  move_to_parent();
}

// ── Attribute Operations ─────────────────────────────────────────────────────
//
// Layout (from lnast_writer):
//   attr_set:  ref(tuple), ref/const(attr)..., ref/const(value)
//              Printed as:  tuple.attr = value
//   attr_get:  ref(dst), ref(tuple), ref/const(attr)...
//              Printed as:  dst = tuple.attr
//
// Constprop skips __bits / __signed attributes — those are bitwidth annotations
// that the bitwidth pass owns; propagating them causes non-convergence (see
// tuple_set comment above for the same reason).

void uPass_constprop::process_attr_set() {
  // Layout: ref(tuple), ref/const(attr)..., ref/const(value)
  // All children after the tuple ref are path components; the LAST child is the value.
  move_to_child();
  auto tuple_var = std::string(current_text());

  std::vector<std::string> path_and_val;
  while (move_to_sibling()) {
    path_and_val.emplace_back(current_text());
  }
  move_to_parent();

  if (path_and_val.size() < 2) return;  // need at least one attr component + value

  // Skip Pyrope bitwidth / signedness annotations (__bits, __signed, etc.)
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    const auto& f = path_and_val[i];
    if (f.size() > 2 && f[0] == '_' && f[1] == '_' && f[2] != '_') return;
  }

  // Build key: "tuple.attr1.attr2..."
  std::string key = tuple_var;
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    key += '.';
    key += path_and_val[i];
  }
  const auto& val_text = path_and_val.back();

  Lconst val = Lconst::from_pyrope(val_text);
  if (!val.is_invalid()) {
    bool local_changed = !st.has_trivial(key) || st.get_trivial(key) != val;
    if (local_changed) {
      st.set(key, val);
      mark_changed();
    }
  } else if (st.has_trivial(val_text)) {
    const auto sv      = st.get_trivial(val_text);
    bool local_changed = !st.has_trivial(key) || st.get_trivial(key) != sv;
    if (local_changed) {
      st.set(key, sv);
      mark_changed();
    }
  }
}

void uPass_constprop::process_attr_get() {
  // Layout: ref(dst), ref(tuple), ref/const(attr)...
  // Printed as: dst = tuple.attr
  move_to_child();
  auto dst = std::string(current_text());

  // Guard: malformed node with no tuple sibling — leave dst unknown.
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string key = std::string(current_text());  // start with tuple name

  while (move_to_sibling()) {
    key += '.';
    key += current_text();
  }
  move_to_parent();

  if (st.has_trivial(key)) {
    const Lconst result    = st.get_trivial(key);
    bool local_changed     = !st.has_trivial(dst) || st.get_trivial(dst) != result;
    if (local_changed && st.set(dst, result)) {
      mark_changed();
    }
  } else if (st.has_bundle(key)) {
    auto sub_bundle    = st.get_bundle(key);
    bool local_changed = !st.has_bundle(dst) || st.get_bundle(dst) != sub_bundle;
    if (sub_bundle && local_changed && st.set(dst, sub_bundle)) {
      mark_changed();
    }
  }
}

// ── Tuple Concatenation ───────────────────────────────────────────────────────
//
// Layout:  ref(dst), ref(lhs), ref(rhs)
// Pyrope:  dst = lhs ++ rhs
//
// Merges the fields from lhs and rhs bundles into a new bundle for dst.
// Fields from lhs come first (positions 0..N-1), rhs fields follow (N..M-1).
// If either side is unknown (no bundle in symbol table), dst stays unknown —
// soundness over completeness.

void uPass_constprop::process_tuple_concat() {
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) { move_to_parent(); return; }
  auto lhs_name = std::string(current_text());

  if (!move_to_sibling()) { move_to_parent(); return; }
  auto rhs_name = std::string(current_text());

  move_to_parent();

  auto lhs_bundle = st.get_bundle(lhs_name);
  auto rhs_bundle = st.get_bundle(rhs_name);

  // If either side is unknown we cannot fold — leave dst unknown (soundness).
  if (!lhs_bundle || !rhs_bundle) return;

  // Build dst as a copy of lhs, then concat rhs into it using Bundle::concat().
  auto dst_bundle = std::make_shared<Bundle>(dst);
  dst_bundle->concat(lhs_bundle);
  dst_bundle->concat(rhs_bundle);
  st.set(dst, dst_bundle);
  // Field-level convergence driven by downstream process_tuple_get calls.
}

// ── Delay Assign ─────────────────────────────────────────────────────────────
//
// Layout:  ref(dst), ref(src), const(offset)
// Meaning: dst = value of src delayed by `offset` clock cycles.
//   offset > 0 → next-cycle (register D pin)
//   offset = 0 → current reg Q pin
//   offset < 0 → past cycle
//
// Constprop cannot know the previous/next cycle's value at compile time, so
// dst is left unknown (no entry written to symbol table).  This is soundness
// over completeness — better to leave unknown than to fold incorrectly.

void uPass_constprop::process_delay_assign() {
  // Intentional no-op: cross-cycle values are not statically foldable.
  // dst stays unknown in the symbol table.
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
    const Lconst result       = input.is_known_false() ? Lconst(0) : Lconst(1);
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
    const Lconst result       = input.is_mask() ? Lconst(1) : Lconst(0);
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
    const Lconst result       = (input.popcount() % 2 == 1) ? Lconst(1) : Lconst(0);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}

// ── Bit Manipulation ─────────────────────────────────────────────────────────

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
  if (!src.is_invalid() && !src.has_unknowns() && !src.is_string() &&
      !nbits_lc.is_invalid() && nbits_lc.is_i()) {
    const auto   ebits        = static_cast<Bits_t>(nbits_lc.to_i());
    const Lconst result       = src.sext_op(ebits);
    bool         local_changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (local_changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}
