
#pragma once

#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = "\n";

public:
  Prp_parser() {};
  std::string_view stmt_sep() final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = " ;\n";

public:
  Cpp_parser(){};
  std::string_view stmt_sep() final;

};

