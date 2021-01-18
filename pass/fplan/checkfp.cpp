#include "pass_fplan.hpp"
#include "ann_place.hpp"

void Pass_fplan_checkfp::setup() {
  auto c = Eprp_method("pass.fplan.checkfp", "checks floorplan information stored in LiveHD hierarchy", &Pass_fplan_checkfp::pass);

  register_pass(c);
}

// TODO: once I get more confident that the hierarchy is built correctly, optimize this using hierarchy tree
Node Pass_fplan_checkfp::check_bb(const Node& n) {
  Node int_n;
  root_lg->each_hier_fast_direct([&n, &int_n](const Node& tn) -> bool {

    const Ann_place& p = n.get_place();
    const Ann_place& tp = tn.get_place();

    bool int_x = p.get_pos_x() >= tp.get_pos_x() && p.get_pos_x() < tp.get_pos_x() + tp.get_len_x();
    bool int_y = p.get_pos_y() >= tp.get_pos_y() && p.get_pos_y() < tp.get_pos_y() + tp.get_len_y();

    if (int_x && int_y) {
      int_n = tn;
      return false;
    }

    return true;
  });

  return int_n;
}

Pass_fplan_checkfp::Pass_fplan_checkfp(const Eprp_var& var) : Pass("pass.fplan", var) {
  var.lgs[0]->each_hier_fast_direct([this](const Node& n) -> bool {
    check_bb(n);
    return true;
  });
}

void Pass_fplan_checkfp::pass(Eprp_var& var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one root lgraph provided!");
  }

  fmt::print("checking floorplan...\n");

  Pass_fplan_checkfp c(var);

  fmt::print("floorplan generated.\n\n");
}