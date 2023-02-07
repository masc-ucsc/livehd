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
    st.set(lhs_text, rhs_bundle);
  } else {
    auto rhs_value = current_pyrope_value();
    st.set(lhs_text, rhs_value);
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
  st.set(var, r);

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
  st.set(var, r);

  move_to_parent();
}

template <typename F>
void uPass_constprop::process_unary(F op) {
  move_to_child();

  auto var = current_text();
  move_to_sibling();
  Lconst r = current_prim_value();
  op(r);
  st.set(var, r);

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

void uPass_constprop::process_log_and() {}
void uPass_constprop::process_log_or() {}
void uPass_constprop::process_log_not() {}

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

void uPass_constprop::process_if() {}
void uPass_constprop::process_stmts() {}

void uPass_constprop::process_tuple_set() {}
void uPass_constprop::process_tuple_get() {}
void uPass_constprop::process_tuple_add() {}
