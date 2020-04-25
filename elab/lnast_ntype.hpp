//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

class Lnast_ntype {
public:
  enum Lnast_ntype_int : uint8_t {
    Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype
    //group: tree structure
    Lnast_ntype_top,
    Lnast_ntype_stmts,   // stmts
    Lnast_ntype_cstmts,  // statement for condition determination
    Lnast_ntype_if,
    Lnast_ntype_cond,
    Lnast_ntype_uif,
    Lnast_ntype_elif,
    Lnast_ntype_for,
    Lnast_ntype_while,
    Lnast_ntype_phi,
    Lnast_ntype_func_call,    // .()
    Lnast_ntype_func_def,     // ::{   func_def = sub-graph in lgraph

    //group: primitive operator
    Lnast_ntype_assign,       // =, pure assignment
    Lnast_ntype_dp_assign,    // :=, dp = deprecate
    Lnast_ntype_as,           // as
    Lnast_ntype_label,        // :
    Lnast_ntype_dot,          // .
    Lnast_ntype_logical_and,  // and
    Lnast_ntype_logical_or,   // or
    Lnast_ntype_and,          // &
    Lnast_ntype_or,           // |
    Lnast_ntype_xor,          // ^
    Lnast_ntype_plus,
    Lnast_ntype_minus,
    Lnast_ntype_mult,
    Lnast_ntype_div,
    Lnast_ntype_eq,
    Lnast_ntype_same,
    Lnast_ntype_lt,
    Lnast_ntype_le,
    Lnast_ntype_gt,
    Lnast_ntype_ge,
    Lnast_ntype_tuple,        // ()
    Lnast_ntype_tuple_concat, // ++
    Lnast_ntype_select,       // []
    Lnast_ntype_bit_select,   // [[]]
    Lnast_ntype_range,        // ..
    Lnast_ntype_shift_right,
    Lnast_ntype_shift_left,
    Lnast_ntype_logic_shift_right,
    Lnast_ntype_arith_shift_right,
    Lnast_ntype_dynamic_shift_right,
    Lnast_ntype_dynamic_shift_left,

    //group: language variable
    Lnast_ntype_ref,
    Lnast_ntype_const,

    //group: attribute
    Lnast_ntype_attr,    // __bits, __size, __foo

