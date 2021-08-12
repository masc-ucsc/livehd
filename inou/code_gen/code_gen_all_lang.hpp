
#pragma once

#include <string>
#include <string_view>

#include "mmap_str.hpp"
#include "file_output.hpp"
#include "code_gen.hpp"
#include "lnast_ntype.hpp"

class Code_gen_all_lang {
public:
  Code_gen_all_lang(){};

  virtual ~Code_gen_all_lang(){};

  virtual const mmap_lib::str stmt_sep() const                             = 0;
  virtual const mmap_lib::str get_lang_type() const                        = 0;
  virtual const mmap_lib::str debug_name_lang(Lnast_ntype node_type) const = 0;
  const mmap_lib::str         dot_type_op() const { return "."; };
  const mmap_lib::str         str_qoute(bool is_str) const { return is_str ? "\""_str : ""_str; /*if string then " is added*/ };
  const mmap_lib::str         gmask_op() const { return "@"; }
  virtual const mmap_lib::str start_else_if() const = 0;

  virtual const mmap_lib::str end_else_if() const { return ("}"_str); }
  virtual const mmap_lib::str start_else() const { return ("} else {\n"_str); }
  virtual const mmap_lib::str end_cond() const { return ") {\n"_str; }
  virtual const mmap_lib::str end_if_or_else() const { return "}\n"_str; }

  const mmap_lib::str start_cond() const { return "if ("_str; }
  const mmap_lib::str tuple_stmt_sep() const { return ", "_str; }
  const mmap_lib::str tuple_begin() const { return "("_str; }
  const mmap_lib::str tuple_end() const { return ")"_str; }

  // TODO: func def related parameters: need to make language specific! currently as per pyrope:
  const mmap_lib::str func_begin() const { return ""_str; }
  mmap_lib::str      func_name(const mmap_lib::str &func_name) const { return func_name;}
  const mmap_lib::str param_start(bool param_exist) const {
    if (param_exist)
      return " = |("_str;
    else
      return "= |"_str;
  }
  const mmap_lib::str func_param_sep() const { return ", "_str; }
  const mmap_lib::str param_end(bool param_exist) const {
    if (param_exist)
      return ")"_str;
    else
      return ""_str;
  }
  const mmap_lib::str print_cond(const mmap_lib::str &cond_val) const {
    if (cond_val != "")
      return (mmap_lib::str::concat(" when ", cond_val));
    else
      return cond_val;
  }
  const mmap_lib::str func_stmt_strt() const { return "|{\n"_str; }
  const mmap_lib::str func_stmt_end() const { return "}\n"_str; }
  const mmap_lib::str func_end() const { return ""_str; }

  // for related parameters:
  virtual const mmap_lib::str for_cond_beg() const = 0;
  virtual const mmap_lib::str for_cond_mid() const = 0;
  virtual const mmap_lib::str for_cond_end() const = 0;
  virtual const mmap_lib::str for_stmt_beg() const { return "{\n"_str; }
  virtual const mmap_lib::str for_stmt_end() const { return "}\n"_str; }

  // TODO: while related parameters: need to make language specific! currently as per pyrope:
  const mmap_lib::str while_cond_beg() const { return "("_str; }
  const mmap_lib::str while_cond_end() const { return ") "_str; }

  // TODO: select related parameters: need to make language specific! currently as per pyrope:
  mmap_lib::str select_init(const mmap_lib::str &select_type) const {
    if (select_type == "bit")
      return "[[";
    else if (select_type == "tuple_add")
      return "(";
    else
      return "[";
  }
  mmap_lib::str select_end(const mmap_lib::str &select_type) const {
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

  virtual mmap_lib::str ref_name_str(const mmap_lib::str &prp_term, bool strct = true) const = 0;

  // in verilog, assign stmt starts with assign keyword. thus this function.
  virtual const mmap_lib::str assign_node_strt() const { return ""_str; }

  virtual mmap_lib::str starter(const mmap_lib::str) const { return ""; };  // filename goes in here

  // for header file:
//  virtual mmap_lib::str supporting_fend(const mmap_lib::str) const { return ""_str; };  // basename_s goes here

  // Set methods modify the object. Do they really need to return arguments (return a new string is expensive)
  virtual void      set_supporting_fstart(const mmap_lib::str) { };  // basename_s goes in here
  virtual const mmap_lib::str supporting_ftype() const { return ""_str; };
  virtual void      set_supp_buffer_to_print(const mmap_lib::str) { };  // modname is the argument passed here

  // for main file (cpp file)
  virtual void add_to_buff_vec_for_cpp(const mmap_lib::str ) {};
  virtual void set_main_fstart(const mmap_lib::str &, const mmap_lib::str &) {
    //return mmap_lib::str::concat("file: "_str, basename, "\n"_str);
  };  // the other arg is basename_s

  // FIXME:renau. Several methods have no variable name. Use it as a way to explain what is the arg
  virtual bool set_convert_parameters(const mmap_lib::str &, const mmap_lib::str &) {
    return false;
  };  // 1st param is key and 2nd is ref

  // for final printing
  virtual void set_final_print(const mmap_lib::str &modname, std::shared_ptr<File_output> buffer_to_print) = 0;  // param is modname
  virtual void        call_dump_maps() const                                                          = 0;

  virtual int indent_final_system() const { return 0; };

//  virtual void result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const = 0;
  virtual mmap_lib::str get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const = 0;

  virtual void set_for_vcd_comb(const mmap_lib::str,const mmap_lib::str ){};

  // For tposs:
  virtual void        set_make_unsigned(const mmap_lib::str &sec_child) = 0;
  virtual bool        is_unsigned(const mmap_lib::str &var_name) const = 0;
};
