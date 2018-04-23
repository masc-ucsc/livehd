

#include <iostream>
#include <random>

#include "lgraph.hpp"
#include "lgbench.hpp"

#include "inou/pyrope/inou_pyrope.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Inou_pyrope pyrope;

  Options::setup_lock();
  b.sample("setup");

  std::vector<LGraph *> rvec = pyrope.generate();

  for(auto &g:rvec) {
    g->print_stats();
    pyrope.generate(g);
  }

  b.sample("creation");
  b.sample("to_pyrope");
}

