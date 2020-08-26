
#include "lnast_generic_parser.hpp"
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
  return absl::StrCat("file: ", basename_s, "\n#pragma once");
}
std::string Cpp_parser::supporting_fend(std::string basename_s){
  return absl::StrCat("<<EOF ", basename_s);
}

std::string Cpp_parser::main_fstart(std::string basename, std::string basename_s) {
  return absl::StrCat("file: ", basename, "\n#include \"livesim_types.hpp\"\n#include \"", basename_s, "\"\n");
}

bool Cpp_parser::convert_parameters(std::string key, std::string ref) {
  //convert parameters to cpp format 
  //currently supports .__bits only
  assert(key.size()>=1);
  if(key.find("$")==0) {//it is i/p
    std::vector<std::string> _key = absl::StrSplit(key, absl::ByAnyChar("$."));
    inp_bw.insert(std::pair<std::string, std::string>(_key[1], ref));
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
std::string Cpp_parser::outline_cpp(std::string modname) {
  //constructor
  std::vector<std::string> name_split = absl::StrSplit(modname, "_");
  std::string constructor_vcd = modname + "::" + modname + "(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)\n\t: hidx(_hidx)\n\t, scope_name(parent_name+\"." + name_split[1] + "\")\n\t, vcd_writer(writer) {\n}\n";
  std::string constructor = modname + "::" + modname + "(uint64_t _hidx)\n\t: hidx(_hidx) {\n}\n";

  //reset function
  //main code part function
  return absl::StrCat(constructor_vcd, "\n", constructor);
}
