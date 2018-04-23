
// YAML to YAML graph conversion


#include <iostream>
#include <random>

#include "lgraph.hpp"
#include "lgbench.hpp"

#include "inou/yaml/inou_yaml.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);

  Inou_yaml yaml;

  Options::setup_lock();
  b.sample("setup");


  std::vector<LGraph *> rvec = yaml.generate();

  for(auto &g:rvec) {
    g->print_stats();
    yaml.generate(g);
  }

  b.sample("creation");
  b.sample("to_yaml");
}