    //group: others
    Lnast_ntype_assert,       // I
    Lnast_ntype_err_flag,     // compile error flag
  };

protected:
  static inline std::string_view namemap[] = {
    "invalid",
    //group: tree structure
    "top",
    "stmts",
    "cstmts",
    "if",
    "cond",
    "uif",
    "elif",
    "for",
    "while",
    "phi",
    "func_call",
    "func_def",

    //group: primitive operator
    "assign",
    "dp_assign",
    "as",
    "label",
    "dot",
    "logical_and",
    "logical_or",
    "and",
    "or",
    "xor",
    "plus",
    "minus",
    "mult",
    "div",
    "eq",
    "same",
    "lt",
    "le",
    "gt",
    "ge",
    "tuple",
    "tuple_concat", // ++
    "select",     // []
    "bit_select", // [[]]
    "range",      // ..
    "shift_right",
    "shift_left",
    "logic_shift_right",
    "arith_shift_right",
    "dynamic_shift_right",
    "dynamic_shift_left",

    //group: language variable
    "ref",
    "const",

    //group: attribute
    "attr",

    //group: others
    "assert",
    "error_flag"
  };
  static inline std::string_view namemap_cfg[] = {
    "invalid",
    "top",
    "sts",
    "csts",
    "if",
    "cond",
    "uif",
    "elif",
    "for",
    "while",
    "phi",
    "func_call",
    "func_def",
    "=",
    ":=",
    "as",
    "=",
    ".",
    "add",
    "or",
    "&",
    "|",
    "^",
    "+",
    "-",
    "*",
    "/",
    "ed",
    "==",
    "<",
    "<=",
    ">",
    ">=",
    "()",
    "tuple_concat", // ++
    "select",  // []
    "bit_select", // [[]]
    "range", // ..
    "shift_right",
    "shift_left",
    "logic_shift_right",
    "arith_shift_right",
    "dynamic_shift_right",
    "dynamic_shift_left",

    "ref",
    "const",
    "attr",
    "I",
  };
  static inline std::string_view namemap_pyrope[] = {
    "invalid",
    "top",
    "sts",
    "csts",
    "if",
    "cond",
    "uif",
    "elif",
    "for",
    "while",
    "phi",
    "func_call",
    "func_def",
    "=",
    ":=",
    "as",
    "=",
    ".",
    "add",
    "or",
    "&",
    "|",
    "^",
    "+",
    "+",
    "*",
    "/",
    "eq",
    "==",
    "<",
    "<=",
    ">",
    ">=",
    "()",
    "tuple_concat", // ++
    "select",  // []
    "bit_select", // [[]]
    "range",      // ..
    "shift_right",
    "shift_left",
    "logic_shift_right",
    "arith_shift_right",
    "dynamic_shift_right",
    "dynamic_shift_left",

    "ref",
    "const",
    "attr",
    "assert",
    "error_flag"
  };
  static inline std::string_view namemap_verilog[] = {
    "invalid",
    "top",
    "sts",
    "csts",
    "if",
    "cond",
    "uif",
    "elif",
    "for",
    "while",
    "phi",
    "func_call",
    "func_def",
    "=",
    ":=",
    "as",
    "=",
    "_",
    "and",
    "or",
    "&",
    "|",
    "^",
    "+",
    "-",
    "*",
    "/",
    "eq",
    "==",
    "<",
    "<=",
    ">",
    ">=",
    "()",
    "tuple_concat",
    "select", // []
    "bit_select", // [[]]
    "range",      // ..
    "shift_right",
    "shift_left",
    "logic_shift_right",
    "arith_shift_right",
    "dynamic_shift_right",
    "dynamic_shift_left",

    "ref",
    "const",
    "attr",
    "assert",
    "error_flag"
  };
  static inline std::string_view namemap_cpp[] = {
    "invalid",
    "top",
    "sts",
    "csts",
    "if",
    "cond",
    "uif",
    "elif",
    "for",
    "while",
    "phi",
    "func_call",
    "func_def",
    "=", // assign
    ":=",
    "as",
    "=", // label
    ".", // dot
    "&&", // logical_and
    "||", // logical_or
    "operator&", // and
    "operator|", // or
    "operator^",
    "operator+",
    "operator-",
    "operator*",
    "operator/",
    "eq",
    "operator==",
    "operator<",
    "operator<=",
    "operator>",
    "operator>=",
    "()",
    "tuple_concat", // ++
    "select", // []
    "bit_select", // [[]]
    "range",         // ..
    "shift_right",
    "shift_left",
    "logic_shift_right",
    "arith_shift_right",
    "dynamic_shift_right",
    "dynamic_shift_left",

    "ref",
    "const",
    "attr",
    "assert",
    "error_flag"
  };

  Lnast_ntype_int val;
  explicit Lnast_ntype(Lnast_ntype_int _val) : val(_val) {}
public:
  Lnast_ntype() : val(Lnast_ntype_invalid) {}

  Lnast_ntype_int get_raw_ntype() const { return val; }

  static Lnast_ntype create_top()          { return Lnast_ntype(Lnast_ntype_top); }

  static Lnast_ntype create_stmts()        { return Lnast_ntype(Lnast_ntype_stmts); }
  static Lnast_ntype create_cstmts()       { return Lnast_ntype(Lnast_ntype_cstmts); }
  static Lnast_ntype create_if()           { return Lnast_ntype(Lnast_ntype_if); }
  static Lnast_ntype create_cond()         { return Lnast_ntype(Lnast_ntype_cond); }
  static Lnast_ntype create_uif()          { return Lnast_ntype(Lnast_ntype_uif); }
  static Lnast_ntype create_elif()         { return Lnast_ntype(Lnast_ntype_elif); }
  static Lnast_ntype create_for()          { return Lnast_ntype(Lnast_ntype_for); }
  static Lnast_ntype create_while()        { return Lnast_ntype(Lnast_ntype_while); }
  static Lnast_ntype create_phi()          { return Lnast_ntype(Lnast_ntype_phi); }
  static Lnast_ntype create_func_call()    { return Lnast_ntype(Lnast_ntype_func_call); }
  static Lnast_ntype create_func_def()     { return Lnast_ntype(Lnast_ntype_func_def); }

