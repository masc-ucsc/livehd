//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <string_view>

class Lnast_ntype {
public:
  enum Lnast_ntype_int : uint8_t {
    Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype

    // group: tree structure
    Lnast_ntype_top,
    Lnast_ntype_stmts,  // stmts
    Lnast_ntype_if,
    Lnast_ntype_uif,    // unique IF
    Lnast_ntype_for,
    Lnast_ntype_while,
    Lnast_ntype_func_call,  // .()
    Lnast_ntype_func_def,   // ::{   func_def = sub-graph in lgraph

    // assign related
    Lnast_ntype_assign,     //  =
    Lnast_ntype_dp_assign,  // :=
    Lnast_ntype_mut,        // mut a = 3

    // bitwidth ops
    Lnast_ntype_bit_and,  // &
    Lnast_ntype_bit_or,   // |
    Lnast_ntype_bit_not,  // ~
    Lnast_ntype_bit_xor,  // ^

    // Only bitwidth insensitive reduce
    Lnast_ntype_reduce_or,  // ror

    // Logical ops
    Lnast_ntype_logical_and,  // and
    Lnast_ntype_logical_or,   // or
    Lnast_ntype_logical_not,  // !

    // arithmetic ops
    Lnast_ntype_plus,
    Lnast_ntype_minus,
    Lnast_ntype_mult,
    Lnast_ntype_div,
    Lnast_ntype_mod,

    // shifts
    Lnast_ntype_shl,  // << or <<<
    // NO SHR because it is bitwidth sensitive
    Lnast_ntype_sra,  // >>>

    // bit manipulation (zero/sign extend)
    Lnast_ntype_sext,  // sext(wire, bit) == chop wire/tuple to have sbits == bit, and bit pos is the sign
    Lnast_ntype_set_mask,

    // reduce ops with mask (ror does not need mask)
    // These reduce OPS use a bit and a mask (different from LGraph or reduce which has no mask)
    Lnast_ntype_get_mask,  // zext
    Lnast_ntype_mask_and,
    Lnast_ntype_mask_popcount,
    Lnast_ntype_mask_xor,

    // Comparators
    Lnast_ntype_is,
    Lnast_ntype_ne,
    Lnast_ntype_eq,
    Lnast_ntype_lt,
    Lnast_ntype_le,
    Lnast_ntype_gt,
    Lnast_ntype_ge,

    // group: language variable
    Lnast_ntype_ref,
    Lnast_ntype_const,

    // Tuple ops
    Lnast_ntype_tuple_concat,  // ++
    Lnast_ntype_tuple_add,
    Lnast_ntype_tuple_get,
    Lnast_ntype_tuple_set,

    // group: compiler internal type
    // DO NOT GENERATE THIS from IO passes
    Lnast_ntype_attr_set,
    Lnast_ntype_attr_get,

    Lnast_ntype_err_flag,  // compile error flag
    Lnast_ntype_phi,
    Lnast_ntype_hot_phi,

    Lnast_ntype_last_invalid
  };

protected:
  constexpr static std::array namemap{
      "invalid",
      // group: tree structure
      "top",
      "stmts",
      "if",
      "uif",
      "for",
      "while",
      "fcall",
      "fdef",

      "assign",
      "dp_assign",
      "mut",

      "and",
      "or",
      "not",
      "xor",

      "ror",

      "land",
      "lor",
      "lnot",

      "plus",
      "minus",
      "mult",
      "div",
      "mod",

      "shl",
      "sra",

      "sext",
      "set_mask",

      "get_mask",
      "mask_and",
      "mask_popcount",
      "mask_xor",

      "is",
      "ne",
      "eq",
      "lt",
      "le",
      "gt",
      "ge",

      "ref",
      "const",

      "tuple_concat",
      "tuple_add",
      "tuple_get",
      "tuple_set",

      "attr_set",
      "attr_get",

      "error_flag",
      "phi",
      "hot_phi"
  };

  Lnast_ntype_int val;
  constexpr explicit Lnast_ntype(Lnast_ntype_int _val) : val(_val) {}

public:
  constexpr Lnast_ntype() : val(Lnast_ntype_invalid) {}

  mmap_lib::str to_str() const { return mmap_lib::str(namemap[val]); }

