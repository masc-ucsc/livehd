
#pragma once

#include "code_gen.hpp"
#include "code_gen_all_lang.hpp"

class Prp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = "\n";
  std::string_view lang_type = "prp";
public:
  Prp_parser() {};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
};

//-------------------------------------------------------------------------------------

class Cpp_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = " ;\n";
  std::string_view lang_type = "cpp";

public:
  Cpp_parser(){};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
};

//-------------------------------------------------------------------------------------

class Ver_parser: public Code_gen_all_lang {
  std::string_view stmt_separator = ";\n";
  std::string_view lang_type = "v";

public:
  Ver_parser(){};
  std::string_view stmt_sep() final;
  std::string_view get_lang_type() final;
  std::string_view debug_name_lang(Lnast_ntype node_type) final;
};

