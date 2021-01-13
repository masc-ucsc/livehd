#include <string>

#include "pass_fplan.hpp"
#include "profile_time.hpp"

void setup_pass_fplan() {
  Pass_fplan_makefp::setup();
  Pass_fplan_writearea::setup();
}

void Pass_fplan_makefp::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan_makefp::pass);

  m.add_label_optional(
      "traversal",
      "LGraph traversal method to use, valid options are \"hier_lg\", \"flat_lg\", \"flat_node\", and \"hier_node\"",
      "hier_node");

  m.add_label_optional("write_file",
                       "If true, output will be sent to a floorplan file instead of being written back into LiveHD",
                       "false");

  register_pass(m);
}

void Pass_fplan_makefp::makefp_int(Lhd_floorplanner& fp, bool write_file) {
  auto t = profile_time::Timer();
  t.start();
  fmt::print("  traversing hierarchy...");
  fp.load(root_lg, path);
  fmt::print(" done ({} ms).\n", t.time());

  t.start();
  fmt::print("  creating floorplan...");
  fp.create();
  fmt::print(" done ({} ms).\n", t.time());

  t.start();
  fmt::print("  writing floorplan...");
  if (write_file) {
    fp.write_file("floorplan.flp");
  } else {
    fp.write_lhd();
  }
  fmt::print(" done ({} ms).\n", t.time());
}

Pass_fplan_makefp::Pass_fplan_makefp(const Eprp_var& var) : Pass("pass.fplan", var) {
  root_lg = var.lgs[0];

  std::string_view t_str      = var.get("traversal");
  bool             write_file = var.get("write_file").data() == std::string_view("true");

  if (t_str == "hier_lg") {
    Lg_hier_floorp hfp;
    makefp_int(hfp, write_file);
  } else if (t_str == "flat_lg") {
    Lg_flat_floorp ffp;
    makefp_int(ffp, write_file);
  } else if (t_str == "flat_node") {
    // ArchFP doesn't handle large numbers of nodes being attached to a single geogLayout instance very well
    fmt::print("WARNING: this kind of traversal only works for small numbers of nodes.\n");

    Node_flat_floorp nffp;
    makefp_int(nffp, write_file);
  } else if (t_str == "hier_node") {
    Node_hier_floorp nhfp;
    makefp_int(nhfp, write_file);
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

  fmt::print("floorplan generated ({} ms).\n\n", whole_t.time());
}
