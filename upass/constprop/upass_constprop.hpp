//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <map>
#include <string>

#include "lconst.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

struct uPass_constprop : public upass::uPass {
public:
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  void process_assign() override;
  void process_plus() override;
  void process_minus() override;
  void process_mult() override;
  void process_div() override;
  void process_mod() override;
  void process_shl() override;
  void process_sra() override;
  void process_bit_and() override;
  void process_bit_or() override;
  void process_bit_not() override;
  void process_bit_xor() override;
  void process_log_and() override;
  void process_log_or() override;
  void process_log_not() override;
  void process_ne() override;
  void process_eq() override;
  void process_lt() override;
  void process_le() override;
  void process_gt() override;
  void process_ge() override;
  void process_if() override;

  // Bitwidth Insensitive Reduce
  void process_red_or() override;
  void process_red_and() override;
  void process_red_xor() override;

  // Bit Manipulation
  void process_sext() override;
  void process_get_mask() override;

  void process_stmts() override;
  void process_tuple_set() override;
  void process_tuple_get() override;
  void process_tuple_add() override;
  void process_tuple_concat() override;
  void process_func_call() override;
  void process_range() override;

  upass::Emit_decision  classify_statement() override;
  std::optional<Lconst> fold_ref(std::string_view name) override;

protected:
  Symbol_table st;

  // Range bookkeeping outside the symbol table: a `range` LNAST node binds
  // its destination ref to a (start, end) pair. `end` may be the literal
  // pyrope `nil` to mean "open-ended" (slice runs to the source's last
  // index). process_tuple_get consults this map when the field operand is
  // a ref so it can fold string slicing like `x[1..]` and `x[1..=2]`.
  std::map<std::string, std::pair<Lconst, Lconst>> range_map;

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

  template <bool Negate>
  void process_eq_ne_impl();

  void fold_does(const std::string& dst);
};

// Plugin registration lives in upass_constprop.cpp to avoid duplicate
// construction when multiple TUs include this header.
