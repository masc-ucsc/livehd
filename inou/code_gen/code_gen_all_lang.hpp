
#pragma once

#include "code_gen.hpp"

class Code_gen_all_lang {
//protected:
//  virtual std::string_view stmt_sep() = 0;
public:
  Code_gen_all_lang() {};

  virtual std::string_view stmt_sep() = 0;
  virtual std::string_view get_lang_type() = 0;
  virtual std::string_view debug_name_lang(Lnast_ntype node_type) = 0;
  virtual std::string_view start_else_if() = 0;

  virtual std::string_view end_else_if() { return ("}"); }
  virtual std::string_view start_else() { return ("} else {\n"); }
  virtual std::string_view end_cond() {return ") {\n";}
  virtual std::string_view end_if_or_else() { return "}\n";}

  std::string_view start_cond() {return "if (";}
  std::string_view tuple_stmt_sep() {return ", ";}

};

