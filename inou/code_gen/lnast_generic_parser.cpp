
#include "lnast_generic_parser.hpp"

#include <unistd.h>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "code_gen_all_lang.hpp"
#include "lnast_map.hpp"

std::string Prp_parser::ref_name(const std::string &prp_term, bool) const { return prp_term; }

std::string Prp_parser::ref_name(std::string_view prp_term, bool) const { return std::string(prp_term); }

std::string Cpp_parser::ref_name(const std::string &prp_term, bool strct) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    if (Code_gen_all_lang::is_output(prp_term) && strct == true) {
      return (absl::StrCat("outputs.", prp_term.substr(1)));
    }
    return prp_term.substr(1);
  } else
    return prp_term;
}

std::string Cpp_parser::ref_name(std::string_view prp_term, bool strct) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    std::string _prp_term = std::string(prp_term);
    if (Code_gen_all_lang::is_output(prp_term) && strct == true) {
      return (absl::StrCat("outputs.", _prp_term.substr(1)));
    }
    return _prp_term.substr(1);
  } else
    return std::string(prp_term);
}

std::string Ver_parser::ref_name(const std::string &prp_term, bool) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    return prp_term.substr(1);
  } else
    return prp_term;
}

std::string Ver_parser::ref_name(std::string_view prp_term, bool) const {
  if (Code_gen_all_lang::has_prefix(prp_term)) {
    std::string _prp_term = std::string(prp_term);
    return _prp_term.substr(1);
  } else
    return std::string(prp_term);
}

std::string_view Prp_parser::stmt_sep() const { return stmt_separator; }
std::string_view Cpp_parser::stmt_sep() const { return stmt_separator; }
std::string_view Ver_parser::stmt_sep() const { return stmt_separator; }

std::string_view Prp_parser::get_lang_type() const { return lang_type; }
std::string_view Cpp_parser::get_lang_type() const { return lang_type; }
std::string_view Ver_parser::get_lang_type() const { return lang_type; }

std::string_view Prp_parser::debug_name_lang(Lnast_ntype node_type) const { return Lnast_map::debug_name_pyrope(node_type); }
std::string_view Cpp_parser::debug_name_lang(Lnast_ntype node_type) const { return Lnast_map::debug_name_cpp(node_type); }
std::string_view Ver_parser::debug_name_lang(Lnast_ntype node_type) const { return Lnast_map::debug_name_verilog(node_type); }

std::string_view Prp_parser::start_else_if() const { return ("} elif ("); }
std::string_view Cpp_parser::start_else_if() const { return ("} else if ("); }
std::string_view Ver_parser::start_else_if() const { return ("end else if ("); }

std::string_view Ver_parser::end_else_if() const { return ("end"); }

std::string_view Ver_parser::start_else() const { return ("end else begin\n"); }

std::string_view Ver_parser::end_cond() const { return (") begin\n"); }

std::string_view Ver_parser::end_if_or_else() const { return ("end\n"); }

std::string_view Ver_parser::for_stmt_beg() const { return "begin\n"; }
std::string_view Ver_parser::for_stmt_end() const { return "end\n"; }

std::string_view Prp_parser::for_cond_mid() const { return " in "; }
std::string_view Cpp_parser::for_cond_mid() const { return ": "; }
std::string_view Ver_parser::for_cond_mid() const {
  return ": ";  // TODO
}

std::string_view Prp_parser::for_cond_beg() const { return " "; }
std::string_view Cpp_parser::for_cond_beg() const { return " ( auto "; }
std::string_view Ver_parser::for_cond_beg() const {
  return " ( auto ";  // TODO
}

std::string_view Prp_parser::for_cond_end() const { return " "; }
std::string_view Cpp_parser::for_cond_end() const { return ") "; }
std::string_view Ver_parser::for_cond_end() const {
  return ") ";  // TODO
}

std::string_view Ver_parser::assign_node_strt() const { return "assign "; }

std::string Cpp_parser::starter(std::string_view filename) const {
  std::string _filename = std::string(filename);
  return absl::StrCat("void ", _filename, "::cycle(");
}

