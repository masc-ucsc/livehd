#include <vector>

#include "ann_place.hpp"
#include "pass_fplan.hpp"

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

  unsigned int issue_counter = 0;

  std::vector<Ann_place> places;

  // TODO: once I get more confident that the hierarchy is built correctly, optimize this using hierarchy tree
  var.lgs[0]->each_hier_fast_direct([&](const Node& n) -> bool {
    if (!n.is_type_synth()) {
      return true;
    }

    if (!n.has_place()) {
      fmt::print("ERROR: node {} has no place information!\n", n.debug_name());
      issue_counter++;
      return true;
    }

    places.emplace_back(n.get_place());

    return true;
  });

  for (auto p : places) {
    var.lgs[0]->each_hier_fast_direct([&](const Node& n) -> bool {
      if (!n.is_type_synth()) {
        return true;
      }

      I(n.has_place());

      Ann_place np  = n.get_place();

      if (np == p) {
        return true;
      }

      float psx = p.get_pos_x();
      float pex = p.get_pos_x() + p.get_len_x();
      float psy = p.get_pos_y();
      float pey = p.get_pos_y() + p.get_len_y();

      float tsx = np.get_pos_x();
      float tex = np.get_pos_x() + np.get_len_x();
      float tsy = np.get_pos_y();
      float tey = np.get_pos_y() + np.get_len_y();

      bool clear_x = (pex <= tsx) || (psx >= tex);
      bool clear_y = (pey <= tsy) || (psy >= tey);

      if (!clear_x && !clear_y) {
        //fmt::print("node {} collides with node {}!\n", n1.debug_name(), n2.debug_name());
        //fmt::print("n1\tnid: {}, level: {}, pos: {} ", n1.get_nid(), n1.get_hidx().level, n1.get_hidx().pos);
        fmt::print("({}, {}) -> ({}, {})\n", psx, psy, pex, pey);
        //fmt::print("n2\tnid: {}, level: {}, pos: {} ", n2.get_nid(), n2.get_hidx().level, n2.get_hidx().pos);
        fmt::print("({}, {}) -> ({}, {})\n\n", tsx, tsy, tex, tey);

        issue_counter++;
      }

      return true;
    });
  }

  fmt::print("checking floorplan...\n");

  // Pass_fplan_checkfp c(var);

  fmt::print("done. {} problems found.\n\n", issue_counter);
}