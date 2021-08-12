
#include "lnast_generic_parser.hpp"

#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "code_gen_all_lang.hpp"
#include "lnast_map.hpp"


mmap_lib::str Prp_parser::ref_name_str(const mmap_lib::str &prp_term, bool) const { return prp_term; }

mmap_lib::str Cpp_parser::ref_name_str(const mmap_lib::str &prp_term, bool strct) const {
  if (!Code_gen_all_lang::has_prefix(prp_term)) {
    return prp_term;
  }

  if (Code_gen_all_lang::is_output(prp_term) && strct == true) {
    return mmap_lib::str::concat(mmap_lib::str("outputs."), prp_term.substr(1));
  }

  return prp_term.substr(1);
}

mmap_lib::str Ver_parser::ref_name_str(const mmap_lib::str &prp_term, bool) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    return prp_term.substr(1);
  }

  return prp_term;
}

const mmap_lib::str Prp_parser::stmt_sep() const { return stmt_separator; }
const mmap_lib::str Cpp_parser::stmt_sep() const { return stmt_separator; }
const mmap_lib::str Ver_parser::stmt_sep() const { return stmt_separator; }

const mmap_lib::str  Prp_parser::get_lang_type() const { return lang_type; }
const mmap_lib::str  Cpp_parser::get_lang_type() const { return lang_type; }
const mmap_lib::str  Ver_parser::get_lang_type() const { return lang_type; }

const mmap_lib::str Prp_parser::debug_name_lang(Lnast_ntype node_type) const { return mmap_lib::str(Lnast_map::debug_name_pyrope(node_type)); }
const mmap_lib::str Cpp_parser::debug_name_lang(Lnast_ntype node_type) const { return mmap_lib::str(Lnast_map::debug_name_cpp(node_type)); }
const mmap_lib::str Ver_parser::debug_name_lang(Lnast_ntype node_type) const { return mmap_lib::str(Lnast_map::debug_name_verilog(node_type)); }

const mmap_lib::str Prp_parser::start_else_if() const { return ("} elif ("_str); }
const mmap_lib::str Cpp_parser::start_else_if() const { return ("} else if ("_str); }
const mmap_lib::str Ver_parser::start_else_if() const { return ("end else if ("_str); }

const mmap_lib::str Ver_parser::end_else_if() const { return ("end"_str); }

const mmap_lib::str Ver_parser::start_else() const { return ("end else begin\n"_str); }

const mmap_lib::str Ver_parser::end_cond() const { return (") begin\n"_str); }

const mmap_lib::str Ver_parser::end_if_or_else() const { return ("end\n"_str); }

const mmap_lib::str Ver_parser::for_stmt_beg() const { return "begin\n"_str; }
const mmap_lib::str Ver_parser::for_stmt_end() const { return "end\n"_str; }

const mmap_lib::str Prp_parser::for_cond_mid() const { return " in "_str; }
const mmap_lib::str Cpp_parser::for_cond_mid() const { return ": "_str; }
const mmap_lib::str Ver_parser::for_cond_mid() const {
  return ": "_str;  // TODO
}

const mmap_lib::str Prp_parser::for_cond_beg() const { return " "_str; }
const mmap_lib::str Cpp_parser::for_cond_beg() const { return " ( auto "_str; }
const mmap_lib::str Ver_parser::for_cond_beg() const {
  return " ( auto "_str;  // TODO
}

const mmap_lib::str Prp_parser::for_cond_end() const { return " "_str; }
const mmap_lib::str Cpp_parser::for_cond_end() const { return ") "_str; }
const mmap_lib::str Ver_parser::for_cond_end() const {
  return ") "_str;  // TODO
}

const mmap_lib::str Ver_parser::assign_node_strt() const { return "assign "_str; }

mmap_lib::str Cpp_parser::starter(const mmap_lib::str filename) const {
  return mmap_lib::str::concat("void "_str, filename, "::cycle("_str);
}

// header related functions:
const mmap_lib::str Cpp_parser::supporting_ftype() const {
  return supp_ftype; 
}

void      Cpp_parser::set_supporting_fstart(const mmap_lib::str basename_s) {
  supp_file_final_str = std::make_shared<File_output>(basename_s);
  auto header_includes = "#pragma once\n#include <string>\n#include \"vcd_writer.hpp\"\n"_str;
  supp_file_final_str->append( header_includes);
}

