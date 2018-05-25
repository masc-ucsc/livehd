//
// Created by birdeclipse on 12/18/17.
//

#include "core/lgbench.hpp"
#include "core/lgedgeiter.hpp"
#include "core/lgraph.hpp"

#include "inou/json/inou_json.hpp"

int main(int argc, const char **argv) {

  LGBench b;
  Options::setup(argc, argv);

  Inou_json json;

  Options::setup_lock();
  std::vector<LGraph *> rvec = json.generate();

  for(auto &g : rvec) {
    json.generate(g);
  }
}