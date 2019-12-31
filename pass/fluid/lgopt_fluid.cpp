//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <random>

#include "inou.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"
#include "pass/fluid/pass_fluid.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Pass_fluid   fluid;
  Inou_trivial inou;

  Options::setup_lock();

  std::vector<LGraph *> lgs = inou.generate();
  b.sample("setup");

  for (auto g : lgs) {
    fluid.transform(g);
  }

  b.sample("fluid_pass");

  for (auto g : lgs) {
    g->sync();
  }
}
