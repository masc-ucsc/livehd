
#include <fstream>
#include <iostream>
#include <random>

#include "inou.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"

#include "invariant_finder.hpp"
#include "invariant_options.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Options::setup(argc, argv);
  Invariant_find_pack pack;

  Options::setup_lock();

  fmt::print("running lginvariant with separator {}\n", pack.hierarchical_separator);

  b.sample("setup");
  LGraph *    elab           = LGraph::open_lgraph(pack.elab_lgdb, pack.top);
  LGraph *    synth          = LGraph::open_lgraph(pack.synth_lgdb, pack.top);
  std::string invariant_file = pack.invariant_file;

  if(!elab) {
    console->error("I was not able to open elab netlist {} in {}\n", pack.top, pack.elab_lgdb);
    exit(1);
  }
  if(!synth) {
    console->error("I was not able to open synth netlist {} in {}\n", pack.top, pack.synth_lgdb);
    exit(1);
  }
  b.sample("read_graphs");

  Invariant_finder     worker(elab, synth, pack.hierarchical_separator);
  Invariant_boundaries fibs = worker.get_boundaries();

  b.sample("found_invariants");

  std::ofstream of(invariant_file);
  Invariant_boundaries::serialize(&fibs, of);
  of.close();

  b.sample("wrote_output");

  return 0;
}
