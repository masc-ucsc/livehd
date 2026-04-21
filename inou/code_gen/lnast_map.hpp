
#pragma once

#include <array>
#include <string_view>

#include "lnast_ntype.hpp"

class Lnast_map {
protected:
  constexpr static std::array namemap_pyrope{"invalid",

                                             "top",      "sts",        "if",        "uif",       "for",
                                             "while",    "func_call",  "func_def",

                                             "=",        ":=",         "mut",        "delay_assign",

                                             "&",        "|",          "~",         "^",

                                             "ror",      "rand",       "rxor",      "popcount",

                                             "and",      "or",         "!",

                                             "+",        "-",          "*",         "/",         "mod",

                                             "<<",       ">>",

                                             "sext",     "set_mask",   "get_mask",  "mask_and",  "mask_popcount",
                                             "mask_xor",

                                             "is",       "!=",         "==",        "<",         "<=",
                                             ">",        ">=",

                                             "ref",      "const",      "range",

                                             "++",       "++",         "tuple_get", "tuple_set",

                                             "attr_set", "attr_get",

                                             "cassert",  "error_flag", "phi",       "hot_phi",

                                             "type_def", "type_spec",

                                             "none",     "uint",       "sint",      "range",      "string",
                                             "boolean",  "type",       "ref",

                                             "tuple",    "array",      "mixin",     "lambda",     "enum",
                                             "expr_type", "unknown"};

  constexpr static std::array namemap_verilog{
      "invalid",
      "top",
      "sts",
      "if",
      "uif",
      "for",
      "while",
      "func_call",
      "func_def",

      "=",
      "=",  // dp_assign
      "mut",
      "delay_assign",

      "&",
      "|",
      "~",
      "^",

      "ror",
      "rand",
      "rxor",
      "popcount",

      "and",
      "or",
      "not",

      "+",
      "-",
      "*",
      "/",
      "%",

      "<<",
      ">>>",

      "sext",
      "set_mask",
      "get_mask",
      "mask_and",
      "mask_popcount",
      "mask_xor",

      "is",
      "!=",
      "==",
      "<",
      "<=",
      ">",
      ">=",

      "ref",
      "const",
      "range",

      "tuple_concat",
      "tuple_add",
      "tuple_get",
      "tuple_set",

      "attr_set",
      "attr_get",

      "cassert",
      "error_flag",
      "phi",
      "hotphi",

      "typedef",
      "type_spec",

      "none",
      "logic",  // prim_type_uint
      "signed logic",  // prim_type_sint
      "range",
      "string",
      "bit",  // prim_type_boolean
      "type",
      "ref",  // prim_type_ref

      "struct",  // comp_type_tuple
      "array",
      "mixin",
      "lambda",
      "enum",
      "expr_type",
      "unknown",
  };

  constexpr static std::array namemap_cpp{
      "invalid",

      "top",
      "sts",
      "if",
      "uif",
      "for",
      "while",
      "func_call",
      "func_def",

      "=",  // assign
      "=",  // dp_assign
      "mut",
      "delay_assign",

      "&",  // and
      "|",  // or
      "~",  // not
      "^",  // not

      "ror",
      "rand",
      "rxor",
      "popcount",

      "&&",  // log_and
      "||",  // log_or
      "!",   // log_not

      "+",
      "-",
      "*",
      "/",
      "%",

      "<<",
      ">>>",

      "sext",
      "set_mask",
      "get_mask",
      "mask_and",
      "mask_popcount",
      "mask_xor",

      "is",
      "!=",
      "==",
      "<",
      "<=",
      ">",
      ">=",

      "ref",
      "const",
      "range",

      "tuple_concat",  // ++
      "tuple_add",
      "tuple_get",
      "tuple_set",

      "attr_set",
      "attr_get",

      "cassert",
      "error_flag",
      "phi",
      "hotphi",

      "typedef",
      "type_spec",

      "none",
      "unsigned",  // prim_type_uint
      "signed",    // prim_type_sint
      "range",
      "string",
      "bool",  // prim_type_boolean
      "type",
      "ref",   // prim_type_ref

      "tuple",  // comp_type_tuple
      "array",
      "mixin",
      "lambda",
      "enum",
      "expr_type",
      "unknown",
  };

public:
  static std::string_view debug_name_pyrope(Lnast_ntype val) { return namemap_pyrope[val.get_raw_ntype()]; }
  static std::string_view debug_name_verilog(Lnast_ntype val) { return namemap_verilog[val.get_raw_ntype()]; }
  static std::string_view debug_name_cpp(Lnast_ntype val) { return namemap_cpp[val.get_raw_ntype()]; }

  static_assert(namemap_cpp.size() == namemap_pyrope.size());
  static_assert(namemap_cpp.size() == namemap_verilog.size());
  static_assert(namemap_pyrope.size() == Lnast_ntype::Lnast_ntype_last_invalid,
                "lnast_map.hpp namemap_pyrope out of sync with lnast_nodes.def — add/remove the matching entry");
  static_assert(namemap_verilog.size() == Lnast_ntype::Lnast_ntype_last_invalid,
                "lnast_map.hpp namemap_verilog out of sync with lnast_nodes.def — add/remove the matching entry");
  static_assert(namemap_cpp.size() == Lnast_ntype::Lnast_ntype_last_invalid,
                "lnast_map.hpp namemap_cpp out of sync with lnast_nodes.def — add/remove the matching entry");
};
