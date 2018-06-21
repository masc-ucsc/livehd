//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include <iostream>
#include <random>

#include "lgbench.hpp"
#include "lgraph.hpp"

#include "inou_pyrope.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Inou_pyrope pyrope;

  Options::setup_lock();
  b.sample("setup");

  std::vector<LGraph *> rvec = pyrope.generate();

  for(auto &g : rvec) {
    g->print_stats();
    pyrope.generate(g);
  }

  b.sample("creation");
  b.sample("to_pyrope");
}
