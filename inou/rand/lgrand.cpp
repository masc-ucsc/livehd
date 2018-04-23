
#include <iostream>

#include "lgraph.hpp"
#include "lgbench.hpp"

#include "inou/yaml/inou_yaml.hpp"
#include "inou/json/inou_json.hpp"
#include "inou/rand/inou_rand.hpp"

int main(int argc, const char **argv) {

  //auto console = spdlog::stdout_color_mt("console");
  LGBench b;

  Options::setup(argc, argv);

  Inou_yaml yaml;
  Inou_json json;
  Inou_rand rand;

  Options::setup_lock();
  b.sample("setup");

  auto vgen = rand.generate();

  assert(vgen.size() == 1);

  for(auto &g:vgen) {
    g->print_stats();
    yaml.generate(g);
    json.generate(g);
  }
}