/*mmap_lib::str Cpp_parser::supporting_fend(const mmap_lib::str basename_s) const { 
  return mmap_lib::str::concat("<<EOF "_str, basename_s); 
}
*/
void Cpp_parser::set_supp_buffer_to_print(const mmap_lib::str modname) {
  supp_file_final_str->append("class "_str);
  supp_file_final_str->append(modname);
  supp_file_final_str->append("_sim {\npublic:\n  uint64_t hidx;\n  "_str);

  mmap_lib::str outps_nline;
  if (!outp_bw.empty()) {
    outps_nline = "struct {"_str;
    for (auto const &[key, val] : outp_bw) {
      outps_nline = mmap_lib::str::concat(outps_nline, "UInt<"_str, val, "> "_str, key, "; "_str);
    }
    outps_nline = outps_nline.append("} outputs;\n"_str);
  }

  mmap_lib::str regs_nline, regs_next_nline;
  if (!reg_bw.empty()) {
    regs_nline = "struct {"_str;
    for (auto const &[key, val] : reg_bw) {
      regs_nline = mmap_lib::str::concat(regs_nline, "UInt<"_str, val, "> "_str, key, "; "_str);
    }
    regs_next_nline = mmap_lib::str::concat(regs_next_nline, regs_nline, "} regs_next;"_str);
    regs_nline = regs_nline.append("} regs;"_str);
  }

  auto funcs = mmap_lib::str::concat("  "_str, modname, "_sim(uint64_t _hidx);\n  void reset_cycle();\n  void cycle("_str, inps_csv, ");\n"_str);

  auto vcd_params = "  std::string scope_name;\n  vcd::VCDWriter* vcd_writer;\n"_str;

  mmap_lib::str vcd_varptrs;
  for (auto const &[key, val] : inp_bw) {
    if (!  (val < 2))
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "["_str,
                      mmap_lib::str::concat(
                        (val.to_i() - 1),
                        ":0]\", vcd::VariableType::wire, "_str,
                        val,
                        ");\n"_str)
                      );
    else
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "\", vcd::VariableType::wire, "_str,
                      val,
                      ");\n"_str);
  }
  for (auto const &[key, val] : outp_bw) {
    if (!(val<2)) //was (val>"1") before mmap_lib::str 
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "["_str,
                      mmap_lib::str::concat(
                        (val.to_i() - 1),
                        ":0]\", vcd::VariableType::wire, "_str,
                        val,
                        ");\n"_str)
                      );
    else
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "\", vcd::VariableType::wire, "_str,
                      val,
                      ");\n"_str);
  }
  for (auto const &[key, val] : reg_bw) {
    if (!(val<2))
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "["_str,
                      mmap_lib::str::concat(
                        (val.to_i() - 1),
                        ":0]\", vcd::VariableType::wire, "_str,
                        val,
                        ");\n"_str)
                      );
    else
      vcd_varptrs = mmap_lib::str::concat(vcd_varptrs,
                      "  vcd::VarPtr vcd_"_str,
                      key,
                      " = vcd_writer->register_var(scope_name, \""_str,
                      key,
                      "\", vcd::VariableType::wire, "_str,
                      val,
                      ");\n"_str);
  }

  auto vcd_funcs = mmap_lib::str::concat("  "_str,
                                       modname,
                                       "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer);\n  void vcd_reset_cycle();\n  void vcd_posedge();\n  void vcd_negedge();\n  void vcd_comb("_str,
                                       inps_csv,
                                       ");\n"_str);
  // auto answer = absl::StrCat(header_strt, outps_nline, regs_nline, regs_next_nline, funcs, vcd_params, vcd_varptrs, vcd_funcs,
  // "\n};");
  supp_file_final_str->append(outps_nline);
  supp_file_final_str->append(regs_nline);
  supp_file_final_str->append(regs_next_nline);
  supp_file_final_str->append("\n#ifndef SIMLIB_VCD\n"_str);
  supp_file_final_str->append(funcs);
  supp_file_final_str->append("\n#else\n"_str);
  supp_file_final_str->append(vcd_params);
  supp_file_final_str->append(vcd_varptrs);
  supp_file_final_str->append(vcd_funcs);
  supp_file_final_str->append("\n#endif\n};"_str);
  //supp_file_final_str->append( answer);
//  return answer;
}

