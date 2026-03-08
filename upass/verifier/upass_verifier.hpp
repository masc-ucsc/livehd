//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <string>

#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_verifier : public upass::uPass {
public:
  using uPass::uPass;

  // ── Assignment ────────────────────────────────────────────────────────────
  void process_assign() override { check_unary(); }

  // ── Bitwidth operators ────────────────────────────────────────────────────
  void process_bit_and() override { check_binary(); }
  void process_bit_or()  override { check_binary(); }
  void process_bit_not() override { check_unary();  }
  void process_bit_xor() override { check_binary(); }

  // ── Bitwidth-insensitive reduce ───────────────────────────────────────────
  void process_red_or()  override { check_unary(); }

  // ── Logical operators ─────────────────────────────────────────────────────
  void process_log_and() override { check_binary(); }
  void process_log_or()  override { check_binary(); }
  void process_log_not() override { check_unary();  }

  // ── Arithmetic ────────────────────────────────────────────────────────────
  void process_plus()  override { check_binary(); }
  void process_minus() override { check_binary(); }
  void process_mult()  override { check_binary(); }
  void process_div()   override { check_binary(); }
  void process_mod()   override { check_binary(); }

  // ── Shift ─────────────────────────────────────────────────────────────────
  void process_shl() override { check_binary(); }
  void process_sra() override { check_binary(); }

  // ── Comparison ────────────────────────────────────────────────────────────
  void process_ne() override { check_binary(); }
  void process_eq() override { check_binary(); }
  void process_lt() override { check_binary(); }
  void process_le() override { check_binary(); }
  void process_gt() override { check_binary(); }
  void process_ge() override { check_binary(); }

private:
  // Validates an operator node with exactly two operands:
  //   child[0] = destination (ref)
  //   child[1] = first  operand (ref | const)
  //   child[2] = second operand (ref | const)
  void check_binary() {
    move_to_child();
    check_type("destination", Lnast_ntype::Lnast_ntype_ref);

    move_to_sibling();
    check_type("first operand",
               Lnast_ntype::Lnast_ntype_ref,
               Lnast_ntype::Lnast_ntype_const);

    move_to_sibling();
    check_type("second operand",
               Lnast_ntype::Lnast_ntype_ref,
               Lnast_ntype::Lnast_ntype_const);

    end_of_siblings("binary operator (expected exactly 3 children)");
    move_to_parent();
  }

  // Validates an operator node with exactly one operand (or an assignment):
  //   child[0] = destination (ref)
  //   child[1] = operand     (ref | const)
  void check_unary() {
    move_to_child();
    check_type("destination", Lnast_ntype::Lnast_ntype_ref);

    move_to_sibling();
    check_type("operand",
               Lnast_ntype::Lnast_ntype_ref,
               Lnast_ntype::Lnast_ntype_const);

    end_of_siblings("unary operator / assign (expected exactly 2 children)");
    move_to_parent();
  }

  // Reports if there are unexpected extra siblings after we have consumed
  // all expected children.
  void end_of_siblings(std::string_view ctx) const {
    if (!is_last_child()) {
      upass::error("verifier: extra sibling in {} near '{}'\n",
                   ctx, current_text());
    }
  }

  // Checks that the current node's type matches at least one of the expected
  // types.  Reports the actual type and all expected types on mismatch.
  template <class... Lnast_ntype_int>
  void check_type(std::string_view role, Lnast_ntype_int... expected) const {
    if (is_invalid()) {
      upass::error("verifier: missing {} (got invalid/end-of-siblings)\n", role);
    }
    auto actual = get_raw_ntype();
    if (((actual == expected) || ...)) {
      return;  // OK
    }
    // Build the "expected: X | Y" string from the parameter pack.
    std::string expected_str;
    bool first = true;
    (
      [&] {
        if (!first) { expected_str += " | "; }
        expected_str += Lnast_ntype::debug_name(expected);
        first = false;
      }(), ...
    );
    upass::error("verifier: {} — expected {} but got {} (text='{}')\n",
                 role,
                 expected_str,
                 Lnast_ntype::debug_name(actual),
                 current_text());
  }
};

static upass::uPass_plugin verifier("verifier", upass::uPass_wrapper<uPass_verifier>::get_upass);