// header related functions:
std::string_view Cpp_parser::supporting_ftype() const { return supp_ftype; }
std::string      Cpp_parser::set_supporting_fstart(const std::string &basename_s) {
  auto txt_to_print    = absl::StrCat("file: ", basename_s, "\n");
  auto header_includes = "#pragma once\n#include <string>\n#include \"vcd_writer.hpp\"\n";
  absl::StrAppend(&supp_file_final_str, header_includes);
  return absl::StrCat(txt_to_print, header_includes);
}
std::string Cpp_parser::supporting_fend(const std::string &basename_s) const { return absl::StrCat("<<EOF ", basename_s); }
std::string Cpp_parser::set_supp_buffer_to_print(const std::string &modname) {
  std::string header_strt = absl::StrCat("class ", modname, "_sim {\npublic:\n  uint64_t hidx;\n  ");

  std::string outps_nline;
  if (!outp_bw.empty()) {
    outps_nline = "struct {";
    for (auto const &[key, val] : outp_bw) {
      absl::StrAppend(&outps_nline, "UInt<", val, "> ", key, "; ");
    }
    absl::StrAppend(&outps_nline, "} outputs;\n");
  }

  std::string regs_nline, regs_next_nline;
  if (!reg_bw.empty()) {
    regs_nline = "struct {";
    for (auto const &[key, val] : reg_bw) {
      absl::StrAppend(&regs_nline, "UInt<", val, "> ", key, "; ");
    }
    absl::StrAppend(&regs_next_nline, regs_nline, "} regs_next;");
    absl::StrAppend(&regs_nline, "} regs;");
  }

  std::string funcs = absl::StrCat("  ", modname, "_sim(uint64_t _hidx);\n  void reset_cycle();\n  void cycle(", inps_csv, ");\n");

  std::string vcd_params = "  std::string scope_name;\n  vcd::VCDWriter* vcd_writer;\n";

  std::string vcd_varptrs;
  for (auto const &[key, val] : inp_bw) {
    if (val > "1")
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      std::to_string(std::stoi(val) - 1),
                      ":0]\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    else
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
  }
  for (auto const &[key, val] : outp_bw) {
    if (val > "1")
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      std::to_string(std::stoi(val) - 1),
                      ":0]\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    else
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
  }
  for (auto const &[key, val] : reg_bw) {
    if (val > "1")
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "[",
                      std::to_string(std::stoi(val) - 1),
                      ":0]\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
    else
      absl::StrAppend(&vcd_varptrs,
                      "  vcd::VarPtr vcd_",
                      key,
                      " = vcd_writer->register_var(scope_name, \"",
                      key,
                      "\", vcd::VariableType::wire, ",
                      val,
                      ");\n");
  }

  std::string vcd_funcs = absl::StrCat("  ",
                                       modname,
                                       "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer);\n  void "
                                       "vcd_reset_cycle();\n  void vcd_posedge();\n  void vcd_negedge();\n  void vcd_comb(",
                                       inps_csv,
                                       ");\n");
  // auto answer = absl::StrCat(header_strt, outps_nline, regs_nline, regs_next_nline, funcs, vcd_params, vcd_varptrs, vcd_funcs,
  // "\n};");
  auto answer = absl::StrCat(header_strt,
                             outps_nline,
                             regs_nline,
                             regs_next_nline,
                             "\n#ifndef SIMLIB_VCD\n",
                             funcs,
                             "\n#else\n",
                             vcd_params,
                             vcd_varptrs,
                             vcd_funcs,
                             "\n#endif\n};");
  absl::StrAppend(&supp_file_final_str, answer);
  return answer;
}

std::string Cpp_parser::set_main_fstart(const std::string &basename, const std::string &basename_s) {
  auto txt_to_print = absl::StrCat("file: ", basename, "\n");
  absl::StrAppend(&main_file_final_str, "\n#include \"livesim_types.hpp\"\n#include \"", basename_s, "\"\n");
  return absl::StrCat(txt_to_print, main_file_final_str);
}

bool Cpp_parser::set_convert_parameters(const std::string &key, const std::string &ref) {
  // convert parameters to cpp format
  // currently supports .__bits only
  assert(key.size() >= 1);
  if (key.find("$") == 0) {  // it is i/p
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("$."));
    if (_key[1].find("clock") == std::string::npos) {
      inp_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
      if (inps_csv.empty())
        absl::StrAppend(&inps_csv, "UInt<", ref, "> ", _key[1]);
      else
        absl::StrAppend(&inps_csv, ", UInt<", ref, "> ", _key[1]);
    } else {  // TODO: add for reset also
      sys_clock      = _key[1];
      sys_clock_bits = ref;
    }
  } else if (key.find("%") == 0) {  // it is o/p
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("%."));
    outp_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
  } else if (key.find("#") == 0) {  // it is register
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("#."));
    reg_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
  } else {  // TODO: print error!
    return false;
  }
  return true;
}

