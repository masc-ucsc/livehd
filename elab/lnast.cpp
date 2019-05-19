
#include "lnast.hpp"

//------------- Language_neutral_ast member function start -----
Language_neutral_ast::Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top) : buffer(_buffer) {
  I(!buffer.empty());

  set_root(Lnast_node(ntype_top,0));
}


//------------- Lnast_parser member function start -------------
void Lnast_parser::elaborate(){
  while(!scan_is_end()){
    fmt::print("token:{}\n", scan_text());
    scan_next();
  }
}
