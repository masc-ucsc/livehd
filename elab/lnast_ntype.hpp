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
    Lnast_ntype_uif,
    Lnast_ntype_for,
    Lnast_ntype_while,
    Lnast_ntype_phi,
    Lnast_ntype_hot_phi,
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

    // Logical ops
    Lnast_ntype_logical_and,  // and
    Lnast_ntype_logical_or,   // or
    Lnast_ntype_logical_not,  // !

    // reduce ops
    // NO reduce AND because the operation is bitwidth sensitive
    Lnast_ntype_reduce_or,
    Lnast_ntype_reduce_xor,

    // arithmetic ops
    Lnast_ntype_plus,
    Lnast_ntype_minus,
    Lnast_ntype_mult,
    Lnast_ntype_div,
    Lnast_ntype_mod,

    // shifts
    Lnast_ntype_shr,  // >>
    Lnast_ntype_shl,  // << or <<<
    Lnast_ntype_sra,  // >>>

    // zero/sign extend (becomes Cell:And, Cell:sext, Cell:tposs)
    Lnast_ntype_sext,  // sext(wire, bit) == chop wire/tuple to have sbits == bit, and bit pos is the sign
    Lnast_ntype_set_mask,
    Lnast_ntype_get_mask,

    // Comparators
    Lnast_ntype_is,
    Lnast_ntype_ne,
    Lnast_ntype_eq,
    Lnast_ntype_lt,
    Lnast_ntype_le,
    Lnast_ntype_gt,
    Lnast_ntype_ge,

    // Tuple ops
    Lnast_ntype_tuple,         // ()
    Lnast_ntype_tuple_concat,  // ++
    Lnast_ntype_tuple_delete,  // --
    Lnast_ntype_select,

    // group: language variable
    Lnast_ntype_ref,
    Lnast_ntype_const,

    // group: others
    Lnast_ntype_assert,    // I
    Lnast_ntype_err_flag,  // compile error flag

    // group: compiler internal type
    // TODO: If the parsers generate this directly (possible) instead of
    // select, we can avoid 2 LNAST traversals (faster compilation)
    Lnast_ntype_tuple_add,
    Lnast_ntype_tuple_get,
    Lnast_ntype_attr_set,
    Lnast_ntype_attr_get,

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
      "phi",
      "hot_phi",
      "fcall",
      "fdef",

      "assign",
      "dp_assign",
      "mut",

      "and",
      "or",
      "not",
      "xor",

      "land",
      "lor",
      "lnot",

      "ror",
      "rxor",

      "plus",
      "minus",
      "mult",
      "div",
      "mod",

      "shl",
      "shr",
      "sra",

      "sext",
      "set_mask",
      "get_mask",

      "is",
      "ne",
      "eq",
      "lt",
      "le",
      "gt",
      "ge",

      "tuple",
      "tuple_concat",
      "tuple_delete",
      "select",

      // group: language variable
      "ref",
      "const",

      // group: others
      "assert",
      "error_flag",

      // group: compiler internal type (generated out of select)
      "tuple_add",
      "tuple_get",
      "attr_set",  // tuple_add with __foo
      "attr_get"   // tuple_get with __foo

  };

  Lnast_ntype_int val;
  constexpr explicit Lnast_ntype(Lnast_ntype_int _val) : val(_val) {}

