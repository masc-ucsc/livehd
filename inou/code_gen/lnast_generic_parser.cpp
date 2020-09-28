
#include "lnast_generic_parser.hpp"
#include <unistd.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

std::string Prp_parser::ref_name(std::string prp_term){
    return prp_term;
}

std::string Prp_parser::ref_name(std::string_view prp_term){
    return std::string(prp_term);
}

std::string_view Prp_parser::stmt_sep(){
  return stmt_separator;
}
std::string_view Cpp_parser::stmt_sep(){
  return stmt_separator;
}
std::string_view Ver_parser::stmt_sep(){
  return stmt_separator;
}

std::string_view Prp_parser::get_lang_type(){
  return lang_type;
}
std::string_view Cpp_parser::get_lang_type(){
  return lang_type;
}
std::string_view Ver_parser::get_lang_type(){
  return lang_type;
}


std::string_view Prp_parser::debug_name_lang(Lnast_ntype node_type){
  return node_type.debug_name_pyrope();
}
std::string_view Cpp_parser::debug_name_lang(Lnast_ntype node_type){
  return node_type.debug_name_cpp();
}
std::string_view Ver_parser::debug_name_lang(Lnast_ntype node_type){
  return node_type.debug_name_verilog();
}


std::string_view Prp_parser::start_else_if(){
  return ("} elif (");
}
std::string_view Cpp_parser::start_else_if(){
  return ("} else if (");
}
std::string_view Ver_parser::start_else_if(){
  return ("end else if (");
}

std::string_view Ver_parser::end_else_if(){
  return ("end");
}

std::string_view Ver_parser::start_else(){
  return ("end else begin\n");
}


std::string_view Ver_parser::end_cond(){
  return (") begin\n");
}

std::string_view Ver_parser::end_if_or_else(){
  return ("end\n");
}

std::string_view Ver_parser::for_stmt_beg() {return "begin\n";}
std::string_view Ver_parser::for_stmt_end() {return "end\n";}

std::string_view Prp_parser::for_cond_mid(){
  return " in ";
}
std::string_view Cpp_parser::for_cond_mid(){
  return ": ";
}
std::string_view Ver_parser::for_cond_mid(){
  return ": ";//TODO
}

std::string_view Prp_parser::for_cond_beg(){
  return " ";
}
std::string_view Cpp_parser::for_cond_beg(){
  return " ( auto ";
}
std::string_view Ver_parser::for_cond_beg(){
  return " ( auto ";//TODO
}

std::string_view Prp_parser::for_cond_end(){
  return " ";
}
std::string_view Cpp_parser::for_cond_end(){
  return ") ";
}
std::string_view Ver_parser::for_cond_end(){
  return ") ";//TODO
}

std::string_view Ver_parser::assign_node_strt() {
  return "assign ";
}

std::string Cpp_parser::starter(std::string_view filename){
  std::string _filename = std::string (filename);
  return absl::StrCat("void ", _filename, "::cycle(");
}

