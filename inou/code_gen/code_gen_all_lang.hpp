
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
  std::string_view tuple_begin() {return "(";}
  std::string_view tuple_end() {return ")";}

  //TODO: func def related parameters: need to make language specific! currently as per pyrope:
  std::string_view func_begin() {return "";}
  std::string_view func_name(std::string_view func_name) {return func_name;}
  std::string_view param_start() {return " = :(";}
  std::string_view func_param_sep() {return ", ";}
  std::string_view param_end() {return ")";}
  std::string print_cond(std::string cond_val) {
    if(cond_val != "")
      return (absl::StrCat(" when ", cond_val));
    else
      return cond_val;
  }
  std::string_view func_stmt_strt() {return ":{\n";}
  std::string_view func_stmt_end() {return "}\n";}
  std::string_view func_end() {return "";}

  //TODO: for related parameters: need to make language specific! currently as per pyrope:
  std::string_view for_cond_beg() {return " ";}
  std::string_view for_cond_end() {return " ";}
  std::string_view for_stmt_beg() {return "{\n";}
  std::string_view for_stmt_end() {return "}\n";}

  //TODO: while related parameters: need to make language specific! currently as per pyrope:
  std::string_view while_cond_beg() {return "(";}
  std::string_view while_cond_end() {return ") ";}

  //TODO: select related parameters: need to make language specific! currently as per pyrope:
  std::string_view select_init(std::string select_type) {
    if (select_type=="bit")
      return "[[";
    else
      return "[";
  }
  std::string_view select_end(std::string select_type) {
    if (select_type=="bit")
      return "]]";
else
      return "]";
  }
};

