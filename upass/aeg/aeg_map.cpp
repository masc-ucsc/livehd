//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "aeg_map.hpp"

namespace upass::aeg {

using N = Lnast_ntype;

bool is_mapped_op(Lnast_ntype::Lnast_ntype_int t) {
  switch (t) {
    case N::Lnast_ntype_plus:
    case N::Lnast_ntype_minus:
    case N::Lnast_ntype_mult:
    case N::Lnast_ntype_div:
    case N::Lnast_ntype_bit_and:
    case N::Lnast_ntype_bit_or:
    case N::Lnast_ntype_bit_xor:
    case N::Lnast_ntype_log_and:
    case N::Lnast_ntype_log_or:
    case N::Lnast_ntype_log_not:
    case N::Lnast_ntype_bit_not:
    case N::Lnast_ntype_eq:
    case N::Lnast_ntype_ne:
    case N::Lnast_ntype_lt:
    case N::Lnast_ntype_le:
    case N::Lnast_ntype_gt:
    case N::Lnast_ntype_ge:
    case N::Lnast_ntype_shl:
    case N::Lnast_ntype_sra:
    case N::Lnast_ntype_get_mask:
    case N::Lnast_ntype_sext:
    case N::Lnast_ntype_red_or: return true;
    default                    : return false;
  }
}

bool pure_temp_op(Lnast_ntype::Lnast_ntype_int t) {
  return N::is_plus(t) || N::is_minus(t) || N::is_mult(t) || N::is_shl(t) || N::is_sra(t) || N::is_range(t) || N::is_bit_and(t)
         || N::is_bit_or(t) || N::is_bit_xor(t) || N::is_bit_not(t) || N::is_get_mask(t) || N::is_sext(t) || N::is_type_spec(t);
}

bool is_compare(Lnast_ntype::Lnast_ntype_int t) {
  return N::is_eq(t) || N::is_ne(t) || N::is_lt(t) || N::is_le(t) || N::is_gt(t) || N::is_ge(t);
}

Kind result_kind(Lnast_ntype::Lnast_ntype_int t, std::span<const Kind> operand_kinds) {
  if (is_compare(t) || N::is_log_not(t)) {
    return Kind::boolean;
  }
  if (N::is_plus(t) || N::is_minus(t) || N::is_mult(t) || N::is_div(t) || N::is_shl(t) || N::is_sra(t) || N::is_get_mask(t)
      || N::is_sext(t) || N::is_bit_not(t) || N::is_red_or(t)) {
    return Kind::integer;
  }
  if (N::is_bit_and(t) || N::is_bit_or(t) || N::is_bit_xor(t) || N::is_log_and(t) || N::is_log_or(t)) {
    if (operand_kinds.empty()) {
      return Kind::unknown;
    }
    Kind k = operand_kinds.front();
    for (const auto o : operand_kinds) {
      k = meet(k, o);
    }
    return k;
  }
  return Kind::unknown;
}

Ntype_op chain_op(Lnast_ntype::Lnast_ntype_int t) {
  if (N::is_plus(t) || N::is_minus(t)) {
    return Ntype_op::Sum;
  }
  if (N::is_mult(t)) {
    return Ntype_op::Mult;
  }
  if (N::is_bit_and(t) || N::is_log_and(t)) {
    return Ntype_op::And;
  }
  if (N::is_bit_or(t) || N::is_log_or(t)) {
    return Ntype_op::Or;
  }
  if (N::is_bit_xor(t)) {
    return Ntype_op::Xor;
  }
  return Ntype_op::Invalid;
}

}  // namespace upass::aeg