  Lnast_ntype_int get_raw_ntype() const { return val; }

  static constexpr Lnast_ntype create_invalid() { return Lnast_ntype(Lnast_ntype_invalid); }
  static constexpr Lnast_ntype create_top() { return Lnast_ntype(Lnast_ntype_top); }

  static constexpr Lnast_ntype create_stmts() { return Lnast_ntype(Lnast_ntype_stmts); }
  static constexpr Lnast_ntype create_if() { return Lnast_ntype(Lnast_ntype_if); }
  static constexpr Lnast_ntype create_uif() { return Lnast_ntype(Lnast_ntype_uif); }
  static constexpr Lnast_ntype create_for() { return Lnast_ntype(Lnast_ntype_for); }
  static constexpr Lnast_ntype create_while() { return Lnast_ntype(Lnast_ntype_while); }
  static constexpr Lnast_ntype create_func_call() { return Lnast_ntype(Lnast_ntype_func_call); }
  static constexpr Lnast_ntype create_func_def() { return Lnast_ntype(Lnast_ntype_func_def); }

  static constexpr Lnast_ntype create_assign() { return Lnast_ntype(Lnast_ntype_assign); }
  static constexpr Lnast_ntype create_dp_assign() { return Lnast_ntype(Lnast_ntype_dp_assign); }
  static constexpr Lnast_ntype create_mut() { return Lnast_ntype(Lnast_ntype_mut); }

  static constexpr Lnast_ntype create_bit_and() { return Lnast_ntype(Lnast_ntype_bit_and); }
  static constexpr Lnast_ntype create_bit_or() { return Lnast_ntype(Lnast_ntype_bit_or); }
  static constexpr Lnast_ntype create_bit_not() { return Lnast_ntype(Lnast_ntype_bit_not); }
  static constexpr Lnast_ntype create_bit_xor() { return Lnast_ntype(Lnast_ntype_bit_xor); }

  static constexpr Lnast_ntype create_reduce_or() { return Lnast_ntype(Lnast_ntype_reduce_or); }

  static constexpr Lnast_ntype create_logical_and() { return Lnast_ntype(Lnast_ntype_logical_and); }
  static constexpr Lnast_ntype create_logical_or() { return Lnast_ntype(Lnast_ntype_logical_or); }
  static constexpr Lnast_ntype create_logical_not() { return Lnast_ntype(Lnast_ntype_logical_not); }

  static constexpr Lnast_ntype create_plus() { return Lnast_ntype(Lnast_ntype_plus); }
  static constexpr Lnast_ntype create_minus() { return Lnast_ntype(Lnast_ntype_minus); }
  static constexpr Lnast_ntype create_mult() { return Lnast_ntype(Lnast_ntype_mult); }
  static constexpr Lnast_ntype create_div() { return Lnast_ntype(Lnast_ntype_div); }
  static constexpr Lnast_ntype create_mod() { return Lnast_ntype(Lnast_ntype_mod); }

  static constexpr Lnast_ntype create_shl() { return Lnast_ntype(Lnast_ntype_shl); }
  static constexpr Lnast_ntype create_sra() { return Lnast_ntype(Lnast_ntype_sra); }

  static constexpr Lnast_ntype create_sext() { return Lnast_ntype(Lnast_ntype_sext); }
  static constexpr Lnast_ntype create_set_mask() { return Lnast_ntype(Lnast_ntype_set_mask); }

  static constexpr Lnast_ntype create_get_mask() { return Lnast_ntype(Lnast_ntype_get_mask); }
  static constexpr Lnast_ntype create_mask_and() { return Lnast_ntype(Lnast_ntype_mask_and); }
  static constexpr Lnast_ntype create_mask_popcount() { return Lnast_ntype(Lnast_ntype_mask_popcount); }
  static constexpr Lnast_ntype create_mask_xor() { return Lnast_ntype(Lnast_ntype_mask_xor); }

