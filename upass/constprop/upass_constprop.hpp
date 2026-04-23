//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>

#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

/// Constant-propagation pass over LNAST.
///
/// Walks the LNAST tree and folds nodes whose operands are all known compile-time
/// constants into a single constant value stored in the Symbol_table.  Convergence
/// is driven by the fixed-point runner: the pass signals `mark_changed()` whenever
/// a new constant is discovered, and the runner re-iterates until no changes occur.
struct uPass_constprop : public upass::uPass {
public:
  /// Construct with a shared Lnast_manager; initialises the symbol table scope.
  uPass_constprop(std::shared_ptr<upass::Lnast_manager>&);
  uPass_constprop() = delete;
  virtual ~uPass_constprop() {}

  /// Fold a simple assignment: propagate a known RHS constant or bundle alias into LHS.
  void process_assign();

  /// Fold n-ary addition of constants into LHS.
  void process_plus();
  /// Fold n-ary subtraction of constants into LHS.
  void process_minus();
  /// Fold n-ary multiplication of constants into LHS.
  void process_mult();
  /// Fold n-ary division of constants into LHS.
  void process_div();
  /// Fold binary modulo; leaves LHS unknown on divide-by-zero or non-integer operands.
  void process_mod();
  /// Fold binary left-shift; leaves LHS unknown if shift amount is not a known integer.
  void process_shl();
  /// Fold binary arithmetic right-shift; leaves LHS unknown if shift amount is not a known integer.
  void process_sra();
  /// Fold n-ary bitwise AND of constants into LHS.
  void process_bit_and();
  /// Fold n-ary bitwise OR of constants into LHS.
  void process_bit_or();
  /// Fold bitwise NOT of a constant into LHS.
  void process_bit_not();
  /// Fold binary bitwise XOR via (a|b)&~(a&b) identity into LHS.
  void process_bit_xor();
  /// Fold binary logical AND into a 1-bit boolean constant.
  void process_log_and();
  /// Fold binary logical OR into a 1-bit boolean constant.
  void process_log_or();
  /// Fold logical NOT into a 1-bit boolean constant.
  void process_log_not();
  /// Fold binary not-equal comparison into a 1-bit boolean constant.
  void process_ne();
  /// Fold binary equality comparison into a 1-bit boolean constant.
  void process_eq();
  /// Fold binary less-than comparison into a 1-bit boolean constant.
  void process_lt();
  /// Fold binary less-than-or-equal comparison into a 1-bit boolean constant.
  void process_le();
  /// Fold binary greater-than comparison into a 1-bit boolean constant.
  void process_gt();
  /// Fold binary greater-than-or-equal comparison into a 1-bit boolean constant.
  void process_ge();
  /// Conservatively traverse if-node children; dead-branch elimination is not yet implemented.
  void process_if();

  /// Reduce-OR: fold to 1 if any bit in input is set, else 0.
  void process_red_or();
  /// Reduce-AND: fold to 1 only if all bits are set (input is all-ones mask).
  void process_red_and();
  /// Reduce-XOR: fold to the parity (1 if popcount is odd) of the input.
  void process_red_xor();

  /// Sign-extend src to nbits and store result; skips if src or nbits are unknown.
  void process_sext();

  /// No-op hook called on stmts nodes; traversal is handled by the runner.
  void process_stmts();

  /// Build or update a Bundle for dst from each positional or named field child.
  void process_tuple_add();
  /// Read dst = src[field]: propagate a known scalar or sub-bundle into dst.
  void process_tuple_get();
  /// Update a single field inside an existing tuple bundle; skips __bits/__signed annotations.
  void process_tuple_set();
  /// Concatenate lhs and rhs bundles into dst using Bundle::concat(); leaves dst unknown if either side is unknown.
  void process_tuple_concat();

  /// Store an attribute value (tuple.attr = value); skips __bits/__signed annotations to avoid non-convergence.
  void process_attr_set();
  /// Read an attribute value (dst = tuple.attr) and propagate it into the symbol table.
  void process_attr_get();

  /// No-op: cross-cycle register values are not statically foldable; dst is left unknown.
  void process_delay_assign();

protected:
  Symbol_table st;

  /// Return the bundle currently tracked for the node under the cursor, or nullptr.
  auto current_bundle() { return st.get_bundle(current_text()); }

  /// Parse the current node's text as a Pyrope literal and return its Lconst value.
  auto current_pyrope_value() { return Lconst::from_pyrope(current_text()); }

  /// Return the constant value of the current node: looks up the symbol table for
  /// refs, or parses the literal text for consts.  Returns Lconst::invalid() if unknown.
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