  static Lnast_ntype create_assign()       { return Lnast_ntype(Lnast_ntype_assign); }
  static Lnast_ntype create_dp_assign()    { return Lnast_ntype(Lnast_ntype_dp_assign); }
  static Lnast_ntype create_as()           { return Lnast_ntype(Lnast_ntype_as); }
  static Lnast_ntype create_label()        { return Lnast_ntype(Lnast_ntype_label); }
  static Lnast_ntype create_dot()          { return Lnast_ntype(Lnast_ntype_dot); }
  static Lnast_ntype create_logical_and()  { return Lnast_ntype(Lnast_ntype_logical_and); }
  static Lnast_ntype create_logical_or()   { return Lnast_ntype(Lnast_ntype_logical_or); }
  static Lnast_ntype create_and()          { return Lnast_ntype(Lnast_ntype_and); }
  static Lnast_ntype create_or()           { return Lnast_ntype(Lnast_ntype_or); }
  static Lnast_ntype create_xor()          { return Lnast_ntype(Lnast_ntype_xor); }
  static Lnast_ntype create_plus()         { return Lnast_ntype(Lnast_ntype_plus); }
  static Lnast_ntype create_minus()        { return Lnast_ntype(Lnast_ntype_minus); }
  static Lnast_ntype create_mult()         { return Lnast_ntype(Lnast_ntype_mult); }
  static Lnast_ntype create_div()          { return Lnast_ntype(Lnast_ntype_div); }
  static Lnast_ntype create_eq()           { return Lnast_ntype(Lnast_ntype_eq); }
  static Lnast_ntype create_same()         { return Lnast_ntype(Lnast_ntype_same); }
  static Lnast_ntype create_lt()           { return Lnast_ntype(Lnast_ntype_lt); }
  static Lnast_ntype create_le()           { return Lnast_ntype(Lnast_ntype_le); }
  static Lnast_ntype create_gt()           { return Lnast_ntype(Lnast_ntype_gt); }
  static Lnast_ntype create_ge()           { return Lnast_ntype(Lnast_ntype_ge); }
  static Lnast_ntype create_tuple()        { return Lnast_ntype(Lnast_ntype_tuple); }
  static Lnast_ntype create_tuple_concat() { return Lnast_ntype(Lnast_ntype_tuple_concat); }
  static Lnast_ntype create_select()       { return Lnast_ntype(Lnast_ntype_select);}
  static Lnast_ntype create_bit_select()   { return Lnast_ntype(Lnast_ntype_bit_select);}
  static Lnast_ntype create_range()        { return Lnast_ntype(Lnast_ntype_range);}

  static Lnast_ntype create_shift_right()         {return Lnast_ntype(Lnast_ntype_shift_right);}
  static Lnast_ntype create_shift_left()          {return Lnast_ntype(Lnast_ntype_shift_left);}
  static Lnast_ntype create_logic_shift_right()   {return Lnast_ntype(Lnast_ntype_logic_shift_right);}
  static Lnast_ntype create_arith_shift_right()   {return Lnast_ntype(Lnast_ntype_arith_shift_right);}
  static Lnast_ntype create_dynamic_shift_right() {return Lnast_ntype(Lnast_ntype_dynamic_shift_right);}
  static Lnast_ntype create_dynamic_shift_left()  {return Lnast_ntype(Lnast_ntype_dynamic_shift_left);}







  static Lnast_ntype create_ref()          { return Lnast_ntype(Lnast_ntype_ref); }
  static Lnast_ntype create_const()        { return Lnast_ntype(Lnast_ntype_const); }

  static Lnast_ntype create_attr()         { return Lnast_ntype(Lnast_ntype_attr); }

  static Lnast_ntype create_assert()       { return Lnast_ntype(Lnast_ntype_assert); }
  static Lnast_ntype create_err_flag()     { return Lnast_ntype(Lnast_ntype_err_flag); }

  bool is_invalid()      const { return val == Lnast_ntype_invalid; }
  bool is_top()          const { return val == Lnast_ntype_top; }

