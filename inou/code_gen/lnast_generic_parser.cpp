
#include "lnast_generic_parser.hpp"

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