public:
  constexpr Lnast_ntype() : val(Lnast_ntype_invalid) {}

  std::string_view to_s() const { return namemap[val]; }

  Lnast_ntype_int get_raw_ntype() const { return val; }

  static constexpr Lnast_ntype create_invalid() { return Lnast_ntype(Lnast_ntype_invalid); }
  static constexpr Lnast_ntype create_top() { return Lnast_ntype(Lnast_ntype_top); }

  static constexpr Lnast_ntype create_stmts() { return Lnast_ntype(Lnast_ntype_stmts); }
  static constexpr Lnast_ntype create_if() { return Lnast_ntype(Lnast_ntype_if); }
  static constexpr Lnast_ntype create_uif() { return Lnast_ntype(Lnast_ntype_uif); }
  static constexpr Lnast_ntype create_for() { return Lnast_ntype(Lnast_ntype_for); }
  static constexpr Lnast_ntype create_while() { return Lnast_ntype(Lnast_ntype_while); }
  static constexpr Lnast_ntype create_phi() { return Lnast_ntype(Lnast_ntype_phi); }
  static constexpr Lnast_ntype create_hot_phi() { return Lnast_ntype(Lnast_ntype_hot_phi); }
  static constexpr Lnast_ntype create_func_call() { return Lnast_ntype(Lnast_ntype_func_call); }
  static constexpr Lnast_ntype create_func_def() { return Lnast_ntype(Lnast_ntype_func_def); }

  static constexpr Lnast_ntype create_assign() { return Lnast_ntype(Lnast_ntype_assign); }
  static constexpr Lnast_ntype create_dp_assign() { return Lnast_ntype(Lnast_ntype_dp_assign); }
  static constexpr Lnast_ntype create_mut() { return Lnast_ntype(Lnast_ntype_mut); }

  static constexpr Lnast_ntype create_bit_and() { return Lnast_ntype(Lnast_ntype_bit_and); }
  static constexpr Lnast_ntype create_bit_or() { return Lnast_ntype(Lnast_ntype_bit_or); }
  static constexpr Lnast_ntype create_bit_not() { return Lnast_ntype(Lnast_ntype_bit_not); }
  static constexpr Lnast_ntype create_bit_xor() { return Lnast_ntype(Lnast_ntype_bit_xor); }

  static constexpr Lnast_ntype create_logical_and() { return Lnast_ntype(Lnast_ntype_logical_and); }
  static constexpr Lnast_ntype create_logical_or() { return Lnast_ntype(Lnast_ntype_logical_or); }
  static constexpr Lnast_ntype create_logical_not() { return Lnast_ntype(Lnast_ntype_logical_not); }

  static constexpr Lnast_ntype create_reduce_or() { return Lnast_ntype(Lnast_ntype_reduce_or); }
  static constexpr Lnast_ntype create_reduce_xor() { return Lnast_ntype(Lnast_ntype_reduce_xor); }

  static constexpr Lnast_ntype create_plus() { return Lnast_ntype(Lnast_ntype_plus); }
  static constexpr Lnast_ntype create_minus() { return Lnast_ntype(Lnast_ntype_minus); }
  static constexpr Lnast_ntype create_mult() { return Lnast_ntype(Lnast_ntype_mult); }
  static constexpr Lnast_ntype create_div() { return Lnast_ntype(Lnast_ntype_div); }
  static constexpr Lnast_ntype create_mod() { return Lnast_ntype(Lnast_ntype_mod); }

  static constexpr Lnast_ntype create_shl() { return Lnast_ntype(Lnast_ntype_shl); }
  static constexpr Lnast_ntype create_shr() { return Lnast_ntype(Lnast_ntype_shr); }
  static constexpr Lnast_ntype create_sra() { return Lnast_ntype(Lnast_ntype_sra); }

  static constexpr Lnast_ntype create_sext() { return Lnast_ntype(Lnast_ntype_sext); }
  static constexpr Lnast_ntype create_set_mask() { return Lnast_ntype(Lnast_ntype_set_mask); }
  static constexpr Lnast_ntype create_get_mask() { return Lnast_ntype(Lnast_ntype_get_mask); }

  static constexpr Lnast_ntype create_is() { return Lnast_ntype(Lnast_ntype_is); }
  static constexpr Lnast_ntype create_ne() { return Lnast_ntype(Lnast_ntype_ne); }
  static constexpr Lnast_ntype create_eq() { return Lnast_ntype(Lnast_ntype_eq); }
  static constexpr Lnast_ntype create_lt() { return Lnast_ntype(Lnast_ntype_lt); }
  static constexpr Lnast_ntype create_le() { return Lnast_ntype(Lnast_ntype_le); }
  static constexpr Lnast_ntype create_gt() { return Lnast_ntype(Lnast_ntype_gt); }
  static constexpr Lnast_ntype create_ge() { return Lnast_ntype(Lnast_ntype_ge); }

  static constexpr Lnast_ntype create_tuple() { return Lnast_ntype(Lnast_ntype_tuple); }
  static constexpr Lnast_ntype create_tuple_concat() { return Lnast_ntype(Lnast_ntype_tuple_concat); }
  static constexpr Lnast_ntype create_tuple_delete() { return Lnast_ntype(Lnast_ntype_tuple_delete); }
  static constexpr Lnast_ntype create_select() { return Lnast_ntype(Lnast_ntype_select); }

  static constexpr Lnast_ntype create_ref() { return Lnast_ntype(Lnast_ntype_ref); }
  static constexpr Lnast_ntype create_const() { return Lnast_ntype(Lnast_ntype_const); }

  static constexpr Lnast_ntype create_assert() { return Lnast_ntype(Lnast_ntype_assert); }
  static constexpr Lnast_ntype create_err_flag() { return Lnast_ntype(Lnast_ntype_err_flag); }

  static constexpr Lnast_ntype create_tuple_add() { return Lnast_ntype(Lnast_ntype_tuple_add); }
  static constexpr Lnast_ntype create_tuple_get() { return Lnast_ntype(Lnast_ntype_tuple_get); }
  static constexpr Lnast_ntype create_attr_set() { return Lnast_ntype(Lnast_ntype_attr_set); }
  static constexpr Lnast_ntype create_attr_get() { return Lnast_ntype(Lnast_ntype_attr_get); }

  bool constexpr is_invalid() const { return val == Lnast_ntype_invalid; }

  bool constexpr is_top() const { return val == Lnast_ntype_top; }
  bool constexpr is_stmts() const { return val == Lnast_ntype_stmts; }
  bool constexpr is_if() const { return val == Lnast_ntype_if; }
  bool constexpr is_uif() const { return val == Lnast_ntype_uif; }
  bool constexpr is_for() const { return val == Lnast_ntype_for; }
  bool constexpr is_while() const { return val == Lnast_ntype_while; }
  bool constexpr is_phi() const { return val == Lnast_ntype_phi; }
  bool constexpr is_hot_phi() const { return val == Lnast_ntype_hot_phi; }
  bool constexpr is_func_call() const { return val == Lnast_ntype_func_call; }
  bool constexpr is_func_def() const { return val == Lnast_ntype_func_def; }

  bool constexpr is_assign() const { return val == Lnast_ntype_assign; }
  bool constexpr is_dp_assign() const { return val == Lnast_ntype_dp_assign; }
  bool constexpr is_mut() const { return val == Lnast_ntype_mut; }

  bool constexpr is_bit_and() const { return val == Lnast_ntype_bit_and; }
  bool constexpr is_bit_or() const { return val == Lnast_ntype_bit_or; }
  bool constexpr is_bit_not() const { return val == Lnast_ntype_bit_not; }
  bool constexpr is_bit_xor() const { return val == Lnast_ntype_bit_xor; }

  bool constexpr is_logical_and() const { return val == Lnast_ntype_logical_and; }
  bool constexpr is_logical_or() const { return val == Lnast_ntype_logical_or; }
  bool constexpr is_logical_not() const { return val == Lnast_ntype_logical_not; }

  bool constexpr is_reduce_or() const { return val == Lnast_ntype_reduce_or; }
  bool constexpr is_reduce_xor() const { return val == Lnast_ntype_reduce_xor; }

  bool constexpr is_plus() const { return val == Lnast_ntype_plus; }
  bool constexpr is_minus() const { return val == Lnast_ntype_minus; }
  bool constexpr is_mult() const { return val == Lnast_ntype_mult; }
  bool constexpr is_div() const { return val == Lnast_ntype_div; }
  bool constexpr is_mod() const { return val == Lnast_ntype_mod; }

  bool constexpr is_shl() const { return val == Lnast_ntype_shl; }
  bool constexpr is_shr() const { return val == Lnast_ntype_shr; }
  bool constexpr is_sra() const { return val == Lnast_ntype_sra; }

  bool constexpr is_sext() const { return val == Lnast_ntype_sext; }
  bool constexpr is_set_mask() const { return val == Lnast_ntype_set_mask; }
  bool constexpr is_get_mask() const { return val == Lnast_ntype_get_mask; }

  bool constexpr is_is() const { return val == Lnast_ntype_is; }
  bool constexpr is_ne() const { return val == Lnast_ntype_ne; }
  bool constexpr is_eq() const { return val == Lnast_ntype_eq; }
  bool constexpr is_lt() const { return val == Lnast_ntype_lt; }
  bool constexpr is_le() const { return val == Lnast_ntype_le; }
  bool constexpr is_gt() const { return val == Lnast_ntype_gt; }
  bool constexpr is_ge() const { return val == Lnast_ntype_ge; }

  bool constexpr is_tuple() const { return val == Lnast_ntype_tuple; }
  bool constexpr is_tuple_concat() const { return val == Lnast_ntype_tuple_concat; }
  bool constexpr is_tuple_delete() const { return val == Lnast_ntype_tuple_delete; }
  bool constexpr is_select() const { return val == Lnast_ntype_select; }

  bool constexpr is_ref() const { return val == Lnast_ntype_ref; }
  bool constexpr is_const() const { return val == Lnast_ntype_const; }

  bool constexpr is_assert() const { return val == Lnast_ntype_assert; }
  bool constexpr is_err_flag() const { return val == Lnast_ntype_err_flag; }

  bool constexpr is_tuple_add() const { return val == Lnast_ntype_tuple_add; }
  bool constexpr is_tuple_get() const { return val == Lnast_ntype_tuple_get; }
  bool constexpr is_attr_set() const { return val == Lnast_ntype_attr_set; }
  bool constexpr is_attr_get() const { return val == Lnast_ntype_attr_get; }

  bool constexpr is_tuple_attr() const {
    return val == Lnast_ntype_tuple_add || val == Lnast_ntype_tuple_get || val == Lnast_ntype_attr_set
           || val == Lnast_ntype_attr_get;
  }

  // Super types
  bool constexpr is_primitive_op() const { return (val >= Lnast_ntype_assign && val <= Lnast_ntype_attr_get); }
  bool constexpr is_logical_op() const {
    return (val == Lnast_ntype_logical_and) || (val == Lnast_ntype_logical_or) || (val == Lnast_ntype_logical_not);
  }

  bool constexpr is_reduce_op() const { return (val == Lnast_ntype_reduce_or) || (val == Lnast_ntype_reduce_xor); }

  bool constexpr is_unary_op() const {
    return (val == Lnast_ntype_bit_not) || (val == Lnast_ntype_logical_not) || (val == Lnast_ntype_assign)
           || (val == Lnast_ntype_dp_assign) || (val == Lnast_ntype_mut);
  }

  bool constexpr is_binary_op() const {
    return (val == Lnast_ntype_shl) || (val == Lnast_ntype_shr) || (val == Lnast_ntype_sra) || (val == Lnast_ntype_sext)
           || (val == Lnast_ntype_get_mask) || (val == Lnast_ntype_set_mask);
  }

  bool constexpr is_nary_op() const {
    return (val == Lnast_ntype_bit_and) || (val == Lnast_ntype_bit_or) || (val == Lnast_ntype_bit_xor)
           || (val == Lnast_ntype_logical_and) || (val == Lnast_ntype_logical_or) || (val == Lnast_ntype_reduce_or)
           || (val == Lnast_ntype_reduce_xor) || (val == Lnast_ntype_plus) || (val == Lnast_ntype_minus)
           || (val == Lnast_ntype_mult) || (val == Lnast_ntype_is) || (val == Lnast_ntype_eq) || (val == Lnast_ntype_ne)
           || (val == Lnast_ntype_lt) || (val == Lnast_ntype_le) || (val == Lnast_ntype_gt) || (val == Lnast_ntype_ge);
  }

  // basic_op have 1 to 1 translation between LNAST and Lgraph
  bool constexpr is_direct_lgraph_op() const {
    return (val >= Lnast_ntype_bit_and && val <= Lnast_ntype_ge) && val != Lnast_ntype_reduce_xor
           && val != Lnast_ntype_mod
           // && val != Lnast_ntype_shr
           && val != Lnast_ntype_is && val != Lnast_ntype_ne && val != Lnast_ntype_le;
  }

  std::string_view debug_name() const { return namemap[val]; }

  static_assert(namemap.size() == Lnast_ntype_last_invalid);
};
