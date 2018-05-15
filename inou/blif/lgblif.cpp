//
// Created by birdeclipse on 1/24/18.
//
#include "lgraph.hpp"

#include "inou/blif/inou_blif.hpp"

int main(int argc, const char **argv) {

  Options::setup(argc, argv);

  Inou_blif blif;

  Options::setup_lock();

  std::vector<LGraph *> rvec = blif.generate();
  for(auto &g : rvec) {
    blif.generate(g);
  }
}