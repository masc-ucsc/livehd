
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser : public Code_gen_all_lang {
  const std::string         stmt_separator = "\n";
  std::string         lang_type      = "prp"_str;
  absl::flat_hash_set<std::string> unsigned_vars;

public:
  Prp_parser(){};
  const std::string stmt_sep() const final;
  const std::string get_lang_type() const final;
  const std::string debug_name_lang(Lnast_ntype node_type) const final;
  const std::string start_else_if() const final;
  const std::string for_cond_mid() const final;
  const std::string for_cond_beg() const final;
  const std::string for_cond_end() const final;
  std::string    ref_name_str(std::string_view prp_term, bool strct = true) const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  void set_final_print(std::string_view modname, std::shared_ptr<File_output>buffer_to_print) final;

//  void result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;
  std::string get_lang_fname(std::string_view fname, std::string_view odir) const final;

  void        set_make_unsigned(std::string_view sec_child) final;
  bool        is_unsigned(std::string_view var_name) const final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser : public Code_gen_all_lang {
  const std::string stmt_separator = " ;\n";
  const std::string lang_type   = "cpp"_str;
  const std::string supp_ftype  = "hpp"_str;

  absl::flat_hash_map<std::string, std::string> inp_bw;   // first->input port name, second->UInt<bw>
  absl::flat_hash_map<std::string, std::string> outp_bw;  // first->o/p port name, sec->UInt<bw>
  absl::flat_hash_map<std::string, std::string> reg_bw;   // first->register name, sec->UInt<bw>
  std::string                        sys_clock;
  std::string                        sys_clock_bits;
  std::string                      inps_csv;
  std::shared_ptr<File_output>       supp_file_final_str;
  std::shared_ptr<File_output>       main_file_final_str;
  std::string                        buff_to_print_vcd;
  std::vector<std::string> buff_vec_for_cpp;

  absl::flat_hash_set<std::string> unsigned_vars;

public:
  Cpp_parser(){};
  const std::string stmt_sep() const final;
  const std::string get_lang_type() const final;
  const std::string debug_name_lang(Lnast_ntype node_type) const final;
  const std::string start_else_if() const final;
  const std::string for_cond_mid() const final;
  const std::string for_cond_beg() const final;
  const std::string for_cond_end() const final;
  std::string    ref_name_str(std::string_view prp_term, bool strct = true) const final;
  std::string      starter(std::string_view filename) const final;
  // header related functions:
  const std::string supporting_ftype() const final;
  void      set_supporting_fstart(std::string_view basename_s) final;
//  std::string      supporting_fend(std::string_view basename_s) const final;
  void     set_supp_buffer_to_print(std::string_view modname) final;

  void add_to_buff_vec_for_cpp(std::string_view s) final;
  void set_main_fstart(std::string_view basename, std::string_view basename_s) final;
  bool        set_convert_parameters(std::string_view key, std::string_view ref) final;
  void        dump_maps() const;
  void        call_dump_maps() const final;

  void set_final_print(std::string_view modname, std::shared_ptr<File_output> buffer_to_print) final;
  int         indent_final_system() const final;
//  void        result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;
  std::string get_lang_fname(std::string_view fname, std::string_view odir) const final;
  void        set_for_vcd_comb(std::string_view key1, std::string_view key2) final;

  void        set_make_unsigned(std::string_view sec_child) final;
  bool        is_unsigned(std::string_view var_name) const final;
};

//-------------------------------------------------------------------------------------

class Ver_parser : public Code_gen_all_lang {
  const std::string stmt_separator = ";\n";
  const std::string lang_type      = "v"_str;

  absl::flat_hash_set<std::string> unsigned_vars;

public:
  Ver_parser(){};
  const std::string stmt_sep() const final;
  const std::string get_lang_type() const final;
  const std::string debug_name_lang(Lnast_ntype node_type) const final;
  const std::string start_else_if() const final;
  const std::string end_else_if() const final;
  const std::string start_else() const final;
  const std::string end_cond() const final;
  const std::string end_if_or_else() const final;
  const std::string for_stmt_beg() const final;
  const std::string for_stmt_end() const final;
  const std::string for_cond_mid() const final;
  const std::string for_cond_beg() const final;
  const std::string for_cond_end() const final;
  std::string    ref_name_str(std::string_view prp_term, bool strct = true) const final;
  const std::string assign_node_strt() const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  void set_final_print(std::string_view modname, std::shared_ptr<File_output> buffer_to_print) final;
//  void        result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const final;
 std::string get_lang_fname(std::string_view fname, std::string_view odir) const final;

  void        set_make_unsigned(std::string_view sec_child) final;
  bool        is_unsigned(std::string_view var_name) const final;
};
