
#pragma once

#include <string>
#include <string_view>

#include "mmap_str.hpp"

#include "absl/strings/str_cat.h"
#include "code_gen.hpp"
#include "lnast_ntype.hpp"

class Code_gen_all_lang {
public:
  Code_gen_all_lang(){};

  virtual ~Code_gen_all_lang(){};

  virtual std::string_view stmt_sep() const                             = 0;
  virtual std::string_view get_lang_type() const                        = 0;
  virtual std::string_view debug_name_lang(Lnast_ntype node_type) const = 0;
  std::string_view         dot_type_op() const { return "."; };
  std::string_view         str_qoute(bool is_str) const { return is_str ? "\"" : ""; /*if string then " is added*/ };
  std::string_view         gmask_op() const { return "@"; }
  virtual std::string_view start_else_if() const = 0;

  virtual std::string_view end_else_if() const { return ("}"); }
  virtual std::string_view start_else() const { return ("} else {\n"); }
  virtual std::string_view end_cond() const { return ") {\n"; }
  virtual std::string_view end_if_or_else() const { return "}\n"; }

  std::string_view start_cond() const { return "if ("; }
  std::string_view tuple_stmt_sep() const { return ", "; }
  std::string_view tuple_begin() const { return "("; }
  std::string_view tuple_end() const { return ")"; }

  // TODO: func def related parameters: need to make language specific! currently as per pyrope:
  std::string_view func_begin() const { return ""; }
  std::string      func_name(const mmap_lib::str &func_name) const { return func_name.to_s(); }
  std::string_view param_start(bool param_exist) const {
    if (param_exist)
      return " = |(";
    else
      return "= |";
  }
  std::string_view func_param_sep() const { return ", "; }
  std::string_view param_end(bool param_exist) const {
    if (param_exist)
      return ")";
    else
      return "";
  }
  std::string print_cond(const std::string &cond_val) const {
    if (cond_val != "")
      return (absl::StrCat(" when ", cond_val));
    else
      return cond_val;
  }
  std::string_view func_stmt_strt() const { return "|{\n"; }
  std::string_view func_stmt_end() const { return "}\n"; }
  std::string_view func_end() const { return ""; }

  // for related parameters:
  virtual std::string_view for_cond_beg() const = 0;
  virtual std::string_view for_cond_mid() const = 0;
  virtual std::string_view for_cond_end() const = 0;
  virtual std::string_view for_stmt_beg() const { return "{\n"; }
  virtual std::string_view for_stmt_end() const { return "}\n"; }

  // TODO: while related parameters: need to make language specific! currently as per pyrope:
  std::string_view while_cond_beg() const { return "("; }
  std::string_view while_cond_end() const { return ") "; }

  // TODO: select related parameters: need to make language specific! currently as per pyrope:
  std::string_view select_init(const mmap_lib::str &select_type) const {
    if (select_type == "bit")
      return "[[";
    else if (select_type == "tuple_add")
      return "(";
    else
      return "[";
  }
  std::string_view select_end(const mmap_lib::str &select_type) const {
    if (select_type == "bit")
      return "]]";
    else if (select_type == "tuple_add")
      return ")";
    else
      return "]";
  }

  bool has_prefix(const mmap_lib::str &test_string) const {
    auto ch = test_string.front();
    return ch == '$' || ch == '#' || ch == '%';
  }

  bool is_output(const mmap_lib::str &test_string) const {
    return test_string.front() == '%';
  }

  virtual std::string ref_name(const mmap_lib::str &prp_term, bool strct = true) const = 0;
  virtual mmap_lib::str ref_name_str(const mmap_lib::str &prp_term, bool strct = true) const = 0;

  // in verilog, assign stmt starts with assign keyword. thus this function.
  virtual std::string_view assign_node_strt() const { return ""; }

  virtual std::string starter(std::string_view) const { return ""; };  // filename goes in here

  // for header file:
  virtual std::string supporting_fend(const std::string &) const { return ""; };  // basename_s goes here

  // Set methods modify the object. Do they really need to return arguments (return a new string is expensive)
  virtual std::string      set_supporting_fstart(const std::string &) { return ""; };  // basename_s goes in here
  virtual std::string_view supporting_ftype() const { return ""; };
  virtual std::string      set_supp_buffer_to_print(const std::string &) { return ""; };  // modname is the argument passed here

  // for main file (cpp file)
  virtual std::string set_main_fstart(const std::string &basename, const std::string &) {
    return absl::StrCat("file: ", basename, "\n");
  };  // the other arg is basename_s

  // FIXME:renau. Several methods have no variable name. Use it as a way to explain what is the arg
  virtual bool set_convert_parameters(const mmap_lib::str &, const std::string &) {
    return false;
  };  // 1st param is key and 2nd is ref

  // for final printing
  virtual std::string set_final_print(const std::string &modname, const std::string &buffer_to_print) = 0;  // param is modname
  virtual void        call_dump_maps() const                                                          = 0;

  virtual int indent_final_system() const { return 0; };

  virtual void result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const = 0;

  virtual void set_for_vcd_comb(std::string_view, std::string_view){};

  // For tposs:
  virtual void        set_make_unsigned(const mmap_lib::str &sec_child) = 0;
  virtual bool        is_unsigned(const mmap_lib::str &var_name) const = 0;
};
