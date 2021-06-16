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
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an Lgraph", &Pass_fplan_makefp::pass);

  m.add_label_optional("traversal",
                       "Lgraph traversal method to use. Valid options are \"hier_lg\", \"flat_node\", and \"hier_node\"",
                       "hier_node");

  m.add_label_optional("strategy",
                       "What to optimize for.  Valid options are \"area\", \"wirelength\", and \"area_wirelength\"",
                       "area_wirelength");

  m.add_label_optional("filename", "If set, write the floorplan to a file named <filename>.flp as well as back into LiveHD.");
  m.add_label_optional("aspect", "Requested aspect ratio of the entire floorplan, default is 1.0.", "1.0");

  register_pass(m);
}

void Pass_fplan_makefp::makefp_int(Lhd_floorplanner& fp, const std::string_view dest, const std::string_view opt, const float ar) {
  auto t = profile_time::Timer();

  t.start();
  fmt::print("  traversing node hierarchy...");
  fp.load();
  fmt::print(" done ({} ms).\n", t.time());

  bool found = false;
  t.start();
  fmt::print("  creating floorplan...");

  constexpr size_t           opt_map_size   = (size_t)IllegalOpt;
  constexpr std::string_view opt_name_map[] = {"area", "wirelength", "area_wirelength", "aspect_ratio"};

  for (size_t i = 0; i < opt_map_size; i++) {
    if (opt_name_map[i] == opt) {
      fp.create((FPOptimization)i, ar);
      found = true;
      break;
    }
  }

  if (!found) {
    error("unknown strategy!");
  }

  fmt::print(" done ({} ms).\n", t.time());

  if (dest.length() > 0) {
    fmt::print("  writing floorplan to file {}...", dest);
    fp.write_file(dest);
    fmt::print(" done ({} ms).\n", t.time());
  }
}

Pass_fplan_makefp::Pass_fplan_makefp(const Eprp_var& var) : Pass("pass.fplan", var), root_lg(nullptr) {
  root_lg = var.lgs[0];  // length checked by pass() before being passed to Pass_fplan_makefp







  annLayout* chip = new annLayout(5);
  chip->addComponentCluster("Cnorm", 1, 3, 4., 1.);
  chip->addComponentCluster("Csmallar", 1, 4, 2., 1.);
  chip->addComponentCluster("Cbigar", 1, 2, 32., 1.);
  chip->addComponentCluster("Cgrid", 8, 1, 2., 1.);
  chip->addComponentCluster("Csmallgrid", 2, 2, 4., 1.);

  bool success = chip->layout(AspectRatio, 1.0);
  I(success);

  return;










  std::string_view t_str = var.get("traversal");

  auto whole_t = profile_time::Timer();

  fmt::print("generating floorplan...\n");

  fmt::print("  creating node hierarchy...");
  whole_t.start();
  auto nt = Node_tree(root_lg);
  fmt::print(" done ({} ms).\n", whole_t.time());

  float ar = std::stof(var.get("aspect").data());

  if (t_str == "hier_lg") {
    Lg_hier_floorp hfp(std::move(nt));
    makefp_int(hfp, var.get("filename"), var.get("strategy"), ar);

    profile_time::Timer t;
    t.start();
    fmt::print("  writing floorplan to livehd...");
    hfp.write_lhd_lg();
    fmt::print(" done ({} ms).\n", t.time());
  } else if (t_str == "flat_node") {
    Node_flat_floorp nffp(std::move(nt));
    makefp_int(nffp, var.get("filename"), var.get("strategy"), ar);

    // flat floorplans have no hierarchy and cannot be written to LiveHD
  } else if (t_str == "hier_node") {
    Node_hier_floorp nhfp(std::move(nt));
    makefp_int(nhfp, var.get("filename"), var.get("strategy"), ar);

    profile_time::Timer t;
    t.start();
    fmt::print("  writing floorplan to livehd...");
    nhfp.write_lhd_node();
    fmt::print(" done ({} ms).\n", t.time());
  } else {
    std::string errstr = "unknown traversal method ";
    error(errstr.append(t_str));
  }

  fmt::print("done ({} ms).\n\n", whole_t.time());
}

void Pass_fplan_makefp::pass(Eprp_var& var) {
  if (var.lgs.size() == 0) {
    error("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    error("more than one root lgraph provided!");
  }

  Pass_fplan_makefp p(var);
}
