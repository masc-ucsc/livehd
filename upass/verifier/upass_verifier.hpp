//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "fmt/format.h"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_verifier : public upass::uPass {
public:
  using uPass::uPass;

  // Assignment
  // void process_assign() override { check_binary(); }

  // Operators
  // - Bitwidth
  void process_bit_and() override { check_binary(); }
  void process_bit_or() override { check_binary(); }
  // void process_bit_not() override { check_binary(); }
  void process_bit_xor() override { check_binary(); }
  // - Bitwidth Insensitive Reduce
  // void process_reduce_or() override { check_binary(); }
  // - Logical
  void process_logical_and() override { check_binary(); }
  void process_logical_or() override { check_binary(); }
  // void process_logical_not() override { check_binary(); }
  // - Arithmetic
  void process_plus() override { check_binary(); }
  void process_minus() override { check_binary(); }
  void process_mult() override { check_binary(); }
  void process_div() override { check_binary(); }
  void process_mod() override { check_binary(); }
  // - Shift
  void process_shl() override { check_binary(); }
  void process_sra() override { check_binary(); }
  // - Bit Manipulation
  // void process_sext() override { check_binary(); }
  // void process_set_mask() override { check_binary(); }
  // void process_get_mask() override { check_binary(); }
  // void process_mask_and() override { check_binary(); }
  // void process_mask_popcount() override { check_binary(); }
  // void process_mask_xor() override { check_binary(); }
  // - Comparison
  void process_ne() override { check_binary(); }
  void process_eq() override { check_binary(); }
  void process_lt() override { check_binary(); }
  void process_le() override { check_binary(); }
  void process_gt() override { check_binary(); }
  void process_ge() override { check_binary(); }

private:
  void check_binary() {
    move_to_child();

    check_type(Lnast_ntype::Lnast_ntype_ref);
    move_to_sibling();
    check_type(Lnast_ntype::Lnast_ntype_ref, Lnast_ntype::Lnast_ntype_const);
    move_to_sibling();
    check_type(Lnast_ntype::Lnast_ntype_ref, Lnast_ntype::Lnast_ntype_const);
    end_of_siblings();

    move_to_parent();
  }

  void end_of_siblings() const {
    if (!is_last_child()) {
      upass::error("");
    }
  }

  template <class... Lnast_ntype_int>
  void check_type(Lnast_ntype_int... ty) const {
    if (is_invalid()) {
      upass::error("invalid\n");
      return;
    }
    // print_types(ty...);
    auto n = get_raw_ntype();
    // print_types(n);
    if (((n == ty) || ...) || false) {
      return;
    }
    upass::error("failed\n");
  }

  template <class T, class... Targs>
  void print_types(T ty, Targs... tys) const {
    std::cout << Lnast_ntype::debug_name(ty) << " ";
    print_types(tys...);
  }

  void print_types() const {}
};

static upass::uPass_plugin verifier("verifier", upass::uPass_wrapper<uPass_verifier>::get_upass);
