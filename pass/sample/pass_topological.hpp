//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_TOPO_H
#define PASS_TOPO_H

#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

//sample pass that counts number of nodes, but traverses the graph in
//topological order from inputs to output
class Pass_topo : public Pass {
public:
  Pass_topo() : Pass("topological") { }

  void transform(LGraph *orig) final;
};

#endif
