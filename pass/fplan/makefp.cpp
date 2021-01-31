//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "makefp.hpp"

#include <string>

#include "iassert.hpp"
#include "lg_hier_floorp.hpp"
#include "node_flat_floorp.hpp"
#include "node_hier_floorp.hpp"
#include "node_tree.hpp"
#include "node_type_area.hpp"
#include "profile_time.hpp"

void Pass_fplan_makefp::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan_makefp::pass);

  m.add_label_optional("traversal",
                       "LGraph traversal method to use. Valid options are \"hier_lg\", \"flat_node\", and \"hier_node\"",
                       "hier_node");

  m.add_label_optional("dest", "Where to send the floorplan file.  Valid options are \"file\" and \"livehd\".", "file");

  register_pass(m);
}

void Pass_fplan_makefp::makefp_int(Lhd_floorplanner& fp, const std::string_view dest) {
  auto t = profile_time::Timer();

  t.start();
  fmt::print("  creating node hierarchy...");
  auto nt = Node_tree(root_lg);
  fmt::print(" done ({} ms).\n", t.time());

  t.start();
  fmt::print("  traversing node hierarchy...");
  fp.load(nt, path);
  fmt::print(" done ({} ms).\n", t.time());

  t.start();
  fmt::print("  creating floorplan...");
  fp.create();
  fmt::print(" done ({} ms).\n", t.time());

  t.start();
  fmt::print("  writing floorplan...");
  if (dest == "file") {
    fp.write_file("floorplan.flp");
  } else if (dest == "livehd") {
    fp.write_lhd(nt);
  } else {
    throw std::invalid_argument("unknown destination!");
  }
  fmt::print(" done ({} ms).\n", t.time());
}

Pass_fplan_makefp::Pass_fplan_makefp(const Eprp_var& var) : Pass("pass.fplan", var), root_lg(nullptr) {
  root_lg = var.lgs[0];  // length checked by pass() before being passed to Pass_fplan_makefp

  std::string_view t_str = var.get("traversal");

  


  geogLayout* L2Stack2 = new geogLayout();
  L2Stack2->addComponentCluster("l1", 1, 3.166, 3., 1., Top);
  L2Stack2->addComponentCluster("l2", 1, 3.166, 3., 1., Top);

  geogLayout* L2Stack = new geogLayout();
  L2Stack->addComponent(L2Stack2, 2, Top);
  L2Stack->addComponentCluster("l3", 1, 3.166, 3., 1., Top);
  L2Stack->addComponentCluster("l4", 1, 3.166, 3., 1., Top);

  geogLayout* chip = new geogLayout();
  chip->addComponent(L2Stack, 2, Top);
  //chip->addComponent(L2Stack, 1, Top); // this isn't picked up for some reason
  //chip->addComponent(L2Stack, 1, Top);
  //chip->addComponent(L2Stack2, 1, Top); // this is illegal, and hard to detect - need to floorplan a deep copy.
  chip->addComponentCluster("l5", 2, 9.5, 2., 1., Top);

  bool success = chip->layout(AspectRatio, 1.0);
  if (!success) {
    cerr << "Unable to layout specified CMP configuration." << endl;
  }

  ostream& HSOut = outputHotSpotHeader("TRIPS.flp");
  chip->outputHotSpotLayout(HSOut);
  outputHotSpotFooter(HSOut);




  return;

  if (t_str == "hier_lg") {
    // Lg_hier_floorp hfp;
    // makefp_int(hfp, "file");
  } else if (t_str == "flat_node") {
    // Node_flat_floorp nffp;
    // makefp_int(nffp, "file");
  } else if (t_str == "hier_node") {
    Node_hier_floorp nhfp;
    makefp_int(nhfp, var.get("dest"));
  } else {
    std::string errstr = "unknown traversal method ";
    throw std::invalid_argument(errstr.append(t_str));
  }
}

void Pass_fplan_makefp::pass(Eprp_var& var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one root lgraph provided!");
  }

  auto whole_t = profile_time::Timer();

  fmt::print("generating floorplan...\n");
  whole_t.start();

  Pass_fplan_makefp p(var);

  fmt::print("done ({} ms).\n\n", whole_t.time());
}
