#include "pass_fplan.hpp"

#include <string>

#include "profile_time.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);

  m.add_label_optional("algorithm", "algorithm used to generate the floorplan", "archfp");

  m.add_label_optional("hierarchical", "if set to true, the floorplan will consider the LGraph hierarchy", "false");

  register_pass(m);
}

Pass_fplan::Pass_fplan(const Eprp_var& var) : Pass("pass.fplan", var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one lgraph provided!");
  }

  root_lg = var.lgs[0];

  if (var.get("algorithm") == "archfp") {
    auto t = profile_time::timer();
    if (var.get("hierarchical") == "true") {
      lg_hier_floorp hfp;
      
      t.start();
      fmt::print("  traversing hierarchy...");
      hfp.load_lg(root_lg, path);
      fmt::print(" done ({} ms).\n", t.time());

      t.start();
      fmt::print("  creating floorplan...");
      hfp.create_floorplan("hier_floorplan.txt");
      fmt::print(" done ({} ms).\n", t.time());
    } else {
      lg_flat_floorp ffp;

      t.start();
      fmt::print("  traversing hierarchy...");
      ffp.load_lg(root_lg, path);
      fmt::print(" done ({} ms).\n", t.time());

      t.start();
      fmt::print("  creating floorplan...");
      ffp.create_floorplan("flat_floorplan.txt");
      fmt::print(" done ({} ms).\n", t.time());
    }
  } else {
    fmt::print("algorithm not implemented!\n");
  }
}

void Pass_fplan::analyze_floorplan(const std::string_view filename) {
  // things to record / dump:
  // 1. number of nodes
  // 2. HPWL
  // 3. ??
}

void Pass_fplan::pass(Eprp_var& var) {
  auto whole_t = profile_time::timer();

  fmt::print("generating floorplan...\n");
  whole_t.start();

  Pass_fplan p(var);

  fmt::print("floorplans generated ({} ms).\n\n", whole_t.time());
}
