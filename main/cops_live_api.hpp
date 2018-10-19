#include <unistd.h>

#include "eprp_utils.hpp"
#include "diff_finder.hpp"
#include "invariant_finder.hpp"
#include "structural.hpp"
#include "stitcher.hpp"

class Cops_live_api {
protected:
static void invariant_finder(Eprp_var &var) {

  /*
  const std::string path    = var.get("path","lgdb");
  const std::string files   = var.get("files");

  Inou_pyrope pyrope;

  pyrope.set("path",path);

  for(const auto &f:Eprp_utils::parse_files(files,"inou.pyrope.tolg")) {
    pyrope.set("input",f);

    var.add(pyrope.tolg());
  }*/
  fmt::print("invariant\n");
}

static void diff_finder(Eprp_var &var) {

  /*
  const std::string odir   = var.get("odir",".");

  Inou_pyrope pyrope;

  pyrope.set("odir",odir);

  std::vector<const LGraph *> lgs;
  for(const auto &l:var.lgs) {
    lgs.push_back(l);
  }

  pyrope.fromlg(lgs);
  */
  fmt::print("diff\n");
}

static void netlist_merge(Eprp_var &var) {
  /*
  const std::string odir   = var.get("odir",".");

  Inou_pyrope pyrope;

  pyrope.set("odir",odir);

  std::vector<const LGraph *> lgs;
  for(const auto &l:var.lgs) {
    lgs.push_back(l);
  }

  pyrope.fromlg(lgs);
  */
  fmt::print("merge\n");
}

public:
static void setup(Eprp &eprp) {
  Eprp_method m1("live.invariant_find", "find invariant boundaries between post-synthesis and post-elaboration lgraphs", &Cops_live_api::invariant_finder);
  //m1.add_label_optional("path","lgraph path");
  //m1.add_label_required("files","pyrope input file[s]");

  eprp.register_method(m1);

  Eprp_method m2("live.diff_finder", "find cones that changed between two post-elaboration lgraphs", &Cops_live_api::diff_finder);
  //m2.add_label_optional("odir","pyrope output directory");
  eprp.register_method(m2);

  Eprp_method m3("live.merge_changes", "merge synthesized delta into the original synthesized netlist", &Cops_live_api::netlist_merge);
  //m2.add_label_optional("odir","pyrope output directory");
  eprp.register_method(m3);
}

};
