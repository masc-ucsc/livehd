
#include "lnast_generic_parser.hpp"

#include <unistd.h>

#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "code_gen_all_lang.hpp"
#include "lnast_map.hpp"

std::string Prp_parser::ref_name_str(std::string_view prp_term, bool) const { return std::string(prp_term); }

std::string Cpp_parser::ref_name_str(std::string_view prp_term, bool strct) const {
  if (!Code_gen_all_lang::has_prefix(prp_term)) {
    return std::string(prp_term);
  }

  if (Code_gen_all_lang::is_output(prp_term) && strct == true) {
    return absl::StrCat("outputs.", prp_term.substr(1));
  }

  return std::string(prp_term.substr(1));
}

std::string Ver_parser::ref_name_str(std::string_view prp_term, bool) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    return std::string(prp_term.substr(1));
  }

  return std::string(prp_term);
}

std::string Prp_parser::stmt_sep() const { return stmt_separator; }
std::string Cpp_parser::stmt_sep() const { return stmt_separator; }
std::string Ver_parser::stmt_sep() const { return stmt_separator; }

std::string Prp_parser::get_lang_type() const { return lang_type; }
std::string Cpp_parser::get_lang_type() const { return lang_type; }
std::string Ver_parser::get_lang_type() const { return lang_type; }

std::string Prp_parser::debug_name_lang(Lnast_ntype node_type) const {
  return std::string(Lnast_map::debug_name_pyrope(node_type));
}
std::string Cpp_parser::debug_name_lang(Lnast_ntype node_type) const { return std::string(Lnast_map::debug_name_cpp(node_type)); }
std::string Ver_parser::debug_name_lang(Lnast_ntype node_type) const {
  return std::string(Lnast_map::debug_name_verilog(node_type));
}

std::string Prp_parser::start_else_if() const { return ("} elif ("); }
std::string Cpp_parser::start_else_if() const { return ("} else if ("); }
std::string Ver_parser::start_else_if() const { return ("end else if ("); }

std::string Ver_parser::end_else_if() const { return ("end"); }

std::string Ver_parser::start_else() const { return ("end else begin\n"); }

std::string Ver_parser::end_cond() const { return (") begin\n"); }

std::string Ver_parser::end_if_or_else() const { return ("end\n"); }

std::string Ver_parser::for_stmt_beg() const { return "begin\n"; }
std::string Ver_parser::for_stmt_end() const { return "end\n"; }

std::string Prp_parser::for_cond_mid() const { return " in "; }
std::string Cpp_parser::for_cond_mid() const { return ": "; }
std::string Ver_parser::for_cond_mid() const {
  return ": ";  // TODO
}

std::string Prp_parser::for_cond_beg() const { return " "; }
std::string Cpp_parser::for_cond_beg() const { return " ( auto "; }
std::string Ver_parser::for_cond_beg() const {
  return " ( auto ";  // TODO
}

std::string Prp_parser::for_cond_end() const { return " "; }
std::string Cpp_parser::for_cond_end() const { return ") "; }
std::string Ver_parser::for_cond_end() const {
  return ") ";  // TODO
}

std::string Ver_parser::assign_node_strt() const { return "assign "; }

std::string Cpp_parser::starter(std::string_view filename) const { return absl::StrCat("void ", filename, "::cycle("); }

// header related functions:
std::string Cpp_parser::supporting_ftype() const { return supp_ftype; }

void Cpp_parser::set_supporting_fstart(std::string_view basename_s) {
  supp_file_final_str  = std::make_shared<File_output>(basename_s);
  auto header_includes = "#pragma once\n#include <string>\n#include \"vcd_writer.hpp\"\n";
  supp_file_final_str->append(header_includes);
}

