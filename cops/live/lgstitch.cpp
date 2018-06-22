//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <iostream>

#include "inou.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"

#include "invariant.hpp"
#include "stitch_options.hpp"

#include "stitcher.hpp"
#include "structural.hpp"

int main(int argc, const char **argv) {
  LGBench b;

  Stitch_pass_pack pack(argc, argv);
  std::ifstream    invariant_file(pack.boundaries_name);
  if(!invariant_file.good()) {
    console->error("Error reading boundaries file {}\n", pack.boundaries_name);
    exit(1);
  }

  b.sample("setup");

  Invariant_boundaries *boundaries = Invariant_boundaries::deserialize(invariant_file);
  invariant_file.close();

  b.sample("read_invariant");

  std::ifstream    ifs(pack.diff_file);
  std::set<Net_ID> diffs;
  WireName_ID      net;
  uint32_t         bit;

  while(ifs >> net >> bit) {
    diffs.insert(std::make_pair(net, bit));
  }
  ifs.close();

  b.sample("read_diff");

  std::string top            = boundaries->top.substr(7);
  LGraph *    original_synth = LGraph::open_lgraph(pack.osynth_lgdb, top);
  LGraph *    newly_synth    = LGraph::open_lgraph(pack.nsynth_lgdb, "lgraph_" + top);

  if(!original_synth) {
    console->error("I was not able to open original synthesized netlist {} in {}\n", boundaries->top, pack.osynth_lgdb);
    exit(1);
  }
  if(!newly_synth) {
    console->error("I was not able to open newly synthesized netlist {} in {}\n", boundaries->top, pack.nsynth_lgdb);
    exit(1);
  }
  b.sample("load_graphs");

  if(pack.method == Stitch_pass_pack::Live_method::LiveSynth) {
    Live_stitcher worker(original_synth, boundaries);
    worker.stitch(newly_synth, diffs);

  } else if(pack.method == Stitch_pass_pack::Live_method::Structural) {
    Live_structural worker(original_synth, boundaries);
    //worker.replace(newly_synth, diffs);
    worker.replace(newly_synth);

  } else {
    console->error("Unsupported stitch method {}\n", pack.method);
    exit(1);
  }

  b.sample("stitch");

  return 0;
}
