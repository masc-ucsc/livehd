
#pragma once
#include "lnast.hpp"
#include "lnast_parser.hpp"

class Lnast_to_cfg_parser {
public:
  Lnast_to_cfg_parser(std::string_view memblock, Language_neutral_ast<Lnast_node> *lnast) 
    :k_num(0), memblock(memblock), lnast(lnast) {};
  void start_sub_sts();
  void stringify();

private:
  uint32_t k_num;
  std::string_view memblock;
  Language_neutral_ast<Lnast_node> *lnast;
  Lnast_parser lnast_parser;
};

