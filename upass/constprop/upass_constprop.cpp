//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include "lnast_ntype.hpp"

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
    bool changed = true;
    if (st.has_bundle(lhs_text)) {
      changed = st.get_bundle(lhs_text) != rhs_bundle;
    }
    if (changed && st.set(lhs_text, rhs_bundle)) {
      mark_changed();
    }
  } else {
    auto rhs_value = current_pyrope_value();
    bool changed = true;
    if (st.has_trivial(lhs_text)) {
      changed = st.get_trivial(lhs_text) != rhs_value;
    }
    if (changed && st.set(lhs_text, rhs_value)) {
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
  bool changed = true;
  if (st.has_trivial(var)) {
    changed = st.get_trivial(var) != r;
  }
  if (changed && st.set(var, r)) {
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
  Lconst r  = op(n1, n2);
  bool changed = true;
  if (st.has_trivial(var)) {
    changed = st.get_trivial(var) != r;
  }
  if (changed && st.set(var, r)) {
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
  bool changed = true;
  if (st.has_trivial(var)) {
    changed = st.get_trivial(var) != r;
  }
  if (changed && st.set(var, r)) {
    mark_changed();
  }

  move_to_parent();
}

void uPass_constprop::process_plus() {
  process_nary([](Lconst &r, Lconst n) { r = r.add_op(n); });
}

void uPass_constprop::process_minus() {
  process_nary([](Lconst &r, Lconst n) { r = r.sub_op(n); });
}

void uPass_constprop::process_mult() {
  process_nary([](Lconst &r, Lconst n) { r = r.mult_op(n); });
}

void uPass_constprop::process_div() {
  process_nary([](Lconst &r, Lconst n) { r = r.div_op(n); });
}

void uPass_constprop::process_bit_and() {
  process_nary([](Lconst &r, Lconst n) { r = r.and_op(n); });
}

void uPass_constprop::process_bit_or() {
  process_nary([](Lconst &r, Lconst n) { r = r.or_op(n); });
}

void uPass_constprop::process_bit_not() {
  process_unary([](Lconst &r) { r = r.not_op(); });
}

void uPass_constprop::process_log_and() {
  // Logical AND: returns 1 if both operands are non-zero, 0 otherwise.
  // Lconst(int64_t) is non-explicit so Lconst(0)/Lconst(1) construct directly.
  process_binary([](Lconst n1, Lconst n2) -> Lconst {
    return (!n1.is_known_false() && !n2.is_known_false()) ? Lconst(1) : Lconst(0);
  });
}

void uPass_constprop::process_log_or() {
  // Logical OR: returns 1 if either operand is non-zero, 0 if both are zero.
  // Lconst::ror_op already encodes this: (this != 0 || o != 0) ? 1 : 0.
  process_binary([](Lconst n1, Lconst n2) -> Lconst { return n1.ror_op(n2); });
}

void uPass_constprop::process_log_not() {
  // Logical NOT: returns 1 if operand is zero, 0 if operand is non-zero.
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
  // TODO(constprop): Dead-branch elimination for constant conditions.
  //
  // LNAST if-node child layout (interleaved cond / stmts):
  //   child[0]          — condition expression node (e.g. gt, lt, eq, ref, const)
  //   child[1]          — condition result ref / const (the variable carrying
  //                       the boolean result of child[0])
  //   child[2]          — stmts (then-branch)
  //   child[3..4]       — (optional) second cond-expr + cond-ref  (else-if)
  //   child[5]          — (optional) stmts (else-if branch)
  //   child[last-stmts] — (optional) bare stmts (final else, no preceding cond)
  //
  // Implementation sketch:
  //   1. Walk sibling pairs (cond-ref, stmts) in the if-node children.
  //   2. For each cond-ref: look it up in the symbol table.
  //   3. If the value is a known non-zero const → this branch is always taken;
  //      process its stmts and delete all subsequent cond+stmts siblings.
  //   4. If the value is known zero → skip this branch's stmts.
  //   5. If unknown → process all branches (conservative; no deletion).
  //
  // Prerequisite: upass_runner must add PROCESS_BLOCK(if) so that the runner
  // recurses into if-node children and invokes process_if() on each pass.
}
void uPass_constprop::process_stmts() {}

void uPass_constprop::process_tuple_set() {}
void uPass_constprop::process_tuple_get() {}
void uPass_constprop::process_tuple_add() {}
