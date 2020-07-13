
#pragma once

#include "code_gen.hpp"

class Prp_parser: public Code_gen {
  std::string_view stmt_separator = "\n";

public:
  Prp_parser(std::shared_ptr<Lnast> _lnast, std::string_view _path) : Code_gen(std::move(_lnast), _path){};
  std::string_view stmt_sep();
};

class Cpp_parser: public Code_gen {
  std::string_view stmt_separator = " ;\n";

public:
  Cpp_parser(std::shared_ptr<Lnast> _lnast, std::string_view _path) : Code_gen(std::move(_lnast), _path){};
  std::string_view stmt_sep();

};
