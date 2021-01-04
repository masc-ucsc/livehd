#include <string>

#include "pass_fplan.hpp"
#include "profile_time.hpp"

void setup_pass_fplan() { Pass_fplan_makefp::setup(); }

void Pass_fplan_makefp::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan_makefp::pass);

  m.add_label_optional("traversal", "LGraph traversal method to use, valid options are \"hier_lg\", \"flat_lg\", and \"flat_node\"", "flat_node");

  register_pass(m);
}

Pass_fplan_makefp::Pass_fplan_makefp(const Eprp_var& var) : Pass("pass.fplan", var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one root lgraph provided!");
  }

  root_lg = var.lgs[0];

  std::string_view t_str = var.get("traversal");

  auto t = profile_time::timer();
  if (t_str == "hier_lg") {
    lg_hier_floorp hfp;

    t.start();
    fmt::print("  traversing hierarchy...");
    hfp.load_lg(root_lg, path);
    fmt::print(" done ({} ms).\n", t.time());

    t.start();
    fmt::print("  creating floorplan...");
    hfp.create_floorplan("lg_hier_floorplan.flp");
    fmt::print(" done ({} ms).\n", t.time());
  } else if (t_str == "flat_lg") {
    lg_flat_floorp ffp;

    t.start();
    fmt::print("  traversing hierarchy...");
    ffp.load_lg(root_lg, path);
    fmt::print(" done ({} ms).\n", t.time());

    t.start();
    fmt::print("  creating floorplan...");
    ffp.create_floorplan("lg_flat_floorplan.flp");
    fmt::print(" done ({} ms).\n", t.time());
  } else if (t_str == "flat_node") {
    node_flat_floorp nfp;

    // ArchFP doesn't handle large numbers of nodes being attached to a single geogLayout instance very well
    fmt::print("WARNING: this kind of traversal only works for small numbers of nodes.\n");

    t.start();
    fmt::print("  traversing hierarchy...");
    nfp.load_lg(root_lg, path);
    fmt::print(" done ({} ms).\n", t.time());

    t.start();
    fmt::print("  creating floorplan...");
    nfp.create_floorplan("node_flat_floorplan.flp");
    fmt::print(" done ({} ms).\n", t.time());
  } else {
    std::string errstr = "unknown traversal method ";
    throw std::invalid_argument(errstr.append(t_str));
  }
}

void Pass_fplan_makefp::pass(Eprp_var& var) {
  auto whole_t = profile_time::timer();

  fmt::print("generating floorplan...\n");
  whole_t.start();

  Pass_fplan_makefp p(var);

  fmt::print("floorplan generated ({} ms).\n\n", whole_t.time());
}
