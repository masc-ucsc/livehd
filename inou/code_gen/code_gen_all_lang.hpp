
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
};

