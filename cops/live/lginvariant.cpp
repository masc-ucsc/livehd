
#include <iostream>
#include <random>
#include <fstream>

#include "lgraph.hpp"
#include "lgbench.hpp"
#include "inou.hpp"

#include "invariant_options.hpp"
#include "invariant_finder.hpp"


int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);
  Invariant_find_pack pack;

  Options::setup_lock();

  b.sample("setup");
  LGraph* elab  = LGraph::open_lgraph(pack.elab_lgdb, pack.top);
  LGraph* synth = LGraph::open_lgraph(pack.synth_lgdb, pack.top);
  std::string invariant_file = pack.invariant_file;

  if(!elab) {
    console->error("I was not able to open elab netlist {} in {}\n",pack.top.c_str(), pack.elab_lgdb.c_str());
    exit(1);
  }
  if(!synth) {
    console->error("I was not able to open synth netlist {} in {}\n",pack.top.c_str(), pack.synth_lgdb.c_str());
    exit(1);
  }
  b.sample("read_graphs");

  Invariant_finder worker(elab, synth);
  Invariant_boundaries fibs = worker.get_boundaries();

  b.sample("found_invariants");

  std::ofstream of(invariant_file);
  Invariant_boundaries::serialize(&fibs, of);
  of.close();

  b.sample("wrote_output");

  return 0;
}
