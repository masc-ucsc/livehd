//
// Created by birdeclipse on 3/22/18.
//
#include "inou_lef.hpp"

int main(int argc, const char **argv) {
  Options::setup(argc, argv);
  Inou_lef lef;
  Options::setup_lock();
  std::vector<LGraph *> rvec = lef.generate();
  for(auto &g : rvec) {
    lef.generate(g);
  }
}