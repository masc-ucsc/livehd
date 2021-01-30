//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "checkfp.hpp"

#include <vector>

#include "ann_place.hpp"

void Pass_fplan_checkfp::setup() {
  auto c = Eprp_method("pass.fplan.checkfp", "checks floorplan information stored in LiveHD hierarchy", &Pass_fplan_checkfp::pass);

  register_pass(c);
}

Pass_fplan_checkfp::Pass_fplan_checkfp(const Eprp_var& var) : Pass("pass.fplan", var) {}

void Pass_fplan_checkfp::pass(Eprp_var& var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one root lgraph provided!");
  }

  fmt::print("checking floorplan...\n");

  unsigned int issue_counter = 0;

  std::vector<Node> nodes;

  var.lgs[0]->each_hier_fast_direct([&](const Node& n) -> bool {
    if (!n.is_type_synth()) {
      return true;
    }

    if (!n.has_place()) {
      fmt::print("ERROR: node {} has no place information!\n", n.debug_name());
      issue_counter++;
      return true;
    }

    nodes.emplace_back(n);

    return true;
  });

  for (auto n : nodes) {
    var.lgs[0]->each_hier_fast_direct([&](const Node& nt) -> bool {
      if (!nt.is_type_synth()) {
        return true;
      }

      I(nt.has_place());

      Ann_place tp = nt.get_place();
      Ann_place np = n.get_place();

      if (np == tp) {
        return true;
      }

      //fmt::print("\n{} vs {}\n", n.debug_name(), nt.debug_name());

      // is (xt, yt) inside a rectangle from (x0, y0) -> (x1, y1)?
      auto inside = [](float x0, float y0, float x1, float y1, float xt, float yt) -> bool {
        bool in_x = (x0 <= xt) && (xt <= x1);
        //fmt::print("x0 ({}) <= xp ({}) <= x1({}): {}\n", x0, xt, x1, in_x);
        bool in_y = (y0 <= yt) && (yt <= y1);
        //fmt::print("y0 ({}) <= yp ({}) <= yp ({}): {}\n", y0, yt, y1, in_y);
        return in_x && in_y;
      };

      float px0 = np.get_pos_x();
      float px1 = np.get_pos_x() + np.get_len_x();
      float py0 = np.get_pos_y();
      float py1 = np.get_pos_y() + np.get_len_y();

      float tx0 = tp.get_pos_x();
      float tx1 = tp.get_pos_x() + tp.get_len_x();
      float ty0 = tp.get_pos_y();
      float ty1 = tp.get_pos_y() + tp.get_len_y();

      bool intersect = false;
      intersect = intersect || inside(tx0, ty0, tx1, ty1, px0, py0);
      intersect = intersect || inside(tx0, ty0, tx1, ty1, px0, py1);
      intersect = intersect || inside(tx0, ty0, tx1, ty1, px1, py0);
      intersect = intersect || inside(tx0, ty0, tx1, ty1, px1, py1);

      if (intersect) {
        fmt::print("intersection detected between nodes {} and {}!\n", n.debug_name(), nt.debug_name());
        // fmt::print("node {} collides with node {}!\n", n1.debug_name(), n2.debug_name());
        // fmt::print("n1\tnid: {}, level: {}, pos: {} ", n1.get_nid(), n1.get_hidx().level, n1.get_hidx().pos);
        //fmt::print("({}, {}) -> ({}, {})\n", psx, psy, pex, pey);
        // fmt::print("n2\tnid: {}, level: {}, pos: {} ", n2.get_nid(), n2.get_hidx().level, n2.get_hidx().pos);
        //fmt::print("({}, {}) -> ({}, {})\n\n", tsx, tsy, tex, tey);

        issue_counter++;
      }

      return true;
    });
  }

  fmt::print("done. {} problems found.\n\n", issue_counter);
}