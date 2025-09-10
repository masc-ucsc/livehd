
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "code_gen.hpp"
#include "file_output.hpp"
#include "lnast_ntype.hpp"

class Code_gen_all_lang {
public:
  Code_gen_all_lang() {};

  virtual ~Code_gen_all_lang() {};

  virtual std::string stmt_sep() const                             = 0;
  virtual std::string get_lang_type() const                        = 0;
  virtual std::string debug_name_lang(Lnast_ntype node_type) const = 0;
  std::string         dot_type_op() const { return "."; };
  std::string         str_qoute(bool is_str) const { return is_str ? "\"" : ""; /*if string then " is added*/ };
  std::string         gmask_op() const { return "@"; }
  virtual std::string start_else_if() const = 0;

  virtual std::string end_else_if() const { return ("}"); }
  virtual std::string start_else() const { return ("} else {\n"); }
  virtual std::string end_cond() const { return ") {\n"; }
  virtual std::string end_if_or_else() const { return "}\n"; }

  std::string start_cond() const { return "if ("; }
  std::string tuple_stmt_sep() const { return ", "; }
  std::string tuple_begin() const { return "("; }
  std::string tuple_end() const { return ")"; }

  // TODO: func def related parameters: need to make language specific! currently as per pyrope:
  std::string func_begin() const { return ""; }
  std::string func_name(std::string_view func_name) const { return std::string(func_name); }
  std::string param_start(bool param_exist) const {
    if (param_exist) {
      return " = |(";
    } else {
      return "= |";
    }
  }
  std::string func_param_sep() const { return ", "; }
  std::string param_end(bool param_exist) const {
    if (param_exist) {
      return ")";
    } else {
      return "";
    }
  }
  std::string print_cond(std::string_view cond_val) const {
    if (cond_val != "") {
      return absl::StrCat(" when ", cond_val);
    }

    return std::string(cond_val);
  }
  std::string func_stmt_strt() const { return "|{\n"; }
  std::string func_stmt_end() const { return "}\n"; }
  std::string func_end() const { return ""; }

  // for related parameters:
  virtual std::string for_cond_beg() const = 0;
  virtual std::string for_cond_mid() const = 0;
  virtual std::string for_cond_end() const = 0;
  virtual std::string for_stmt_beg() const { return "{\n"; }
  virtual std::string for_stmt_end() const { return "}\n"; }

  // TODO: while related parameters: need to make language specific! currently as per pyrope:
  std::string while_cond_beg() const { return "("; }
  std::string while_cond_end() const { return ") "; }

  // TODO: select related parameters: need to make language specific! currently as per pyrope:
  std::string select_init(std::string_view select_type) const {
    if (select_type == "bit") {
      return "[[";
    } else if (select_type == "tuple_add") {
      return "(";
    } else {
      return "[";
    }
  }
  std::string select_end(std::string_view select_type) const {
    if (select_type == "bit") {
      return "]]";
    } else if (select_type == "tuple_add") {
      return ")";
    } else {
      return "]";
    }
  }

  bool has_prefix(std::string_view test_string) const {
    auto ch = test_string.front();
    return ch == '$' || ch == '#' || ch == '%';
  }

  bool is_output(std::string_view test_string) const { return test_string.front() == '%'; }

  virtual std::string ref_name_str(std::string_view prp_term, bool strct = true) const = 0;

  // in verilog, assign stmt starts with assign keyword. thus this function.
  virtual std::string assign_node_strt() const { return ""; }

  virtual std::string starter(std::string_view) const { return ""; };  // filename goes in here

  // for header file:
  //  virtual std::string supporting_fend(const std::string) const { return ""; };  // basename_s goes here

  // Set methods modify the object. Do they really need to return arguments (return a new string is expensive)
  virtual void        set_supporting_fstart(std::string_view){};  // basename_s goes in here
  virtual std::string supporting_ftype() const { return ""; };
  virtual void        set_supp_buffer_to_print(std::string_view){};  // modname is the argument passed here

  // for main file (cpp file)
  virtual void add_to_buff_vec_for_cpp(std::string_view){};
  virtual void set_main_fstart(std::string_view, std::string_view){
      // return absl::StrCat("file: ", basename, "\n");
  };  // the other arg is basename_s

  // FIXME:renau. Several methods have no variable name. Use it as a way to explain what is the arg
  virtual bool set_convert_parameters(std::string_view, std::string_view) { return false; };  // 1st param is key and 2nd is ref

  // for final printing
  virtual void set_final_print(std::string_view modname, std::shared_ptr<File_output> buffer_to_print) = 0;  // param is modname
  virtual void call_dump_maps() const                                                                  = 0;

  virtual int indent_final_system() const { return 0; };

  //  virtual void result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const = 0;
  virtual std::string get_lang_fname(std::string_view fname, std::string_view odir) const = 0;

  virtual void set_for_vcd_comb(std::string_view, std::string_view){};

  // For tposs:
  virtual void set_make_unsigned(std::string_view sec_child) = 0;
  virtual bool is_unsigned(std::string_view var_name) const  = 0;
};
