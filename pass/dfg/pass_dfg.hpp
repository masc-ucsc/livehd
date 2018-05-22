#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_dfg_options_pack : public Options_pack {
public:
};

class Pass_dfg : public Pass {
  public:
    Pass_dfg() : Pass("dfg") { }

};

#endif
