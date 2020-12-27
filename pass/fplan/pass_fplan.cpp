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
    archfp_driver ad;
    if (var.get("hierarchical") == "true") {
      ad.load_hier_lg(root_lg, path);
    } else {
      ad.load_flat_lg(root_lg, path);
    }

    ad.create_floorplan("floorplan.txt");
  } else {
    fmt::print("algorithm not implemented!\n");
  }
}

void Pass_fplan::pass(Eprp_var& var) {
  auto whole_t = profile_time::timer();

  fmt::print("generating floorplan...\n");
  whole_t.start();

  Pass_fplan p(var);

  fmt::print("floorplans generated ({} ms).\n\n", whole_t.time());
}