/*std::string Cpp_parser::supporting_fend(const std::string basename_s) const {
  return absl::StrCat("<<EOF ", basename_s);
}
*/
void Cpp_parser::set_supp_buffer_to_print(std::string_view modname) {
  supp_file_final_str->append("class ");
  supp_file_final_str->append(modname);
  supp_file_final_str->append("_sim {\npublic:\n  uint64_t hidx;\n  ");

  std::string outps_nline;
  if (!outp_bw.empty()) {
    outps_nline = "struct {";
    for (auto const &[key, val] : outp_bw) {
      absl::StrAppend(&outps_nline, "UInt<", val, "> ", key, "; ");
    }
    outps_nline = outps_nline.append("} outputs;\n");
  }

  std::string regs_nline, regs_next_nline;
  if (!reg_bw.empty()) {
    regs_nline = "struct {";
    for (auto const &[key, val] : reg_bw) {
      absl::StrAppend(&regs_nline, "UInt<", val, "> ", key, "; ");
    }
    absl::StrAppend(&regs_next_nline, regs_nline, "} regs_next;");
    regs_nline = regs_nline.append("} regs;");
  }

  auto funcs = absl::StrCat("  ", modname, "_sim(uint64_t _hidx);\n  void reset_cycle();\n  void cycle(", inps_csv, ");\n");

  auto vcd_params = "  std::string scope_name;\n  vcd::VCDWriter* vcd_writer;\n";

  std::string vcd_varptrs;
  for (const auto &[key, val] : inp_bw) {
    auto i_val = str_tools::to_i(val);

    if (!(i_val < 2)) {
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      absl::StrCat(std::to_string(i_val - 1), ":0]\", vcd::VariableType::wire, ", val, ");\n"));
    } else {
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    }
  }
  for (auto const &[key, val] : outp_bw) {
    auto i_val = str_tools::to_i(val);
    if (!(i_val < 2)) {  // was (val>"1") before std::string
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      absl::StrCat(std::to_string(i_val - 1), ":0]\", vcd::VariableType::wire, ", val, ");\n"));
    } else {
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    }
  }
  for (auto const &[key, val] : reg_bw) {
    auto i_val = str_tools::to_i(val);
    if (!(i_val < 2)) {
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      absl::StrCat(std::to_string(i_val - 1), ":0]\", vcd::VariableType::wire, ", val, ");\n"));
    } else {
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    }
  }

  auto vcd_funcs = absl::StrCat("  ",
                                modname,
                                "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer);\n  void "
                                "vcd_reset_cycle();\n  void vcd_posedge();\n  void vcd_negedge();\n  void vcd_comb(",
                                inps_csv,
                                ");\n");
  // auto answer = absl::StrCat(header_strt, outps_nline, regs_nline, regs_next_nline, funcs, vcd_params, vcd_varptrs, vcd_funcs,
  // "\n};");
  supp_file_final_str->append(outps_nline);
  supp_file_final_str->append(regs_nline);
  supp_file_final_str->append(regs_next_nline);
  supp_file_final_str->append("\n#ifndef SIMLIB_VCD\n");
  supp_file_final_str->append(funcs);
  supp_file_final_str->append("\n#else\n");
  supp_file_final_str->append(vcd_params);
  supp_file_final_str->append(vcd_varptrs);
  supp_file_final_str->append(vcd_funcs);
  supp_file_final_str->append("\n#endif\n};");
  // supp_file_final_str->append( answer);
  //  return answer;
}

void Cpp_parser::set_main_fstart(std::string_view basename, std::string_view basename_s) {
  // auto txt_to_print = absl::StrCat("file: ", basename, "\n");
  main_file_final_str = std::make_shared<File_output>(basename);
  main_file_final_str->append("\n#include \"livesim_types.hpp\"\n#include \"", basename_s, "\"\n");
  // return absl::StrCat(txt_to_print);//, main_file_final_str);
}

bool Cpp_parser::set_convert_parameters(std::string_view key, std::string_view ref) {
  if (!Code_gen_all_lang::has_prefix(key)) {
    return false;
  }

  std::string no_prefix_key;
  if (key[1] == '.') {
    no_prefix_key = key.substr(2);
  } else {
    no_prefix_key = key.substr(1);
  }

  assert(key.size() >= 1);
  if (key[0] == '$') {  // it is i/p

    if (no_prefix_key == "clock") {
      sys_clock      = no_prefix_key;
      sys_clock_bits = ref;
    } else {
      inp_bw.insert(std::pair<std::string, std::string>(no_prefix_key, ref));
      if (inps_csv.empty()) {
        absl::StrAppend(&inps_csv, "UInt<", ref, "> ", no_prefix_key);
      } else {
        absl::StrAppend(&inps_csv, ", UInt<", ref, "> ", no_prefix_key);
      }
    }
  } else if (key[0] == '%') {  // it is o/p
    outp_bw.insert(std::pair<std::string, std::string>(no_prefix_key, ref));
  } else if (key[0] == '#') {  // it is register
    reg_bw.insert(std::pair<std::string, std::string>(no_prefix_key, ref));
  } else {
    assert(false);
  }

  return true;
}