void Cpp_parser::set_main_fstart(const mmap_lib::str &basename, const mmap_lib::str &basename_s) {
  //auto txt_to_print = mmap_lib::str::concat("file: "_str, basename, "\n"_str);
  main_file_final_str = std::make_shared<File_output>(basename);
  main_file_final_str->append( "\n#include \"livesim_types.hpp\"\n#include \"", basename_s, "\"\n");
  //return mmap_lib::str::concat(txt_to_print);//, main_file_final_str);
}

bool Cpp_parser::set_convert_parameters(const mmap_lib::str &key, const mmap_lib::str &ref) {

  if (!Code_gen_all_lang::has_prefix(key)) {
    return false;
  }

  mmap_lib::str no_prefix_key;
  if (key[1]=='.')
    no_prefix_key = key.substr(2);
  else
    no_prefix_key = key.substr(1);

  assert(key.size() >= 1);
  if (key[0] == '$') {  // it is i/p

    if (no_prefix_key == "clock") {
      sys_clock      = no_prefix_key;
      sys_clock_bits = ref;
    }else{
      inp_bw.insert(std::pair<mmap_lib::str, mmap_lib::str>(no_prefix_key, ref));
      if (inps_csv.empty())
        inps_csv = mmap_lib::str::concat(inps_csv, "UInt<"_str, ref, "> "_str, no_prefix_key);
      else
        inps_csv = mmap_lib::str::concat(inps_csv, ", UInt<"_str, ref, "> "_str, no_prefix_key);
    }
  } else if (key[0] == '%') {  // it is o/p
    outp_bw.insert(std::pair<mmap_lib::str, mmap_lib::str>(no_prefix_key, ref));
  } else if (key[0] == '#') {  // it is register
    reg_bw.insert(std::pair<mmap_lib::str, mmap_lib::str>(no_prefix_key, ref));
  } else {
    assert(false);
  }

  return true;
}

