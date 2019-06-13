//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "pass.hpp"

class Inou_lnast_dfg_options {
public:
  std::string file;
  std::string path;
};

class Inou_lnast_dfg : public Pass {
private:
  std::vector<LGraph *>  do_tolg();
  Inou_lnast_dfg_options opack;
protected:

public:
  Inou_lnast_dfg() : Pass("lnast_dfg") {};
  static void   tolg(Eprp_var &var);
  void          setup() final;
};