void Prp_parser::dump_maps() const {
  std::cout << "printing the unsigned vector\n";
  for (const auto &elem : unsigned_vars) {
    std::print("var:{}\n", elem);
  }
}
void Prp_parser::call_dump_maps() const { Prp_parser::dump_maps(); }
void Ver_parser::dump_maps() const {
  std::cout << "printing the unsigned vector\n";
  for (const auto &elem : unsigned_vars) {
    std::print("var:{}\n", elem);
  }
}
void Ver_parser::call_dump_maps() const { Ver_parser::dump_maps(); }
void Cpp_parser::dump_maps() const {
  std::cout << "printing I/P bitwidth values:\n";
  for (const auto &elem : inp_bw) {
    std::print("\tkey: {}, value: {}\n", elem.first, elem.second);
  }

  std::cout << "printing O/P bitwidth values:\n";
  for (const auto &elem : outp_bw) {
    std::print("\tkey: {}, value: {}\n", elem.first, elem.second);
  }

  std::cout << "printing reg bitwidth values:\n";
  for (const auto &elem : reg_bw) {
    std::print("\tkey: {}, value: {}\n", elem.first, elem.second);
  }

  std::cout << "printing the unsigned vector\n";
  for (const auto &elem : unsigned_vars) {
    std::print("var:{}\n", elem);
  }
}
void Cpp_parser::call_dump_maps() const { Cpp_parser::dump_maps(); }
int  Cpp_parser::indent_final_system() const { return 1; }

void Cpp_parser::set_for_vcd_comb(std::string_view key1, std::string_view key2) {
  absl::StrAppend(&buff_to_print_vcd, "vcd_writer->change(vcd_", key1, ", ", key2, ".to_string_binary());\n");
}

void Cpp_parser::add_to_buff_vec_for_cpp(std::string_view s) { buff_vec_for_cpp.emplace_back(s); }