  static constexpr Lnast_ntype create_is() { return Lnast_ntype(Lnast_ntype_is); }
  static constexpr Lnast_ntype create_ne() { return Lnast_ntype(Lnast_ntype_ne); }
  static constexpr Lnast_ntype create_eq() { return Lnast_ntype(Lnast_ntype_eq); }
  static constexpr Lnast_ntype create_lt() { return Lnast_ntype(Lnast_ntype_lt); }
  static constexpr Lnast_ntype create_le() { return Lnast_ntype(Lnast_ntype_le); }
  static constexpr Lnast_ntype create_gt() { return Lnast_ntype(Lnast_ntype_gt); }
  static constexpr Lnast_ntype create_ge() { return Lnast_ntype(Lnast_ntype_ge); }

  static constexpr Lnast_ntype create_ref() { return Lnast_ntype(Lnast_ntype_ref); }
  static constexpr Lnast_ntype create_const() { return Lnast_ntype(Lnast_ntype_const); }

  static constexpr Lnast_ntype create_tuple_concat() { return Lnast_ntype(Lnast_ntype_tuple_concat); }
  static constexpr Lnast_ntype create_tuple_add() { return Lnast_ntype(Lnast_ntype_tuple_add); }
  static constexpr Lnast_ntype create_tuple_get() { return Lnast_ntype(Lnast_ntype_tuple_get); }
  static constexpr Lnast_ntype create_tuple_set() { return Lnast_ntype(Lnast_ntype_tuple_set); }

  static constexpr Lnast_ntype create_attr_set() { return Lnast_ntype(Lnast_ntype_attr_set); }
  static constexpr Lnast_ntype create_attr_get() { return Lnast_ntype(Lnast_ntype_attr_get); }

  static constexpr Lnast_ntype create_err_flag() { return Lnast_ntype(Lnast_ntype_err_flag); }
  static constexpr Lnast_ntype create_phi() { return Lnast_ntype(Lnast_ntype_phi); }
  static constexpr Lnast_ntype create_hot_phi() { return Lnast_ntype(Lnast_ntype_hot_phi); }

  bool constexpr is_invalid() const { return val == Lnast_ntype_invalid; }

  bool constexpr is_top() const { return val == Lnast_ntype_top; }
  bool constexpr is_stmts() const { return val == Lnast_ntype_stmts; }
  bool constexpr is_if() const { return val == Lnast_ntype_if; }
  bool constexpr is_uif() const { return val == Lnast_ntype_uif; }
  bool constexpr is_for() const { return val == Lnast_ntype_for; }
  bool constexpr is_while() const { return val == Lnast_ntype_while; }
  bool constexpr is_func_call() const { return val == Lnast_ntype_func_call; }
  bool constexpr is_func_def() const { return val == Lnast_ntype_func_def; }

  bool constexpr is_assign() const { return val == Lnast_ntype_assign; }
  bool constexpr is_dp_assign() const { return val == Lnast_ntype_dp_assign; }
  bool constexpr is_mut() const { return val == Lnast_ntype_mut; }

  bool constexpr is_bit_and() const { return val == Lnast_ntype_bit_and; }
  bool constexpr is_bit_or() const { return val == Lnast_ntype_bit_or; }
  bool constexpr is_bit_not() const { return val == Lnast_ntype_bit_not; }
  bool constexpr is_bit_xor() const { return val == Lnast_ntype_bit_xor; }

  bool constexpr is_reduce_or() const { return val == Lnast_ntype_reduce_or; }

  bool constexpr is_logical_and() const { return val == Lnast_ntype_logical_and; }
  bool constexpr is_logical_or() const { return val == Lnast_ntype_logical_or; }
  bool constexpr is_logical_not() const { return val == Lnast_ntype_logical_not; }

  bool constexpr is_plus() const { return val == Lnast_ntype_plus; }
  bool constexpr is_minus() const { return val == Lnast_ntype_minus; }
  bool constexpr is_mult() const { return val == Lnast_ntype_mult; }
  bool constexpr is_div() const { return val == Lnast_ntype_div; }
  bool constexpr is_mod() const { return val == Lnast_ntype_mod; }

  bool constexpr is_shl() const { return val == Lnast_ntype_shl; }
  bool constexpr is_sra() const { return val == Lnast_ntype_sra; }

  bool constexpr is_sext() const { return val == Lnast_ntype_sext; }
  bool constexpr is_set_mask() const { return val == Lnast_ntype_set_mask; }

