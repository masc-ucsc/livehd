#ifndef PASS_FAST_H
#define PASS_FAST_H

#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

//sample pass that counts number of nodes
class Pass_fast : public Pass {
public:
  Pass_fast() : Pass("fast") { }

  void transform(LGraph *orig) final {
    int cells = 0;
    for(const auto& idx : orig->fast()) {
      (void)idx;
      cells++;
    }
    fmt::print("cells {}\n",cells);
  }
};

#endif
