
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
  return ("end else begin");
}


std::string_view Ver_parser::end_cond(){
  return (") begin");
}

std::string_view Ver_parser::end_if_or_else(){
  return ("end\n");
}