//header related functions:
std::string_view Cpp_parser::supporting_ftype(){
  return supp_ftype;
}
std::string Cpp_parser::supporting_fstart(std::string basename_s){
  auto txt_to_print = absl::StrCat("file: ", basename_s, "\n");
  auto header_includes = "#pragma once\n#include \"vcd_writer.hpp\"\n";
  absl::StrAppend(&supp_file_final_str, header_includes);
  return absl::StrCat(txt_to_print, header_includes);
}
std::string Cpp_parser::supporting_fend(std::string basename_s){
  return absl::StrCat("<<EOF ", basename_s);
}
std::string Cpp_parser::supp_buffer_to_print(std::string modname) {
  std::string header_strt = absl::StrCat("class ", modname, "_sim {\npublic:\n  uint64_t hidx;\n  ");

  std::string outps_nline;
  for (auto const& [key, val] : outp_bw) {
    absl::StrAppend(&outps_nline, "UInt<", val, "> ", key, ";\n  ");
  }

//  std::string inps_csv;
  for (auto const& [key, val] : inp_bw) {
    absl::StrAppend(&inps_csv, "UInt<", val, "> ", key, ", ");
  }
  inps_csv.pop_back();
  inps_csv.pop_back();

  std::string funcs = absl::StrCat(modname, "_sim(uint64_t _hidx);\n  void reset_cycle();\n  void cycle(", inps_csv, ");\n");

  std::string vcd_params = "  std::string scope_name;\n  vcd::VCDWriter* vcd_writer;\n";

  std::string vcd_varptrs;
  for (auto const& [key, val] : inp_bw) {
    if (val>"1")
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_passed_var(scope_name, \"", key, "[", std::to_string(std::stoi(val)-1), ":0]\", vcd::VariableType::wire, ", val, ");\n");
    else
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_passed_var(scope_name, \"", key, "\", vcd::VariableType::wire, ", val, ");\n");
  }
  for (auto const& [key,val] : outp_bw) {
    if (val>"1")
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_passed_var(scope_name, \"", key, "[", std::to_string(std::stoi(val)-1), ":0]\", vcd::VariableType::wire, ", val, ");\n");
    else
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_passed_var(scope_name, \"", key, "\", vcd::VariableType::wire, ", val, ");\n");
  }
  for (auto const& [key,val] : reg_bw) {
    if (val>"1")
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_var(scope_name, \"", key, "[", std::to_string(std::stoi(val)-1), ":0]\", vcd::VariableType::wire, ", val, ");\n");
    else
      absl::StrAppend(&vcd_varptrs, "  vcd::VarPtr vcd_", key, " = vcd_writer->register_var(scope_name, \"", key, "\", vcd::VariableType::wire, ", val, ");\n");
  }


  std::string vcd_funcs = absl::StrCat("  ", modname, "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer);\n  void vcd_reset_cycle();\n  void vcd_posedge();\n  void vcd_negedge();\n  void vcd_comb(", inps_csv, ");\n");
  auto answer = absl::StrCat(header_strt, outps_nline, funcs, vcd_params, vcd_varptrs, vcd_funcs, "\n};");
  absl::StrAppend(&supp_file_final_str, answer);
  return answer;
}

std::string Cpp_parser::main_fstart(std::string basename, std::string basename_s) {
  auto txt_to_print = absl::StrCat("file: ", basename, "\n");
  absl::StrAppend(&main_file_final_str, "\n#include \"livesim_types.hpp\"\n#include \"", basename_s,"\"\n");
  return absl::StrCat(txt_to_print, main_file_final_str);
}

bool Cpp_parser::convert_parameters(std::string key, std::string ref) {
  //convert parameters to cpp format 
  //currently supports .__bits only
  assert(key.size()>=1);
  if(key.find("$")==0) {//it is i/p
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("$."));
    if (_key[1].find("clock")==std::string::npos) {
      inp_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
    } else {//TODO: add for reset also
      sys_clock = _key[1];
      sys_clock_bits = ref;
    }
  } else if (key.find("%")==0) {//it is o/p
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("%."));
    outp_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
  } else if(key.find("#")==0){//it is register
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("#."));
    reg_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
  } else {//TODO: print error!
    return false;
  }
  return true;
}

void Cpp_parser::get_maps() {
  fmt::print("printing I/P bitwidth values:\n");
  for (auto elem : inp_bw)
    fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);

  fmt::print("printing O/P bitwidth values:\n");                      
  for (auto elem : outp_bw)                                            
    fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);    

  fmt::print("printing reg bitwidth values:\n");                      
  for (auto elem : reg_bw)                                            
    fmt::print("\tkey: {}, value: {}\n", elem.first, elem.second);    
}
void Cpp_parser::call_get_maps() {
  Cpp_parser::get_maps();
}
int Cpp_parser::indent_final_system() {
  return 1;
}

