
#include <iostream>
#include <fstream>
#include <string>

#include "lgraph.hpp"
#include "lgbench.hpp"
#include "inou.hpp"

#include "live_options.hpp"
#include "invariant.hpp"

#include "diff_finder.hpp"


int main(int argc, const char **argv) {
  LGBench b;

  Live_pass_pack pack(argc, argv);
  ifstream invariant_file(pack.boundaries_name);
  if(!invariant_file.good()) {
    console->error("Error reading boundaries file {}\n",pack.boundaries_name);
    exit(1);
  }

  b.sample("setup");

  Invariant_boundaries* boundaries;
  try {
   boundaries = Invariant_boundaries::deserialize(invariant_file);
  } catch (std::exception e) {
    console->warn("There was an error de-serializing the boundaries file\n");
  }
  invariant_file.close();

  b.sample("read_invariant");

  std::string top = boundaries->top;
  if(top.substr(0,7) == "lgraph_")
    top = top.substr(7);

  LGraph* original  = LGraph::open_lgraph(pack.original_lgdb, top);
  LGraph* synth  = LGraph::open_lgraph(pack.synth_lgdb, top);

  console->info("Starting Diff pass for: original = {}, synth = {}\n",pack.original_lgdb, pack.synth_lgdb);

  if(!original) {
    console->error("I was not able to open original netlist {} in {}\n",top, pack.original_lgdb);
    exit(1);
  }
  if(!synth) {
    console->error("I was not able to open synthesized netlist {} in {}\n",top, pack.synth_lgdb);
    exit(1);
  }
  b.sample("load_graphs");

  std::set<Net_ID> diffs;
  Diff_finder worker(original, synth, boundaries);
  worker.generate_delta(pack.modified_lgdb, pack.delta_lgdb, diffs);

  b.sample("find_diffs");

  std::ofstream of(pack.diff_file);
  for(auto & diff_ : diffs) {
    of << diff_.first << "\t" << diff_.second << std::endl;
  }
  of.close();

  b.sample("write_diff");

  return 0;
}
