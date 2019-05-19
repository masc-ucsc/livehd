
#include "lnast.hpp"


//------------- Lnast_parser member function start -------------

void Lnast_parser::elaborate(){
  while(!scan_is_end()){
    fmt::print("token:{}\n", scan_text());
    scan_next();
  }
  return;
}
