
#pragma once

#include <array>
#include <string_view>

#include "lnast_ntype.hpp"

class Lnast_map {
protected:
  constexpr static std::array namemap_pyrope{"invalid",

                                             "top",       "sts",        "if",       "uif",     "for", "while", "phi",
                                             "hotphi",    "func_call",  "func_def",

                                             "=",         ":=",         "mut",

                                             "&",         "|",          "~",        "^",

                                             "and",       "or",         "!",

                                             "ror",       "rxor",

                                             "+",         "-",          "*",        "/",       "mod",

                                             ">>",        "<<",         ">>>",

                                             "sext",      "set_mask", "get_mask",

                                             "is",        "!=",         "==",       "<",       "<=",  ">",     ">=",

                                             "()",
                                             "++",      //"tuple_concat", // ++
                                             "--",      //"tuple_delete", // --
                                             "select",  // []

                                             "ref",       "const",

                                             "assert",    "error_flag",

                                             "tuple_add", "tuple_get",  "attr_set", "attr_get"};

  constexpr static std::array namemap_verilog{
      "invalid",
      "top",
      "sts",
      "if",
      "uif",
      "for",
      "while",
      "phi",
      "hotphi",
      "func_call",
      "func_def",

      "=",
      "=",  // dp_assign
      "mut",

      "&",
      "|",
      "~",
      "^",

      "and",
      "or",
      "not",

      "ror",
      "rxor",

      "+",
      "-",
      "*",
      "/",
      "%",

      ">>",
      "<<",
      ">>>",

      "sext",
      "set_mask",
      "get_mask",

      "is",
      "!=",
      "==",
      "<",
      "<=",
      ">",
      ">=",

      "()",
      "tuple_concat",
      "tuple_delete",
      "select",  // []

      "ref",
      "const",

      "assert",
      "error_flag",

      "tuple_add",
      "tuple_get",
      "attr_set",
      "attr_get",
  };

  constexpr static std::array namemap_cpp{
      "invalid",

      "top",
      "sts",
      "if",
      "uif",
      "for",
      "while",
      "phi",
      "hotphi",
      "func_call",
      "func_def",

      "=",  // assign
      "=",  // dp_assign
      "mut",

      "&",  // and
      "|",  // or
      "~",  // not
      "^",  // not

      "&&",  // logical_and
      "||",  // logical_or
      "!",   // logical_not

      "ror",
      "rxor",

      "+",
      "-",
      "*",
      "/",
      "%",

      ">>",
      "<<",
      ">>>",

      "sext",
      "set_mask",
      "get_mask",

      "is",
      "!=",
      "==",
      "<",
      "<=",
      ">",
      ">=",

      "()",
      "tuple_concat",  // ++
      "tuple_delete",  // --
      "selc",          // []

      "ref",
      "const",

      "assert",
      "error_flag",

      "tuple_add",
      "tuple_get",
      "attr_set",
      "attr_get",
  };

public:
  static std::string_view debug_name_pyrope(Lnast_ntype val) { return namemap_pyrope[val.get_raw_ntype()]; }
  static std::string_view debug_name_verilog(Lnast_ntype val) { return namemap_verilog[val.get_raw_ntype()]; }
  static std::string_view debug_name_cpp(Lnast_ntype val) { return namemap_cpp[val.get_raw_ntype()]; }

  static_assert(namemap_cpp.size() == namemap_pyrope.size());
  static_assert(namemap_cpp.size() == namemap_verilog.size());
};
