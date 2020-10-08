
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = "\n";
  std::string_view lang_type = "prp";
  std::vector<std::string> unsigned_vars;
public:
  Prp_parser() {};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
  std::string_view start_else_if() final;
  std::string_view for_cond_mid() final;
  std::string_view for_cond_beg() final;
  std::string_view for_cond_end() final;
  std::string ref_name(std::string prp_term, bool strct = true) final;
  std::string ref_name(std::string_view prp_term, bool strct = true) final;
  void get_maps();//for debugging only
  void call_get_maps() final;

  std::string final_print(std::string modname, std::string buffer_to_print) final;

  void result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) final;

  std::string make_unsigned(std::string sec_child) final;
  bool is_unsigned(std::string var_name) final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = " ;\n";
  std::string_view lang_type = "cpp";
  std::string supp_ftype = "hpp";
  
  std::map<std::string, std::string> inp_bw;//first->input port name, second->UInt<bw>
  std::map<std::string, std::string> outp_bw;//first->o/p port name, sec->UInt<bw>
  std::map<std::string, std::string> reg_bw;//first->register name, sec->UInt<bw>    
  std::string sys_clock;
  std::string sys_clock_bits;
  std::string inps_csv;
  std::string supp_file_final_str;
  std::string main_file_final_str;
  std::string buff_to_print_vcd;

  std::vector<std::string> unsigned_vars;
public:
  Cpp_parser(){};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
  std::string_view start_else_if() final;
  std::string_view for_cond_mid() final;
  std::string_view for_cond_beg() final;
  std::string_view for_cond_end() final;
  std::string ref_name(std::string prp_term, bool strct = true) final;
  std::string ref_name(std::string_view prp_term, bool strct = true) final;
  std::string starter(std::string_view filename) final;
//header related functions:
  std::string_view supporting_ftype() final;
  std::string supporting_fstart(std::string basename_s) final;
  std::string supporting_fend(std::string basename_s) final;
  std::string supp_buffer_to_print(std::string modname) final;
  
  std::string main_fstart(std::string basename, std::string basename_s) final;
  bool convert_parameters(std::string key, std::string ref) final;
  void get_maps();//for debugging only
  void call_get_maps() final;

  std::string final_print(std::string modname, std::string buffer_to_print) final;
  int indent_final_system() final;
  void result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) final;
  void for_vcd_comb(std::string_view key1, std::string_view key2) final;

  std::string make_unsigned(std::string sec_child) final;
  bool is_unsigned(std::string var_name) final;
};

//-------------------------------------------------------------------------------------

class Ver_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = ";\n";
  std::string_view lang_type = "v";

  std::vector<std::string> unsigned_vars;
public:
  Ver_parser(){};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
  std::string_view start_else_if() final;
  std::string_view end_else_if() final;
  std::string_view start_else() final;
  std::string_view end_cond() final;
  std::string_view end_if_or_else() final;
  std::string_view for_stmt_beg() final;
  std::string_view for_stmt_end() final;
  std::string_view for_cond_mid() final;
  std::string_view for_cond_beg() final;
  std::string_view for_cond_end() final;
  std::string ref_name(std::string prp_term, bool strct = true) final;
  std::string ref_name(std::string_view prp_term, bool strct = true) final;
  std::string_view assign_node_strt() final;
  void get_maps();//for debugging only
  void call_get_maps() final;

  std::string final_print(std::string modname, std::string buffer_to_print) final;
  void result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) final;

  std::string make_unsigned(std::string sec_child) final;
  bool is_unsigned(std::string var_name) final;
};