void Prp_parser::dump_maps() const {
  fmt::print("printing the unsigned vector\n");
  for (const auto &elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Prp_parser::call_dump_maps() const { Prp_parser::dump_maps(); }
void Ver_parser::dump_maps() const {
  fmt::print("printing the unsigned vector\n");
  for (const auto &elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Ver_parser::call_dump_maps() const { Ver_parser::dump_maps(); }
void Cpp_parser::dump_maps() const {
  fmt::print("printing I/P bitwidth values:\n");
  for (const auto &elem : inp_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing O/P bitwidth values:\n");
  for (const auto &elem : outp_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing reg bitwidth values:\n");
  for (const auto &elem : reg_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing the unsigned vector\n");
  for (const auto &elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Cpp_parser::call_dump_maps() const { Cpp_parser::dump_maps(); }
int  Cpp_parser::indent_final_system() const { return 1; }

void Cpp_parser::set_for_vcd_comb(const mmap_lib::str key1, const mmap_lib::str key2) {
  buff_to_print_vcd = mmap_lib::str::concat(buff_to_print_vcd, "vcd_writer->change(vcd_"_str, key1, ", "_str, key2, ".to_string_binary());\n"_str);
}

void Cpp_parser::add_to_buff_vec_for_cpp(const mmap_lib::str s) {
  buff_vec_for_cpp.emplace_back(s);
}

void Cpp_parser::set_final_print(const mmap_lib::str &modname, std::shared_ptr<File_output>/* buffer_to_print*/) {
  // constructor
  auto constructor_vcd = mmap_lib::str::concat(modname,
                                             "_sim::"_str,
                                             modname,
                                             "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)\n  : hidx(_hidx)\n  , scope_name(parent_name.empty() ? \""_str,
                                             modname,
                                             "_sim\": parent_name+ \"."_str,
                                             modname,
                                             "_sim\")\n  , vcd_writer(writer) {\n}\n"_str);
  auto constructor     = mmap_lib::str::concat(modname, "_sim::"_str, modname, "_sim(uint64_t _hidx)\n  : hidx(_hidx) {\n}\n"_str);

  mmap_lib::str rst_vals_nline, rst_vals_nline_vcd;
  for (auto const &[key, val] : outp_bw) {
    rst_vals_nline = mmap_lib::str::concat(rst_vals_nline, "  outputs."_str, key, " = UInt<"_str, val, "> (0);\n"_str);
    rst_vals_nline_vcd = mmap_lib::str::concat(rst_vals_nline_vcd, "  vcd_writer->change(vcd_"_str, key, ", outputs."_str, key, ".to_string_binary());\n"_str);
  }
  for (auto const &[key, val] : reg_bw) {
    rst_vals_nline = mmap_lib::str::concat(rst_vals_nline, "  regs."_str, key, " = UInt<"_str, val, "> (0);\n"_str);
     rst_vals_nline_vcd= mmap_lib::str::concat(rst_vals_nline_vcd, "  vcd_writer->change(vcd_"_str, key, ", regs."_str, key, ".to_string_binary());\n"_str);
  }
  auto reset_vcd  =  mmap_lib::str::concat("void "_str, modname, "_sim::vcd_reset_cycle() {\n"_str, rst_vals_nline, rst_vals_nline_vcd, "}\n"_str);
  auto reset_func =  mmap_lib::str::concat("void "_str, modname, "_sim::reset_cycle() {\n"_str, rst_vals_nline, "}\n"_str);

  auto posedge_vcd = mmap_lib::str::concat( "void "_str, modname, "_sim::vcd_posedge(){\n"_str);
  auto negedge_vcd = mmap_lib::str::concat( "void "_str, modname, "_sim::vcd_negedge(){\n"_str);
  if (sys_clock != "") {
   posedge_vcd  = mmap_lib::str::concat(posedge_vcd, "  vcd_writer->change("_str, sys_clock, ", \"1\");\n"_str);
   negedge_vcd = mmap_lib::str::concat(negedge_vcd, "  vcd_writer->change("_str, sys_clock, ", \"0\");\n"_str);
  }
  posedge_vcd  = mmap_lib::str::concat(posedge_vcd, "}\n"_str);
  negedge_vcd = mmap_lib::str::concat(negedge_vcd, "}\n"_str);

  // main code part function
//  auto main_func_vcd  = mmap_lib::str::concat("void "_str, modname, "_sim::vcd_comb("_str, inps_csv, ") {\n"_str);
// // main_func_vcd =  mmap_lib::str::concat(main_func_vcd,buff_vec_for_cpp);// buffer_to_print);
// main_func_vcd =  mmap_lib::str::concat(main_func_vcd, "  "_str, buff_to_print_vcd, "\n}"_str);
//  auto main_func = mmap_lib::str::concat("void "_str, modname, "_sim::cycle("_str, inps_csv, ") {\n"_str);
////  main_func = mmap_lib::str::concat(main_func,buff_vec_for_cpp);// buffer_to_print);
// main_func = mmap_lib::str::concat(main_func,"\n}"_str);
//  auto        answer    = mmap_lib::str::concat("#ifdef SIMLIB_VCD\n",
//                                       constructor_vcd,
//                                       "\n",
//                                       reset_vcd,
//                                       mmap_lib::str::concat(
//                                       "\n",
//                                       posedge_vcd,
//                                       "\n",
//                                       negedge_vcd,
//                                       "\n"),
//                                       mmap_lib::str::concat(
//                                         main_func_vcd,
//                                         "\n#else\n",
//                                         constructor,
//                                         "\n",
//                                         reset_func,
//                                         "\n",
//                                         main_func,
//                                         "\n#endif\n")
//                                       );
  //main_file_final_str->append(answer);

  main_file_final_str->append("#ifdef SIMLIB_VCD\n"_str, constructor_vcd, "\n"_str);
  main_file_final_str->append(reset_vcd, "\n"_str);
  main_file_final_str->append(posedge_vcd, "\n"_str , negedge_vcd, "\n"_str );
  main_file_final_str->append("void "_str, modname, "_sim::vcd_comb("_str, inps_csv, ") {\n"_str);//main_func_vcd started
  for (auto i : buff_vec_for_cpp) {
    main_file_final_str->append(i);
  }
  main_file_final_str->append("  "_str, buff_to_print_vcd, "\n}"_str);//main_func_vcd ended
  main_file_final_str->append("\n#else\n"_str, constructor, "\n"_str);
  main_file_final_str->append(reset_func, "\n"_str);
  main_file_final_str->append(mmap_lib::str::concat("void "_str, modname, "_sim::cycle("_str, inps_csv, ") {\n"_str));//main_func started
  for (auto i : buff_vec_for_cpp) {
    main_file_final_str->append(i);
  }
  main_file_final_str->append("\n}"_str);//main_func ended
  main_file_final_str->append("\n#endif\n"_str);

}

void Prp_parser::set_final_print(const mmap_lib::str &, std::shared_ptr<File_output> buffer_to_print) {
  //return mmap_lib::str::concat(buffer_to_print, "\n"_str);
  buffer_to_print->append("\n"_str);
}

void Ver_parser::set_final_print(const mmap_lib::str &, std::shared_ptr<File_output> buffer_to_print) {
  //return mmap_lib::str::concat(buffer_to_print, "\n"_str);
  buffer_to_print->append("\n"_str);
}

// odir related functions:
/*void Prp_parser::result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const {
  auto file = absl::StrCat(odir.to_s(), "/", fname.to_s(), ".", lang_type.to_s());
  int  fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.code_gen unable to create {}", file);
    return;
  }
  size_t sz = write(fd, buffer_to_print.c_str(), buffer_to_print.size());
  if (sz != buffer_to_print.size()) {
    Pass::error("inou.code_gen unexpected write missmatch");
    return;
  }
  close(fd);
}
*/
mmap_lib::str Prp_parser::get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const {
  return mmap_lib::str::concat(odir, "/"_str, fname, "."_str, lang_type);
}

/*
void Cpp_parser::result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &) const {
  // for header file
  auto supp_f  = absl::StrCat(odir.to_s(), "/", fname.to_s(), ".", supp_ftype.to_s());
  int  supp_fd = ::open(supp_f.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (supp_fd < 0) {
    Pass::error("inou.code_gen unable to create header file {}", supp_f);
    return;
  }
  size_t supp_sz = write(supp_fd, supp_file_final_str.c_str(), supp_file_final_str.size());
  if (supp_sz != supp_file_final_str.size()) {
    Pass::error("inou.code_gen header file unexpected write missmatch");
    return;
  }
  close(supp_fd);

  // for cpp file
  auto file = absl::StrCat(odir.to_s(), "/", fname.to_s(), ".", lang_type);
  int  fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.code_gen unable to create {}", file);
    return;
  }
  size_t sz = write(fd, main_file_final_str.c_str(), main_file_final_str.size());
  if (sz != main_file_final_str.size()) {
    Pass::error("inou.code_gen unexpected write missmatch");
    return;
  }
  close(fd);
}
*/
mmap_lib::str Cpp_parser::get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const {
  // for header file
//  auto supp_f  = mmap_lib::str::concat(odir, "/"_str, fname, "."_str, supp_ftype);
//  return supp_f;

  // for cpp file
  auto file = mmap_lib::str::concat(odir, "/"_str, fname, "."_str, lang_type);
  return file;
}

/*
void Ver_parser::result_in_odir(const mmap_lib::str &fname, const mmap_lib::str &odir, const std::string &buffer_to_print) const {
  // TODO: currently as per prp. change as required.
  auto file = absl::StrCat(odir.to_s(), "/", fname.to_s(), ".", lang_type.to_s());
  int  fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    Pass::error("inou.code_gen unable to create {}", file);
    return;
  }
  size_t sz = write(fd, buffer_to_print.c_str(), buffer_to_print.size());
  if (sz != buffer_to_print.size()) {
    Pass::error("inou.code_gen unexpected write missmatch");
    return;
  }
  close(fd);
}
*/
mmap_lib::str Ver_parser::get_lang_fname(const mmap_lib::str &fname, const mmap_lib::str &odir) const {
  // TODO: currently as per prp. change as required.
  return mmap_lib::str::concat(odir, "/"_str, fname, "."_str, lang_type);
}

void Prp_parser::set_make_unsigned(const mmap_lib::str &sec_child) {
  unsigned_vars.insert(sec_child);
}

void Cpp_parser::set_make_unsigned(const mmap_lib::str &sec_child) {
  unsigned_vars.insert(sec_child);
}

void Ver_parser::set_make_unsigned(const mmap_lib::str &sec_child) {
  unsigned_vars.insert(sec_child);
}

bool Prp_parser::is_unsigned(const mmap_lib::str &var_name) const {
  return unsigned_vars.contains(var_name);
}

bool Cpp_parser::is_unsigned(const mmap_lib::str &var_name) const {
  return unsigned_vars.contains(var_name);
}

bool Ver_parser::is_unsigned(const mmap_lib::str &var_name) const {
  return unsigned_vars.contains(var_name);
}
