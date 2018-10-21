//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_TOPO_H
#define PASS_TOPO_H

#include "pass.hpp"
#include "options.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include <string>
//sample pass that counts number of nodes, but traverses the graph in
//topological order from inputs to output
class Pass_topological_options_pack : public Options_base {
public:
  void set(const std::string &label, const std::string &value) {
    //nothing to do?
  }
};

class Pass_topo : public Pass {
protected:
  Pass_topological_options_pack opack;
public:
  Pass_topo();

  void trans(LGraph *orig) final;

  LGraph *regen(const LGraph *orig) {
    assert(false);
  }

  //no options needed
  void set(const std::string &key, const std::string &value) { }
};

#endif
