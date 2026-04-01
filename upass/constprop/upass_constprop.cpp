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

  if (lhs_text == "%out") {
    lhs_text = lhs_text.substr(1);
  }

  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    auto rhs_bundle = current_bundle();
    bool local_changed    = true;
    if (st.has_bundle(lhs_text)) {
      local_changed = st.get_bundle(lhs_text) != rhs_bundle;
    }
    if (local_changed && st.set(lhs_text, rhs_bundle)) {
      mark_changed();
    }
  } else {
    auto rhs_value = current_pyrope_value();
    bool local_changed   = true;
    if (st.has_trivial(lhs_text)) {
      local_changed = st.get_trivial(lhs_text) != rhs_value;
    }
    if (local_changed && st.set(lhs_text, rhs_value)) {
      mark_changed();
    }
  }

  move_to_parent();
}

template <typename F>
void uPass_constprop::process_nary(F op) {
  move_to_child();

  auto var = current_text();
  move_to_sibling();
  Lconst r = current_prim_value();
  while (move_to_sibling()) {
    op(r, current_prim_value());
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
  Lconst n2      = current_prim_value();
  Lconst r       = op(n1, n2);
  bool   local_changed = true;
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
  process_binary([](Lconst n1, Lconst n2) {
    return Lconst(n1.to_i() % n2.to_i());
  });
}

void uPass_constprop::process_shl() {
  process_binary([](Lconst n1, Lconst n2) {
    return n1.lsh_op(static_cast<Bits_t>(n2.to_i()));
  });
}

void uPass_constprop::process_sra() {
  process_binary([](Lconst n1, Lconst n2) {
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

void uPass_constprop::process_tuple_set() {}
void uPass_constprop::process_tuple_get() {}
void uPass_constprop::process_tuple_add() {}

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
    const Lconst result = input.is_known_false() ? Lconst(0) : Lconst(1);
    bool changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (changed && st.set(var, result)) {
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
    const Lconst result = input.is_mask() ? Lconst(1) : Lconst(0);
    bool changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (changed && st.set(var, result)) {
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
    const Lconst result = (input.popcount() % 2 == 1) ? Lconst(1) : Lconst(0);
    bool changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (changed && st.set(var, result)) {
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
    const auto ebits  = static_cast<Bits_t>(nbits_lc.to_i());
    const Lconst result = src.sext_op(ebits);
    bool changed = !st.has_trivial(var) || st.get_trivial(var) != result;
    if (changed && st.set(var, result)) {
      mark_changed();
    }
  }
  move_to_parent();
}
