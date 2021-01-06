
#pragma once

#include <string>
#include <string_view>
#include "code_gen.hpp"

class Code_gen_all_lang {
//protected:
//  virtual std::string_view stmt_sep() = 0;
public:
  Code_gen_all_lang() {};

  virtual ~Code_gen_all_lang() {};

  virtual std::string_view stmt_sep() = 0;
  virtual std::string_view get_lang_type() = 0;
  virtual std::string_view debug_name_lang(Lnast_ntype node_type) = 0;
  virtual std::string_view start_else_if() = 0;

  virtual std::string_view end_else_if() { return ("}"); }
  virtual std::string_view start_else() { return ("} else {\n"); }
  virtual std::string_view end_cond() {return ") {\n";}
  virtual std::string_view end_if_or_else() { return "}\n";}

  std::string_view start_cond() {return "if (";}
  std::string_view tuple_stmt_sep() {return ", ";}
  std::string_view tuple_begin() {return "(";}
  std::string_view tuple_end() {return ")";}

  //TODO: func def related parameters: need to make language specific! currently as per pyrope:
  std::string_view func_begin() {return "";}
  std::string_view func_name(std::string_view func_name) {return func_name;}
  std::string_view param_start(bool param_exist) {if (param_exist) return " = :("; else return "= :";}
  std::string_view func_param_sep() {return ", ";}
  std::string_view param_end(bool param_exist) {if (param_exist) return ")"; else return "";}
  std::string print_cond(std::string cond_val) {
    if(cond_val != "")
      return (absl::StrCat(" when ", cond_val));
    else
      return cond_val;
  }
  std::string_view func_stmt_strt() {return ":{\n";}
  std::string_view func_stmt_end() {return "}\n";}
  std::string_view func_end() {return "";}

  //for related parameters:
  virtual std::string_view for_cond_beg() = 0;
  virtual std::string_view for_cond_mid() = 0;
  virtual std::string_view for_cond_end() = 0;
  virtual std::string_view for_stmt_beg() {return "{\n";}
  virtual std::string_view for_stmt_end() {return "}\n";}

  //TODO: while related parameters: need to make language specific! currently as per pyrope:
  std::string_view while_cond_beg() {return "(";}
  std::string_view while_cond_end() {return ") ";}

  //TODO: select related parameters: need to make language specific! currently as per pyrope:
  std::string_view select_init(std::string select_type) {
    if (select_type=="bit")
      return "[[";
    else
      return "[";
  }
  std::string_view select_end(std::string select_type) {
    if (select_type=="bit")
      return "]]";
else
      return "]";
  }

  //this func is to truncate the %/$/# during cpp or verilog conversion
  bool has_prefix(std::string_view test_string) {
    return (test_string.find("$")==0 || test_string.find("#") ==0 || test_string.find("%") == 0);
  }
  bool is_output(std::string_view test_string) {
    return (test_string.find("%") == 0);
  }
  virtual std::string ref_name(std::string_view prp_term, bool strct = true) = 0;//if you do not want "outputs." to appear before the output variable in cpp, then use "false" as second parameter
  virtual std::string ref_name(std::string prp_term, bool strct = true) = 0;//if you do not want "outputs." to appear before the output variable in cpp, then use "false" as second parameter

  //in verilog, assign stmt starts with assign keyword. thus this function.
  virtual std::string_view assign_node_strt() {return "";}

  virtual std::string starter(std::string_view ) {return "";} ;//filename goes in here

  //for header file:
  virtual std::string supporting_fend(std::string) {return "";} ;//basename_s goes here
  virtual std::string supporting_fstart(std::string) {return "";} ;//basename_s goes in here
  virtual std::string_view supporting_ftype() {return "";};
  virtual std::string supp_buffer_to_print(std::string) {return "";};//modname is the argument passed here

  //for main file (cpp file)
  virtual std::string main_fstart(std::string basename, std::string ) {return absl::StrCat("file: ", basename, "\n");} ;//the other arg is basename_s
  virtual bool convert_parameters(std::string , std::string) {return false;};//1st param is key and 2nd is ref

  //for final printing
  virtual std::string final_print(std::string modname, std::string buffer_to_print) = 0;//param is modname
  virtual void call_get_maps() = 0;//for debugging only

  virtual int indent_final_system() {return 0;};

  //odir related:
  virtual void result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) =0;

  virtual void for_vcd_comb(std::string_view , std::string_view) {return ;};

  //For tposs:
  virtual std::string make_unsigned(std::string sec_child) =0;
  virtual bool is_unsigned(std::string var_name) =0;
};

