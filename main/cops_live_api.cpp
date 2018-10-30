
#include <unistd.h>

#include "eprp_utils.hpp"
#include "lgbench.hpp"

#include "cops_live_api.hpp"
#include "diff_finder.hpp"
#include "invariant_finder.hpp"
#include "invariant_options.hpp"
#include "live_options.hpp"
#include "structural.hpp"
#include "stitcher.hpp"

void Cops_live_api::invariant_finder(Eprp_var &var) {

  LGBench b;
  Invariant_find_options pack;
  for(const auto &l:var.dict) {
    pack.set(l.first,l.second);
  }

  Invariant_finder     worker(pack);
  b.sample("cops.live.inv_finder.setup");

  Invariant_boundaries fibs = worker.get_boundaries();
  b.sample("cops.live.inv_finder.work");

  std::ofstream of(pack.invariant_file);
  Invariant_boundaries::serialize(&fibs, of);
  of.close();
  b.sample("cops.live.inv_finder.output");
}

void Cops_live_api::diff_finder(Eprp_var &var) {
  LGBench b;

  Live_pass_options pack;
  for(const auto &l:var.dict) {
    pack.set(l.first,l.second);
  }

  Diff_finder      worker(pack);
  b.sample("cops.diff.setup");

  std::set<Net_ID> diffs;
  worker.generate_delta(pack.modified_lgdb, pack.delta_lgdb, diffs);

  b.sample("find_diffs");

  std::ofstream of(pack.diff_file);
  for(auto &diff_ : diffs) {
    of << diff_.first << "\t" << diff_.second << std::endl;
  }
  of.close();

  b.sample("write_diff");
}

void Cops_live_api::netlist_merge(Eprp_var &var) {
}

void Cops_live_api::setup(Eprp &eprp) {
  Eprp_method inv_find("live.invariant_find", "find invariant boundaries between post-synthesis and post-elaboration lgraphs", &Cops_live_api::invariant_finder);
  inv_find.add_label_required("top", "lgraph path");
  inv_find.add_label_required("elab_lgdb","lgdb path of the elaborated netlist");
  inv_find.add_label_required("synth_lgdb","lgdb path of the synthesized netlist");
  inv_find.add_label_required("invariant_file","file to serialize the invariant boundaries object (used by diff)");
  inv_find.add_label_required("hier_sep","hierarchical separator used in names by the synthesis tool");

  eprp.register_method(inv_find);

  Eprp_method diff_find("live.diff_finder", "find cones that changed between two post-elaboration lgraphs", &Cops_live_api::diff_finder);
  diff_find.add_label_required("top", "lgraph path");
  diff_find.add_label_required("elab_lgdb","lgdb path of the elaborated netlist");
  diff_find.add_label_required("synth_lgdb","lgdb path of the synthesized netlist");
  diff_find.add_label_required("invariant_file","file to serialize the invariant boundaries object (used by diff)");
  diff_find.add_label_required("hier_sep","hierarchical separator used in names by the synthesis tool");
  eprp.register_method(diff_find);

  Eprp_method m3("live.merge_changes", "merge synthesized delta into the original synthesized netlist", &Cops_live_api::netlist_merge);
  //m2.add_label_optional("odir","pyrope output directory");
  eprp.register_method(m3);
}