  bool is_stmts()        const { return val == Lnast_ntype_stmts; }
  bool is_cstmts()       const { return val == Lnast_ntype_cstmts; }
  bool is_if()           const { return val == Lnast_ntype_if; }
  bool is_cond()         const { return val == Lnast_ntype_cond; }
  bool is_uif()          const { return val == Lnast_ntype_uif; }
  bool is_elif()         const { return val == Lnast_ntype_elif; }
  bool is_for()          const { return val == Lnast_ntype_for; }
  bool is_while()        const { return val == Lnast_ntype_while; }
  bool is_phi()          const { return val == Lnast_ntype_phi; }
  bool is_func_call()    const { return val == Lnast_ntype_func_call; }
  bool is_func_def()     const { return val == Lnast_ntype_func_def; }

  bool is_assign()       const { return val == Lnast_ntype_assign; }
  bool is_dp_assign()    const { return val == Lnast_ntype_dp_assign; }
  bool is_as()           const { return val == Lnast_ntype_as; }
  bool is_label()        const { return val == Lnast_ntype_label; }
  bool is_dot()          const { return val == Lnast_ntype_dot; }
  bool is_logical_and()  const { return val == Lnast_ntype_logical_and; }
  bool is_logical_or()   const { return val == Lnast_ntype_logical_or; }
  bool is_and()          const { return val == Lnast_ntype_and; }
  bool is_or()           const { return val == Lnast_ntype_or; }
  bool is_xor()          const { return val == Lnast_ntype_xor; }
  bool is_plus()         const { return val == Lnast_ntype_plus; }
  bool is_minus()        const { return val == Lnast_ntype_minus; }
  bool is_mult()         const { return val == Lnast_ntype_mult; }
  bool is_div()          const { return val == Lnast_ntype_div; }
  bool is_eq()           const { return val == Lnast_ntype_eq; }
  bool is_same()         const { return val == Lnast_ntype_same; }
  bool is_lt()           const { return val == Lnast_ntype_lt; }
  bool is_le()           const { return val == Lnast_ntype_le; }
  bool is_gt()           const { return val == Lnast_ntype_gt; }
  bool is_ge()           const { return val == Lnast_ntype_ge; }
  bool is_tuple()        const { return val == Lnast_ntype_tuple; }
  bool is_tuple_concat() const { return val == Lnast_ntype_tuple_concat; }
  bool is_select()       const { return val == Lnast_ntype_select; }
  bool is_bit_select()   const { return val == Lnast_ntype_bit_select; }
  bool is_range()        const { return val == Lnast_ntype_range; }

  bool is_shift_right()         const { return val == Lnast_ntype_shift_right; }
  bool is_shift_left()          const { return val == Lnast_ntype_shift_left; }
  bool is_logic_shift_right()   const { return val == Lnast_ntype_logic_shift_right; }
  bool is_arith_shift_right()   const { return val == Lnast_ntype_arith_shift_right; }
  bool is_dynamic_shift_right() const { return val == Lnast_ntype_dynamic_shift_right; }
  bool is_dynamic_shift_left()  const { return val == Lnast_ntype_dynamic_shift_left; }



  bool is_ref()          const { return val == Lnast_ntype_ref; }
  bool is_const()        const { return val == Lnast_ntype_const; }

  bool is_attr()         const { return val == Lnast_ntype_attr; }

  bool is_assert()       const { return val == Lnast_ntype_assert; }
  bool is_err_flag()     const { return val == Lnast_ntype_err_flag; }

  // Super types
  bool is_logical_op()   const { return (val == Lnast_ntype_logical_and) or
                                        (val == Lnast_ntype_logical_or); }

  bool is_nary_op()      const { return (val == Lnast_ntype_and) or
                                        (val == Lnast_ntype_or) or
                                        (val == Lnast_ntype_xor) or
                                        (val == Lnast_ntype_plus) or
                                        (val == Lnast_ntype_minus) or
                                        (val == Lnast_ntype_mult) or
                                        (val == Lnast_ntype_div) or
                                        (val == Lnast_ntype_same) or
                                        (val == Lnast_ntype_lt) or
                                        (val == Lnast_ntype_le) or
                                        (val == Lnast_ntype_gt) or
                                        (val == Lnast_ntype_ge); }

  std::string_view debug_name() const { return namemap[val]; }
  std::string_view debug_name_cfg() const { return namemap_cfg[val]; }
  std::string_view debug_name_pyrope() const { return namemap_pyrope[val]; }
  std::string_view debug_name_verilog() const { return namemap_verilog[val]; }
  std::string_view debug_name_cpp() const { return namemap_cpp[val]; }
};

