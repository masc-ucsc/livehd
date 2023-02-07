//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <string_view>

class Lnast_ntype {
public:
  enum Lnast_ntype_int : uint8_t {
#define LNAST_NODE(NAME, VERBAL) Lnast_ntype_##NAME,
#include "lnast_nodes.def"
    Lnast_ntype_last_invalid
  };

protected:
  constexpr static std::array namemap{
#define LNAST_NODE(NAME, VERBAL) #VERBAL,
#include "lnast_nodes.def"
  };

  Lnast_ntype_int val = Lnast_ntype_int::Lnast_ntype_invalid;

public:
  constexpr explicit Lnast_ntype() = default;
  constexpr explicit Lnast_ntype(Lnast_ntype_int _val) : val(_val) {}

  [[nodiscard]] std::string_view to_sv() const { return namemap[val]; }

  [[nodiscard]] Lnast_ntype_int get_raw_ntype() const { return val; }

#define LNAST_NODE(NAME, VERBAL)                                                           \
  static constexpr Lnast_ntype create_##NAME() { return Lnast_ntype(Lnast_ntype_##NAME); } \
  bool constexpr is_##NAME() const { return val == Lnast_ntype_##NAME; }
#include "lnast_nodes.def"

  bool constexpr is_tuple_attr() const {
    return val == Lnast_ntype_tuple_add || val == Lnast_ntype_tuple_get || val == Lnast_ntype_tuple_set
           || val == Lnast_ntype_attr_set || val == Lnast_ntype_attr_get || val == Lnast_ntype_tuple_set;
  }

  // Super types
  bool constexpr is_primitive_op() const { return (val >= Lnast_ntype_assign && val <= Lnast_ntype_attr_get); }
  bool constexpr is_logical_op() const {
    return (val == Lnast_ntype_log_and) || (val == Lnast_ntype_log_or) || (val == Lnast_ntype_log_not);
  }

  bool constexpr is_mask_op() const { return val >= Lnast_ntype_mask_and && val <= Lnast_ntype_mask_xor; }

  bool constexpr is_unary_op() const {
    return (val == Lnast_ntype_bit_not) || (val == Lnast_ntype_log_not) || (val == Lnast_ntype_assign)
           || (val == Lnast_ntype_dp_assign) || (val == Lnast_ntype_mut);
  }

  bool constexpr is_binary_op() const {
    return (val == Lnast_ntype_shl) || (val == Lnast_ntype_sra) || (val == Lnast_ntype_sext) || is_mask_op();
  }

  bool constexpr is_nary_op() const {
    return (val == Lnast_ntype_bit_and) || (val == Lnast_ntype_bit_or) || (val == Lnast_ntype_bit_xor)
           || (val == Lnast_ntype_log_and) || (val == Lnast_ntype_log_or) || (val == Lnast_ntype_plus)
           || (val == Lnast_ntype_minus) || (val == Lnast_ntype_mult) || (val == Lnast_ntype_is) || (val == Lnast_ntype_eq)
           || (val == Lnast_ntype_ne) || (val == Lnast_ntype_lt) || (val == Lnast_ntype_le) || (val == Lnast_ntype_gt)
           || (val == Lnast_ntype_ge);
  }

  // basic_op have 1 to 1 translation between LNAST and Lgraph
  bool constexpr is_direct_lgraph_op() const {
    return (val >= Lnast_ntype_bit_and && val <= Lnast_ntype_ge) && !is_mask_op() && val != Lnast_ntype_mod && val != Lnast_ntype_is
           && val != Lnast_ntype_ne && val != Lnast_ntype_le && val != Lnast_ntype_ge;
  }

  bool constexpr is_type() const { return (val >= Lnast_ntype_none_type && val <= Lnast_ntype_unknown_type); }

  std::string_view debug_name() const { return namemap[val]; }

  static std::string_view debug_name(Lnast_ntype_int val) { return namemap[val]; }

  static_assert(namemap.size() == Lnast_ntype_last_invalid);
};
