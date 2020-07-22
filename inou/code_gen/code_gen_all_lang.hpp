
#pragma once

#include "code_gen.hpp"

class Code_gen_all_lang {
//protected:
//  virtual std::string_view stmt_sep() = 0;
public:
  Code_gen_all_lang() {fmt::print("\n------HELLO-----\n"); };
  virtual std::string_view stmt_sep() = 0;
};

