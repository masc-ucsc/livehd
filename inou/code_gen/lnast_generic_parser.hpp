
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser : public Code_gen_all_lang {
  const mmap_lib::str         stmt_separator = "\n";
  mmap_lib::str         lang_type      = "prp"_str;
  absl::flat_hash_set<mmap_lib::str> unsigned_vars;

public:
  Prp_parser(){};
  const mmap_lib::str stmt_sep() const final;
  const mmap_lib::str get_lang_type() const final;
  const mmap_lib::str debug_name_lang(Lnast_ntype node_type) const final;
  const mmap_lib::str start_else_if() const final;
  const mmap_lib::str for_cond_mid() const final;
  const mmap_lib::str for_cond_beg() const final;
  const mmap_lib::str for_cond_end() const final;
  mmap_lib::str    ref_name_str(const mmap_lib::str &prp_term, bool strct = true) const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  void set_final_print(const mmap_lib::str &modname, std::shared_ptr<File_output>buffer_to_print) final;

//  void result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const final;
  mmap_lib::str get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const final;

  void        set_make_unsigned(const mmap_lib::str &sec_child) final;
  bool        is_unsigned(const mmap_lib::str &var_name) const final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser : public Code_gen_all_lang {
  const mmap_lib::str stmt_separator = " ;\n";
  const mmap_lib::str lang_type   = "cpp"_str;
  const mmap_lib::str supp_ftype  = "hpp"_str;

  absl::flat_hash_map<mmap_lib::str, mmap_lib::str> inp_bw;   // first->input port name, second->UInt<bw>
  absl::flat_hash_map<mmap_lib::str, mmap_lib::str> outp_bw;  // first->o/p port name, sec->UInt<bw>
  absl::flat_hash_map<mmap_lib::str, mmap_lib::str> reg_bw;   // first->register name, sec->UInt<bw>
  mmap_lib::str                        sys_clock;
  mmap_lib::str                        sys_clock_bits;
  mmap_lib::str                      inps_csv;
  std::shared_ptr<File_output>       supp_file_final_str;
  std::shared_ptr<File_output>       main_file_final_str;
  mmap_lib::str                        buff_to_print_vcd;
  std::vector<mmap_lib::str> buff_vec_for_cpp;

  absl::flat_hash_set<mmap_lib::str> unsigned_vars;

public:
  Cpp_parser(){};
  const mmap_lib::str stmt_sep() const final;
  const mmap_lib::str get_lang_type() const final;
  const mmap_lib::str debug_name_lang(Lnast_ntype node_type) const final;
  const mmap_lib::str start_else_if() const final;
  const mmap_lib::str for_cond_mid() const final;
  const mmap_lib::str for_cond_beg() const final;
  const mmap_lib::str for_cond_end() const final;
  mmap_lib::str    ref_name_str(const mmap_lib::str &prp_term, bool strct = true) const final;
  mmap_lib::str      starter(const mmap_lib::str filename) const final;
  // header related functions:
  const mmap_lib::str supporting_ftype() const final;
  void      set_supporting_fstart(const mmap_lib::str basename_s) final;
//  mmap_lib::str      supporting_fend(const mmap_lib::str basename_s) const final;
  void     set_supp_buffer_to_print(const mmap_lib::str modname) final;

  void add_to_buff_vec_for_cpp(const mmap_lib::str s) final;
  void set_main_fstart(const mmap_lib::str &basename, const mmap_lib::str &basename_s) final;
  bool        set_convert_parameters(const mmap_lib::str &key, const mmap_lib::str &ref) final;
  void        dump_maps() const;
  void        call_dump_maps() const final;

  void set_final_print(const mmap_lib::str &modname, std::shared_ptr<File_output> buffer_to_print) final;
  int         indent_final_system() const final;
//  void        result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const final;
  mmap_lib::str get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const final;
  void        set_for_vcd_comb(const mmap_lib::str key1, const mmap_lib::str key2) final;

  void        set_make_unsigned(const mmap_lib::str &sec_child) final;
  bool        is_unsigned(const mmap_lib::str &var_name) const final;
};

//-------------------------------------------------------------------------------------

class Ver_parser : public Code_gen_all_lang {
  const mmap_lib::str stmt_separator = ";\n";
  const mmap_lib::str lang_type      = "v"_str;

  absl::flat_hash_set<mmap_lib::str> unsigned_vars;

public:
  Ver_parser(){};
  const mmap_lib::str stmt_sep() const final;
  const mmap_lib::str get_lang_type() const final;
  const mmap_lib::str debug_name_lang(Lnast_ntype node_type) const final;
  const mmap_lib::str start_else_if() const final;
  const mmap_lib::str end_else_if() const final;
  const mmap_lib::str start_else() const final;
  const mmap_lib::str end_cond() const final;
  const mmap_lib::str end_if_or_else() const final;
  const mmap_lib::str for_stmt_beg() const final;
  const mmap_lib::str for_stmt_end() const final;
  const mmap_lib::str for_cond_mid() const final;
  const mmap_lib::str for_cond_beg() const final;
  const mmap_lib::str for_cond_end() const final;
  mmap_lib::str    ref_name_str(const mmap_lib::str &prp_term, bool strct = true) const final;
  const mmap_lib::str assign_node_strt() const final;
  void             dump_maps() const;
  void             call_dump_maps() const final;

  void set_final_print(const mmap_lib::str &modname, std::shared_ptr<File_output> buffer_to_print) final;
//  void        result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const final;
 mmap_lib::str get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const final;

  void        set_make_unsigned(const mmap_lib::str &sec_child) final;
  bool        is_unsigned(const mmap_lib::str &var_name) const final;
};
