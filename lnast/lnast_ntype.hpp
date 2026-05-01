//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Static-only utility over the Lnast node-type enum. The enum value is stored
// directly in the HHDS tree slot via Lnast::set_type/get_type. Detached
// Lnast_node values only represent ref/const/invalid leaves; structural nodes
// should use this raw type value directly.
class Lnast_ntype {
public:
  enum Lnast_ntype_int : uint8_t {
#define LNAST_NODE(NAME, VERBAL) Lnast_ntype_##NAME,
#include "lnast_nodes.def"
    Lnast_ntype_last_invalid
  };

private:
  constexpr static std::array namemap{
#define LNAST_NODE(NAME, VERBAL) #VERBAL,
#include "lnast_nodes.def"
  };

public:
  Lnast_ntype()                              = delete;
  Lnast_ntype(const Lnast_ntype&)            = delete;
  Lnast_ntype& operator=(const Lnast_ntype&) = delete;

  // Per-type creator + predicate.
#define LNAST_NODE(NAME, VERBAL)                                                  \
  static constexpr Lnast_ntype_int create_##NAME() { return Lnast_ntype_##NAME; } \
  static constexpr bool            is_##NAME(Lnast_ntype_int v) { return v == Lnast_ntype_##NAME; }
#include "lnast_nodes.def"

  static constexpr bool is_tuple_attr(Lnast_ntype_int v) {
    return v == Lnast_ntype_tuple_add || v == Lnast_ntype_tuple_get || v == Lnast_ntype_tuple_set || v == Lnast_ntype_attr_set
           || v == Lnast_ntype_attr_get;
  }

  // Super types
  static constexpr bool is_primitive_op(Lnast_ntype_int v) { return v >= Lnast_ntype_assign && v <= Lnast_ntype_attr_get; }
  static constexpr bool is_logical_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_log_and || v == Lnast_ntype_log_or || v == Lnast_ntype_log_not;
  }
  static constexpr bool is_unary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_bit_not || v == Lnast_ntype_log_not || v == Lnast_ntype_assign || v == Lnast_ntype_dp_assign;
  }
  static constexpr bool is_binary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_shl || v == Lnast_ntype_sra || v == Lnast_ntype_sext;
  }
  static constexpr bool is_nary_op(Lnast_ntype_int v) {
    return v == Lnast_ntype_bit_and || v == Lnast_ntype_bit_or || v == Lnast_ntype_bit_xor || v == Lnast_ntype_log_and
           || v == Lnast_ntype_log_or || v == Lnast_ntype_plus || v == Lnast_ntype_minus || v == Lnast_ntype_mult
           || v == Lnast_ntype_is || v == Lnast_ntype_eq || v == Lnast_ntype_ne || v == Lnast_ntype_lt || v == Lnast_ntype_le
           || v == Lnast_ntype_gt || v == Lnast_ntype_ge;
  }

  // basic_op have 1-to-1 translation between LNAST and Lgraph
  static constexpr bool is_direct_lgraph_op(Lnast_ntype_int v) {
    return (v >= Lnast_ntype_bit_and && v <= Lnast_ntype_ge) && v != Lnast_ntype_mod && v != Lnast_ntype_is && v != Lnast_ntype_ne
           && v != Lnast_ntype_le && v != Lnast_ntype_ge;
  }

  static constexpr bool is_type(Lnast_ntype_int v) { return v >= Lnast_ntype_none_type && v <= Lnast_ntype_unknown_type; }

  static std::string_view to_sv(Lnast_ntype_int v) { return namemap[v]; }
  static std::string_view debug_name(Lnast_ntype_int v) { return namemap[v]; }

  static_assert(namemap.size() == Lnast_ntype_last_invalid);
};
