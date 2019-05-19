
#include "lnast.hpp"

//------------- Language_neutral_ast member function start -----
Language_neutral_ast::Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top) : buffer(_buffer) {
  I(!buffer.empty());
  set_root(Lnast_node(ntype_top,0));
}


//------------- Lnast_parser member function start -------------

void Lnast_parser::elaborate(){
  lnast = std::make_unique<Language_neutral_ast>(get_buffer(), Lnast_ntype_top);
  build_statement();
}


void Lnast_parser::build_statement(){

  fmt::print("statement:{}, line:{}\n", scan_text(), line_num);
  lnast->add_child(lnast->get_root(), Lnast_node(Lnast_ntype_statement, scan_token()));

  while(line_num == scan_calc_lineno()){
    if(scan_is_end())
      return;

    fmt::print("token:{}\n", scan_text());
    scan_next();
  }

  line_num += 1;
  build_statement();
}

