//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <random>

#include "lgbench.hpp"
#include "lgraph.hpp"

#include "inou.hpp"

class Pass_options_pack : public Options_pack {
  std::set<std::string> enabled;

public:
  std::vector<std::string> passes;

  Pass_options_pack();
};

Pass_options_pack::Pass_options_pack() {
  assert(Options::get_cargc() != 0); // Options::setup(argc,argv) must be called before setup() is called
}

// This pass performs detailed routing. It reads a lgraph and generates a
// lpgrah with detailed routing information

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Pass_options_pack opack;

#if 0
  Inou_trivial inou;
  Options::setup_lock();

  std::vector<LGraph *> lgs = inou.generate();
  b.sample("setup");
#endif

  fmt::print("Hello world\n");
}