void Cpp_parser::for_vcd_comb(std::string_view key) {
    absl::StrAppend(&buff_to_print_vcd, "vcd_writer->change(vcd_", key, ", ", key, ".to_string_binary());\n");

}

std::string Cpp_parser::final_print(std::string modname, std::string buffer_to_print) {
  //constructor
  std::string constructor_vcd = absl::StrCat(modname, "_sim::", modname, "_sim(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)\n  : hidx(_hidx)\n  , scope_name(parent_name.empty() ? \"", modname, "_sim\": parent_name, \".", modname, "_sim\")\n  , vcd_writer(writer) {\n}\n");
  std::string constructor = absl::StrCat(modname, "_sim::", modname, "_sim(uint64_t _hidx)\n  : hidx(_hidx) {\n}\n");

  std::string rst_vals_nline;
  std::string rst_vals_nline_vcd;
  for (auto const& [key, val] : outp_bw) {
    absl::StrAppend(&rst_vals_nline, "  ", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "vcd_writer->change(vcd_", key, ", ", key, ".to_string_binary());\n");
  }
  for (auto const& [key, val]:reg_bw) {
    absl::StrAppend(&rst_vals_nline, "  ", key, " = UInt<", val, "> (0);\n");
    absl::StrAppend(&rst_vals_nline_vcd, "vcd_writer->change(vcd_", key, ", ", key, ".to_string_binary());\n");
  }
  std::string reset_vcd = absl::StrCat("void ", modname, "::reset_cycle() {\n", rst_vals_nline, rst_vals_nline_vcd, "}\n");
  std::string reset_func = absl::StrCat("void ", modname, "::vcd_reset_cycle() {\n", rst_vals_nline, "}\n");

  std::string posedge_vcd = absl::StrCat("void ", modname, "::vcd_posedge(){\n  vcd_writer->change(", sys_clock, ", \"1\");\n}\n");
  std::string negedge_vcd = absl::StrCat("void ", modname, "::vcd_negedge(){\n  vcd_writer->change(", sys_clock, ", \"0\");\n}\n");

  //main code part function
  std::string main_func_vcd = absl::StrCat("void "+ modname+"_sim::vcd_comb(", inps_csv, ") {\n"+ buffer_to_print+ buff_to_print_vcd, "\n}");
  std::string main_func = absl::StrCat("void "+ modname+"_sim::cycle(", inps_csv, ") {\n"+ buffer_to_print+"\n}");
  auto answer = absl::StrCat(constructor_vcd, "\n", constructor, "\n", reset_vcd, "\n", reset_func, "\n", posedge_vcd, "\n", negedge_vcd, "\n",  main_func, "\n", main_func_vcd, "\n");
  absl::StrAppend(&main_file_final_str, answer);
  return answer;
}

std::string Prp_parser::final_print(std::string , std::string buffer_to_print) {
 return absl::StrCat(buffer_to_print+"\n");
}

std::string Ver_parser::final_print(std::string, std::string buffer_to_print) {
 return absl::StrCat(buffer_to_print+"\n");
}


//odir related functions:
void Prp_parser::result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) {
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

void Cpp_parser::result_in_odir(std::string_view fname, std::string_view odir, std::string ) {
  //for header file
  auto supp_f = absl::StrCat(odir, "/", fname, ".", supp_ftype);
  int supp_fd = ::open(supp_f.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if(supp_fd<0) {
    Pass::error("inou.code_gen unable to create header file {}", supp_f);
    return;
  }
  size_t supp_sz = write(supp_fd, supp_file_final_str.c_str(), supp_file_final_str.size());
  if (supp_sz != supp_file_final_str.size()) {
    Pass::error("inou.code_gen header file unexpected write missmatch");
    return;
  }
  close(supp_fd);

  //for cpp file
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

void Ver_parser::result_in_odir(std::string_view fname, std::string_view odir, std::string buffer_to_print) {//TODO: currently as per prp. change as required.
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