  bool constexpr is_get_mask() const { return val == Lnast_ntype_get_mask; }
  bool constexpr is_mask_and() const { return val == Lnast_ntype_mask_and; }
  bool constexpr is_mask_popcount() const { return val == Lnast_ntype_mask_popcount; }
  bool constexpr is_mask_xor() const { return val == Lnast_ntype_mask_xor; }

  bool constexpr is_is() const { return val == Lnast_ntype_is; }
  bool constexpr is_ne() const { return val == Lnast_ntype_ne; }
  bool constexpr is_eq() const { return val == Lnast_ntype_eq; }
  bool constexpr is_lt() const { return val == Lnast_ntype_lt; }
  bool constexpr is_le() const { return val == Lnast_ntype_le; }
  bool constexpr is_gt() const { return val == Lnast_ntype_gt; }
  bool constexpr is_ge() const { return val == Lnast_ntype_ge; }


  bool constexpr is_ref() const { return val == Lnast_ntype_ref; }
  bool constexpr is_const() const { return val == Lnast_ntype_const; }

  bool constexpr is_tuple_concat() const { return val == Lnast_ntype_tuple_concat; }
  bool constexpr is_tuple_add() const { return val == Lnast_ntype_tuple_add; }
  bool constexpr is_tuple_get() const { return val == Lnast_ntype_tuple_get; }
  bool constexpr is_tuple_set() const { return val == Lnast_ntype_tuple_set; }

  bool constexpr is_tuple_attr() const {
    return val == Lnast_ntype_tuple_add || val == Lnast_ntype_tuple_get || val == Lnast_ntype_tuple_set || val == Lnast_ntype_attr_set
           || val == Lnast_ntype_attr_get || val == Lnast_ntype_tuple_set;
  }

  bool constexpr is_attr_set() const { return val == Lnast_ntype_attr_set; }
  bool constexpr is_attr_get() const { return val == Lnast_ntype_attr_get; }

  bool constexpr is_err_flag() const { return val == Lnast_ntype_err_flag; }
  bool constexpr is_phi() const { return val == Lnast_ntype_phi; }
  bool constexpr is_hot_phi() const { return val == Lnast_ntype_hot_phi; }

  // Super types
  bool constexpr is_primitive_op() const { return (val >= Lnast_ntype_assign && val <= Lnast_ntype_attr_get); }
  bool constexpr is_logical_op() const {
    return (val == Lnast_ntype_logical_and) || (val == Lnast_ntype_logical_or) || (val == Lnast_ntype_logical_not);
  }

  bool constexpr is_mask_op() const { return val >= Lnast_ntype_mask_and && val <= Lnast_ntype_mask_xor; }

  bool constexpr is_unary_op() const {
    return (val == Lnast_ntype_bit_not) || (val == Lnast_ntype_logical_not) || (val == Lnast_ntype_assign)
           || (val == Lnast_ntype_dp_assign) || (val == Lnast_ntype_mut);
  }

  bool constexpr is_binary_op() const {
    return (val == Lnast_ntype_shl) || (val == Lnast_ntype_sra) || (val == Lnast_ntype_sext) || is_mask_op();
  }

  bool constexpr is_nary_op() const {
    return (val == Lnast_ntype_bit_and) || (val == Lnast_ntype_bit_or) || (val == Lnast_ntype_bit_xor)
           || (val == Lnast_ntype_logical_and) || (val == Lnast_ntype_logical_or) || (val == Lnast_ntype_plus)
           || (val == Lnast_ntype_minus)
           || (val == Lnast_ntype_mult) || (val == Lnast_ntype_is) || (val == Lnast_ntype_eq) || (val == Lnast_ntype_ne)
           || (val == Lnast_ntype_lt) || (val == Lnast_ntype_le) || (val == Lnast_ntype_gt) || (val == Lnast_ntype_ge);
  }

  // basic_op have 1 to 1 translation between LNAST and Lgraph
  bool constexpr is_direct_lgraph_op() const {
    return (val >= Lnast_ntype_bit_and && val <= Lnast_ntype_ge) && !is_mask_op()
           && val != Lnast_ntype_mod
           && val != Lnast_ntype_is && val != Lnast_ntype_ne && val != Lnast_ntype_le && val != Lnast_ntype_ge;
  }

  std::string_view debug_name() const { return namemap[val]; }

  static_assert(namemap.size() == Lnast_ntype_last_invalid);
};
