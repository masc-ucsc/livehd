//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>

#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

struct uPass_constprop : public upass::uPass {
public:
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  void process_assign();
  void process_plus();
  void process_minus();
  void process_mult();
  void process_div();
  void process_bit_and();
  void process_bit_or();
  void process_bit_not();
  void process_log_and();
  void process_log_or();
  void process_log_not();
  void process_ne();
  void process_eq();
  void process_lt();
  void process_le();
  void process_gt();
  void process_ge();
  void process_if();

  void process_stmts();
  void process_tuple_set();
  void process_tuple_get();
  void process_tuple_add();

protected:
  Symbol_table st;

  auto current_bundle() { return st.get_bundle(current_text()); }

  auto current_pyrope_value() { return Lconst::from_pyrope(current_text()); }

  auto current_prim_value() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return st.get_trivial(current_text());
    }
    I(is_type(Lnast_ntype::Lnast_ntype_const));
    return Lconst::from_pyrope(current_text());
  }

  template <typename F>
  inline void process_nary(F op);

  template <typename F>
  inline void process_binary(F op);

  template <typename F>
  inline void process_unary(F op);
};

static upass::uPass_plugin cprop("constprop", upass::uPass_wrapper<uPass_constprop>::get_upass);