void Prp_parser::dump_maps() const {
  fmt::print("printing the unsigned vector\n");
  for (auto elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Prp_parser::call_dump_maps() const { Prp_parser::dump_maps(); }
void Ver_parser::dump_maps() const {
  fmt::print("printing the unsigned vector\n");
  for (auto elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Ver_parser::call_dump_maps() const { Ver_parser::dump_maps(); }
void Cpp_parser::dump_maps() const {
  fmt::print("printing I/P bitwidth values:\n");
  for (auto elem : inp_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing O/P bitwidth values:\n");
  for (auto elem : outp_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing reg bitwidth values:\n");
  for (auto elem : reg_bw) fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing the unsigned vector\n");
  for (auto elem : unsigned_vars) fmt::print("var:{}\n", elem);
}
void Cpp_parser::call_dump_maps() const { Cpp_parser::dump_maps(); }
int  Cpp_parser::indent_final_system() const { return 1; }

void Cpp_parser::set_for_vcd_comb(std::string_view key1, std::string_view key2) {
  absl::StrAppend(&buff_to_print_vcd, "vcd_writer->change(vcd_", key1, ", ", key2, ".to_string_binary());\n");
}

std::string Cpp_parser::set_final_print(const std::string &modname, const std::string &buffer_to_print) {
  // constructor
  std::string constructor_vcd = absl::StrCat(modname,
                                             "_sim::",
                                             modname,
                                             "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)\n  : "
                                             "hidx(_hidx)\n  , scope_name(parent_name.empty() ? \"",
                                             modname,
                                             "_sim\": parent_name+ \".",
                                             modname,
                                             "_sim\")\n  , vcd_writer(writer) {\n}\n");
  std::string constructor     = absl::StrCat(modname, "_sim::", modname, "_sim(uint64_t _hidx)\n  : hidx(_hidx) {\n}\n");

  std::string rst_vals_nline;
  std::string rst_vals_nline_vcd;
  for (auto const &[key, val] : outp_bw) {
    absl::StrAppend(&rst_vals_nline, "  outputs.", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "  vcd_writer->change(vcd_", key, ", outputs.", key, ".to_string_binary());\n");
  }
  for (auto const &[key, val] : reg_bw) {
    absl::StrAppend(&rst_vals_nline, "  regs.", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "  vcd_writer->change(vcd_", key, ", regs.", key, ".to_string_binary());\n");
  }
  std::string reset_vcd  = absl::StrCat("void ", modname, "_sim::vcd_reset_cycle() {\n", rst_vals_nline, rst_vals_nline_vcd, "}\n");
  std::string reset_func = absl::StrCat("void ", modname, "_sim::reset_cycle() {\n", rst_vals_nline, "}\n");

  std::string posedge_vcd = absl::StrCat("void ", modname, "_sim::vcd_posedge(){\n");
  std::string negedge_vcd = absl::StrCat("void ", modname, "_sim::vcd_negedge(){\n");
  if (sys_clock != "") {
    absl::StrAppend(&posedge_vcd, "  vcd_writer->change(", sys_clock, ", \"1\");\n");
    absl::StrAppend(&negedge_vcd, "  vcd_writer->change(", sys_clock, ", \"0\");\n");
  }
  absl::StrAppend(&posedge_vcd, "}\n");
  absl::StrAppend(&negedge_vcd, "}\n");

  // main code part function
  std::string main_func_vcd
      = absl::StrCat("void ", modname, "_sim::vcd_comb(", inps_csv, ") {\n", buffer_to_print, "  ", buff_to_print_vcd, "\n}");
  std::string main_func = absl::StrCat("void ", modname, "_sim::cycle(", inps_csv, ") {\n", buffer_to_print, "\n}");
  auto        answer    = absl::StrCat("#ifdef SIMLIB_VCD\n",
                             constructor_vcd,
                             "\n",
                             reset_vcd,
                             "\n",
                             posedge_vcd,
                             "\n",
                             negedge_vcd,
                             "\n",
                             main_func_vcd,
                             "\n#else\n",
                             constructor,
                             "\n",
                             reset_func,
                             "\n",
                             main_func,
                             "\n#endif\n");
  absl::StrAppend(&main_file_final_str, answer);
  return answer;
}

std::string Prp_parser::set_final_print(const std::string &, const std::string &buffer_to_print) {
  return absl::StrCat(buffer_to_print, "\n");
}

std::string Ver_parser::set_final_print(const std::string &, const std::string &buffer_to_print) {
  return absl::StrCat(buffer_to_print, "\n");
}

// odir related functions:
void Prp_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const {
  auto file = absl::StrCat(odir, "/", fname, ".", lang_type);
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

void Cpp_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &) const {
  // for header file
  auto supp_f  = absl::StrCat(odir, "/", fname, ".", supp_ftype);
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
  auto file = absl::StrCat(odir, "/", fname, ".", lang_type);
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

void Ver_parser::result_in_odir(std::string_view fname, std::string_view odir, const std::string &buffer_to_print) const {
  // TODO: currently as per prp. change as required.
  auto file = absl::StrCat(odir, "/", fname, ".", lang_type);
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

std::string Prp_parser::set_make_unsigned(const std::string &sec_child) {
  unsigned_vars.push_back(sec_child);
  return absl::StrCat(sec_child, ".__unsigned = true", std::string(stmt_sep()));
}

std::string Cpp_parser::set_make_unsigned(const std::string &sec_child) {
  unsigned_vars.push_back(sec_child);
  return "";
}
std::string Ver_parser::set_make_unsigned(const std::string &sec_child) {
  unsigned_vars.push_back(sec_child);
  return "";
}

bool Prp_parser::is_unsigned(const std::string &var_name) const {
  return (std::find(unsigned_vars.begin(), unsigned_vars.end(), var_name) != unsigned_vars.end());
}

bool Cpp_parser::is_unsigned(const std::string &var_name) const {
  return (std::find(unsigned_vars.begin(), unsigned_vars.end(), var_name) != unsigned_vars.end());
}
bool Ver_parser::is_unsigned(const std::string &var_name) const {
  return (std::find(unsigned_vars.begin(), unsigned_vars.end(), var_name) != unsigned_vars.end());
}
