//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "pass.hpp"
#include "lnast.hpp"

class Inou_lnast_dfg_options {
public:
  std::string files;
  std::string path;
};

class Inou_lnast_dfg : public Pass {
private:
  Inou_lnast_dfg_options opack;
  std::string_view       memblock;
  Lnast_parser           lnast_parser;

protected:

public:
  Inou_lnast_dfg() : Pass("lnast_dfg") {};
  static void   tolg(Eprp_var &var);
  void          setup() final;

private:
  std::string_view       setup_memblock();
  std::vector<LGraph *>  do_tolg();

};


