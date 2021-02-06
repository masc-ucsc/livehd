
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser : public Code_gen_all_lang {
  std::string_view         stmt_separator = "\n";
  std::string_view         lang_type      = "prp";
  std::vector<std::string> unsigned_vars;

public:
  Prp_parser(){};
  std::string_view stmt_sep() const final;
  std::string_view get_lang_type() const final;
  std::string_view debug_name_lang(Lnast_ntype node_type) const final;
  std::string_view start_else_if() const final;
  std::string_view for_cond_mid() const final;
  std::string_view for_cond_beg() const final;
  std::string_view for_cond_end() const final;
  std::string      ref_name(const std::string &prp_term, bool strct = true) const final;
  std::string      ref_name(std::string_view prp_term, bool strct = true) const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  std::string set_final_print(const std::string &modname, const std::string &buffer_to_print) final;

  void result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;

  std::string set_make_unsigned(const std::string &sec_child) final;
  bool        is_unsigned(const std::string &var_name) const final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser : public Code_gen_all_lang {
  std::string_view stmt_separator = " ;\n";
  std::string_view lang_type      = "cpp";
  std::string      supp_ftype     = "hpp";

  std::map<std::string, std::string> inp_bw;   // first->input port name, second->UInt<bw>
  std::map<std::string, std::string> outp_bw;  // first->o/p port name, sec->UInt<bw>
  std::map<std::string, std::string> reg_bw;   // first->register name, sec->UInt<bw>
  std::string                        sys_clock;
  std::string                        sys_clock_bits;
  std::string                        inps_csv;
  std::string                        supp_file_final_str;
  std::string                        main_file_final_str;
  std::string                        buff_to_print_vcd;

  std::vector<std::string> unsigned_vars;

public:
  Cpp_parser(){};
  std::string_view stmt_sep() const final;
  std::string_view get_lang_type() const final;
  std::string_view debug_name_lang(Lnast_ntype node_type) const final;
  std::string_view start_else_if() const final;
  std::string_view for_cond_mid() const final;
  std::string_view for_cond_beg() const final;
  std::string_view for_cond_end() const final;
  std::string      ref_name(const std::string &prp_term, bool strct = true) const final;
  std::string      ref_name(std::string_view prp_term, bool strct = true) const final;
  std::string      starter(std::string_view filename) const final;
  // header related functions:
  std::string_view supporting_ftype() const final;
  std::string      set_supporting_fstart(const std::string &basename_s) final;
  std::string      supporting_fend(const std::string &basename_s) const final;
  std::string      set_supp_buffer_to_print(const std::string &modname) final;

  std::string set_main_fstart(const std::string &basename, const std::string &basename_s) final;
  bool        set_convert_parameters(const std::string &key, const std::string &ref) final;
  void        dump_maps() const;
  void        call_dump_maps() const final;

  std::string set_final_print(const std::string &modname, const std::string &buffer_to_print) final;
  int         indent_final_system() const final;
  void        result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;
  void        set_for_vcd_comb(std::string_view key1, std::string_view key2) final;

  std::string set_make_unsigned(const std::string &sec_child) final;
  bool        is_unsigned(const std::string &var_name) const final;
};

//-------------------------------------------------------------------------------------

class Ver_parser : public Code_gen_all_lang {
  std::string_view stmt_separator = ";\n";
  std::string_view lang_type      = "v";

  std::vector<std::string> unsigned_vars;

public:
  Ver_parser(){};
  std::string_view stmt_sep() const final;
  std::string_view get_lang_type() const final;
  std::string_view debug_name_lang(Lnast_ntype node_type) const final;
  std::string_view start_else_if() const final;
  std::string_view end_else_if() const final;
  std::string_view start_else() const final;
  std::string_view end_cond() const final;
  std::string_view end_if_or_else() const final;
  std::string_view for_stmt_beg() const final;
  std::string_view for_stmt_end() const final;
  std::string_view for_cond_mid() const final;
  std::string_view for_cond_beg() const final;
  std::string_view for_cond_end() const final;
  std::string      ref_name(const std::string &prp_term, bool strct = true) const final;
  std::string      ref_name(std::string_view prp_term, bool strct = true) const final;
  std::string_view assign_node_strt() const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  std::string set_final_print(const std::string &modname, const std::string &buffer_to_print) final;
  void        result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;

  std::string set_make_unsigned(const std::string &sec_child) final;
  bool        is_unsigned(const std::string &var_name) const final;
};
