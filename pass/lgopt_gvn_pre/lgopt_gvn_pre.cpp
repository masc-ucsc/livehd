
#include <iostream>
#include <random>

#include "lgraph.hpp"
#include "lgbench.hpp"

#include "inou.hpp"

#include "pass/gvn_pre/pass_gvn_pre.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Pass_gvn_pre gvn_pre;
  Inou_trivial inou;

  Options::setup_lock();

  std::vector<LGraph *> lgs = inou.generate();
  b.sample("setup");

  for(auto g:lgs) {
    console->info("processing {}\n",g->get_name());
    gvn_pre.transform(g);
  }

  b.sample("gvn_pre_pass");

  for(auto g:lgs) {
    g->sync();
  }
}