void Cpp_parser::set_final_print(std::string_view modname, std::shared_ptr<File_output> /* buffer_to_print*/) {
  // constructor
  auto constructor_vcd = absl::StrCat(modname,
                                      "_sim::",
                                      modname,
                                      "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)\n  : "
                                      "hidx(_hidx)\n  , scope_name(parent_name.empty() ? \"",
                                      modname,
                                      "_sim\": parent_name+ \".",
                                      modname,
                                      "_sim\")\n  , vcd_writer(writer) {\n}\n");
  auto constructor     = absl::StrCat(modname, "_sim::", modname, "_sim(uint64_t _hidx)\n  : hidx(_hidx) {\n}\n");

  std::string rst_vals_nline, rst_vals_nline_vcd;
  for (auto const &[key, val] : outp_bw) {
    absl::StrAppend(&rst_vals_nline, "  outputs.", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "  vcd_writer->change(vcd_", key, ", outputs.", key, ".to_string_binary());\n");
  }
  for (auto const &[key, val] : reg_bw) {
    absl::StrAppend(&rst_vals_nline, "  regs.", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "  vcd_writer->change(vcd_", key, ", regs.", key, ".to_string_binary());\n");
  }
  auto reset_vcd  = absl::StrCat("void ", modname, "_sim::vcd_reset_cycle() {\n", rst_vals_nline, rst_vals_nline_vcd, "}\n");
  auto reset_func = absl::StrCat("void ", modname, "_sim::reset_cycle() {\n", rst_vals_nline, "}\n");

  auto posedge_vcd = absl::StrCat("void ", modname, "_sim::vcd_posedge(){\n");
  auto negedge_vcd = absl::StrCat("void ", modname, "_sim::vcd_negedge(){\n");
  if (sys_clock != "") {
    absl::StrAppend(&posedge_vcd, "  vcd_writer->change(", sys_clock, ", \"1\");\n");
    absl::StrAppend(&negedge_vcd, "  vcd_writer->change(", sys_clock, ", \"0\");\n");
  }
  absl::StrAppend(&posedge_vcd, "}\n");
  absl::StrAppend(&negedge_vcd, "}\n");

  // main code part function
  //  auto main_func_vcd  = absl::StrCat("void ", modname, "_sim::vcd_comb(", inps_csv, ") {\n");
  // // main_func_vcd =  absl::StrCat(main_func_vcd,buff_vec_for_cpp);// buffer_to_print);
  // main_func_vcd =  absl::StrCat(main_func_vcd, "  ", buff_to_print_vcd, "\n}");
  //  auto main_func = absl::StrCat("void ", modname, "_sim::cycle(", inps_csv, ") {\n");
  ////  main_func = absl::StrCat(main_func,buff_vec_for_cpp);// buffer_to_print);
  // main_func = absl::StrCat(main_func,"\n}");
  //  auto        answer    = absl::StrCat("#ifdef SIMLIB_VCD\n",
  //                                       constructor_vcd,
  //                                       "\n",
  //                                       reset_vcd,
  //                                       absl::StrCat(
  //                                       "\n",
  //                                       posedge_vcd,
  //                                       "\n",
  //                                       negedge_vcd,
  //                                       "\n"),
  //                                       absl::StrCat(
  //                                         main_func_vcd,
  //                                         "\n#else\n",
  //                                         constructor,
  //                                         "\n",
  //                                         reset_func,
  //                                         "\n",
  //                                         main_func,
  //                                         "\n#endif\n")
  //                                       );
  // main_file_final_str->append(answer);

  main_file_final_str->append("#ifdef SIMLIB_VCD\n", constructor_vcd, "\n");
  main_file_final_str->append(reset_vcd, "\n");
  main_file_final_str->append(posedge_vcd, "\n", negedge_vcd, "\n");
  main_file_final_str->append("void ", modname, "_sim::vcd_comb(", inps_csv, ") {\n");  // main_func_vcd started
  for (auto i : buff_vec_for_cpp) {
    main_file_final_str->append(i);
  }
  main_file_final_str->append("  ", buff_to_print_vcd, "\n}");  // main_func_vcd ended
  main_file_final_str->append("\n#else\n", constructor, "\n");
  main_file_final_str->append(reset_func, "\n");
  main_file_final_str->append(absl::StrCat("void ", modname, "_sim::cycle(", inps_csv, ") {\n"));  // main_func started
  for (auto i : buff_vec_for_cpp) {
    main_file_final_str->append(i);
  }
  main_file_final_str->append("\n}");  // main_func ended
  main_file_final_str->append("\n#endif\n");
}

void Prp_parser::set_final_print(std::string_view, std::shared_ptr<File_output> buffer_to_print) {
  // return absl::StrCat(buffer_to_print, "\n");
  buffer_to_print->append("\n");
}

void Ver_parser::set_final_print(std::string_view, std::shared_ptr<File_output> buffer_to_print) {
  // return absl::StrCat(buffer_to_print, "\n");
  buffer_to_print->append("\n");
}

// odir related functions:
/*void Prp_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const {
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
std::string Prp_parser::get_lang_fname(std::string_view fname, std::string_view odir) const {
  return absl::StrCat(odir, "/", fname, ".", lang_type);
}

/*
void Cpp_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &) const {
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
std::string Cpp_parser::get_lang_fname(std::string_view fname, std::string_view odir) const {
  // for header file
  //  auto supp_f  = absl::StrCat(odir, "/", fname, ".", supp_ftype);
  //  return supp_f;

  // for cpp file
  auto file = absl::StrCat(odir, "/", fname, ".", lang_type);
  return file;
}

/*
void Ver_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const {
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
std::string Ver_parser::get_lang_fname(std::string_view fname, std::string_view odir) const {
  // TODO: currently as per prp. change as required.
  return absl::StrCat(odir, "/", fname, ".", lang_type);
}

void Prp_parser::set_make_unsigned(std::string_view sec_child) { unsigned_vars.insert(std::string(sec_child)); }

void Cpp_parser::set_make_unsigned(std::string_view sec_child) { unsigned_vars.insert(std::string(sec_child)); }

void Ver_parser::set_make_unsigned(std::string_view sec_child) { unsigned_vars.insert(std::string(sec_child)); }

bool Prp_parser::is_unsigned(std::string_view var_name) const { return unsigned_vars.contains(var_name); }

bool Cpp_parser::is_unsigned(std::string_view var_name) const { return unsigned_vars.contains(var_name); }

bool Ver_parser::is_unsigned(std::string_view var_name) const { return unsigned_vars.contains(var_name); }
