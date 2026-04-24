//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>

#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

/// Constant-propagation pass over LNAST.
///
/// Folds nodes whose operands are all known compile-time constants into a single
/// constant stored in Symbol_table.  The fixed-point runner re-iterates until
/// no changes occur (mark_changed() is not called).
struct uPass_constprop : public upass::uPass {
public:
  /// Construct with a shared Lnast_manager; initialises the symbol table scope.
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  /// Propagate a known RHS constant or bundle alias into LHS.
  void process_assign() override;
  /// Fold n-ary addition of constants into LHS.
  void process_plus() override;
  /// Fold n-ary subtraction of constants into LHS.
  void process_minus() override;
  /// Fold n-ary multiplication of constants into LHS.
  void process_mult() override;
  /// Fold n-ary division of constants into LHS.
  void process_div() override;
  /// Fold binary modulo; leaves LHS unknown on divide-by-zero or non-integer operands.
  void process_mod() override;
  /// Fold binary left-shift; leaves LHS unknown if shift amount is not a known integer.
  void process_shl() override;
  /// Fold binary arithmetic right-shift; leaves LHS unknown if shift amount is not a known integer.
  void process_sra() override;
  /// Fold n-ary bitwise AND of constants into LHS.
  void process_bit_and() override;
  /// Fold n-ary bitwise OR of constants into LHS.
  void process_bit_or() override;
  /// Fold bitwise NOT of a constant into LHS.
  void process_bit_not() override;
  /// Fold binary bitwise XOR via (a|b)&~(a&b) identity into LHS.
  void process_bit_xor() override;
  /// Fold binary logical AND into a 1-bit boolean constant.
  void process_log_and() override;
  /// Fold binary logical OR into a 1-bit boolean constant.
  void process_log_or() override;
  /// Fold logical NOT into a 1-bit boolean constant.
  void process_log_not() override;
  /// Fold binary not-equal comparison into a 1-bit boolean constant.
  void process_ne() override;
  /// Fold binary equality comparison into a 1-bit boolean constant.
  void process_eq() override;
  /// Fold binary less-than comparison into a 1-bit boolean constant.
  void process_lt() override;
  /// Fold binary less-than-or-equal comparison into a 1-bit boolean constant.
  void process_le() override;
  /// Fold binary greater-than comparison into a 1-bit boolean constant.
  void process_gt() override;
  /// Fold binary greater-than-or-equal comparison into a 1-bit boolean constant.
  void process_ge() override;
  /// Conservatively traverse if-node children; dead-branch elimination not yet implemented.
  void process_if() override;

  /// Reduce-OR: fold to 1 if any bit is set, else 0.
  void process_red_or() override;
  /// Reduce-AND: fold to 1 only if all bits are set (all-ones mask).
  void process_red_and() override;
  /// Reduce-XOR: fold to the parity (1 if popcount is odd) of the input.
  void process_red_xor() override;

  /// Sign-extend src to nbits; skips if src or nbits are unknown.
  void process_sext() override;
  /// Mask-and-extract: apply a bit mask to extract a value field.
  void process_get_mask() override;

  /// No-op hook; traversal of stmts children is handled by the runner.
  void process_stmts() override;
  /// Update a single field inside an existing tuple bundle; skips __bits/__signed annotations.
  void process_tuple_set() override;
  /// Read dst = src[field]: propagate a known scalar or sub-bundle into dst.
  void process_tuple_get() override;
  /// Build or update a Bundle for dst from each positional or named field child.
  void process_tuple_add() override;
  /// Concatenate operand scalars in order using Lconst::concat_op.
  void process_tuple_concat() override;
  /// Fold known built-in typecast calls (int/uint/string/uN/sN) into a constant.
  void process_func_call() override;

  /// Decide whether the current statement can be dropped (folded away) or must be emitted.
  upass::Emit_decision  classify_statement() override;
  /// Return the folded constant for a named variable, or nullopt if unknown.
  std::optional<Lconst> fold_ref(std::string_view name) override;

protected:
  Symbol_table st;

  /// Return the bundle currently tracked for the node under the cursor, or nullptr.
  auto current_bundle() { return st.get_bundle(current_text()); }

  /// Parse the current node's text as a Pyrope literal and return its Lconst value.
  auto current_pyrope_value() { return Lconst::from_pyrope(current_text()); }

  /// Return the constant value of the current node: symbol table lookup for refs,
  /// literal parse for consts.  Returns Lconst::invalid() if unknown.
  auto current_prim_value() const {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return st.get_trivial(current_text());
    }
    I(is_type(Lnast_ntype::Lnast_ntype_const));
    return Lconst::from_pyrope(current_text());
  }

  /// Apply op(accumulator, operand) across all operand children; skips if any operand is invalid/string/unknown.
  template <typename F>
  inline void process_nary(F op);

  /// Apply op(n1, n2) on exactly two operand children; skips if either is invalid/string/unknown.
  template <typename F>
  inline void process_binary(F op);

  /// Apply op(r) on a single operand child; skips if the operand is invalid/string/unknown.
  template <typename F>
  inline void process_unary(F op);
};

// Plugin registration lives in upass_constprop.cpp to avoid duplicate
// construction when multiple TUs include this header.
