
#pragma once

#include <array>
#include <string_view>

#include "lnast_ntype.hpp"

class Lnast_map {
protected:
  constexpr static std::array namemap_pyrope{"invalid",

                                             "top",       "sts",        "if",       "uif",     "for", "while",
                                             "func_call",  "func_def",

                                             "=",         ":=",         "mut",

                                             "&",         "|",          "~",        "^",

                                             "ror",

                                             "and",       "or",         "!",

                                             "+",         "-",          "*",        "/",       "mod",

                                             "<<",        ">>",

                                             "sext",      "set_mask",   "get_mask", "mask_and", "mask_popcount", "mask_xor",

                                             "is",        "!=",         "==",       "<",       "<=",  ">",     ">=",


                                             "ref",       "const",


                                             "++",        "tuple_add", "tuple_get", "tuple_set",

                                             "attr_set", "attr_get",

                                             "error_flag", "phi", "hot_phi"
  };

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

      "&",
      "|",
      "~",
      "^",

      "ror",

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

      "tuple_concat",
      "tuple_add",
      "tuple_get",
      "tuple_set",

      "attr_set",
      "attr_get",

      "error_flag",
      "phi",
      "hotphi",
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

      "&",  // and
      "|",  // or
      "~",  // not
      "^",  // not

      "ror",

      "&&",  // logical_and
      "||",  // logical_or
      "!",   // logical_not

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

      "tuple_concat",  // ++
      "tuple_add",
      "tuple_get",
      "tuple_set",

      "attr_set",
      "attr_get",

      "error_flag",
      "phi",
      "hotphi",
  };

public:
  static std::string_view debug_name_pyrope(Lnast_ntype val) { return namemap_pyrope[val.get_raw_ntype()]; }
  static std::string_view debug_name_verilog(Lnast_ntype val) { return namemap_verilog[val.get_raw_ntype()]; }
  static std::string_view debug_name_cpp(Lnast_ntype val) { return namemap_cpp[val.get_raw_ntype()]; }

  static_assert(namemap_cpp.size() == namemap_pyrope.size());
  static_assert(namemap_cpp.size() == namemap_verilog.size());
};
