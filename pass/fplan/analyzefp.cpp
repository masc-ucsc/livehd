//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ann_place.hpp"
#include "analyzefp.hpp"
#include "lgedgeiter.hpp"
#include <limits>

void Pass_fplan_analyzefp::setup() {
  auto a = Eprp_method("pass.fplan.analyzefp",
                       "return information about a given floorplan within a livehd hierarchy",
                       &Pass_fplan_analyzefp::pass);

  a.add_label_optional("regularity", "determine the amount of regularity in the design", "false");
  a.add_label_optional("hpwl", "??", "false");
  a.add_label_optional("all", "run all available kinds of analysis on the floorplan", "false");

  register_pass(a);
}

Pass_fplan_analyzefp::Pass_fplan_analyzefp(const Eprp_var& var) : Pass("pass.fplan", var) {
  LGraph* root = var.lgs[0];

  const float fmax = std::numeric_limits<float>::max();
  float min_x = fmax, max_x = 0.0f, min_y = fmax, max_y = 0.0f;
  for (auto n : root->fast()) {
    if (!n.is_type_synth() && !n.is_type_sub()) {
      continue;
    }

    Node hn = Node(root, root->ref_htree()->root_index(), n.get_compact_class());
    if (!hn.has_place()) {
      throw std::runtime_error("node has no area information!");
    }

    const Ann_place& p = hn.get_place();
    if (p.get_pos_x() < min_x) {
      min_x = p.get_pos_x();
    }

    if (p.get_pos_y() < min_y) {
      min_y = p.get_pos_y();
    }

    if (p.get_pos_x() + p.get_len_x() > max_x) {
      max_x = p.get_pos_x() + p.get_len_x();
    }

    if (p.get_pos_y() + p.get_len_y() > max_y) {
      max_y = p.get_pos_y() + p.get_len_y();
    }
  }

  const float area = (max_x - min_x) * (max_y - min_y);
  fmt::print("\nwidth: {:.3f} mm², height: {:.3f} mm², area: {:.3f} mm², {} components\n", max_x, max_y, area, root->size());

  if (var.get("hpwl") == "true" || var.get("all") == "true") {
    // TODO
  }

  // computing the "regularity" of a hierarchical design using a method presented in the HiReg paper:
  // regularity = 1 - (area only counting instances of a given LGraph once) / (area counting all instances of an LGraph)
  if (var.get("regularity") == "true" || var.get("all") == "true") {
    // come up with some metric for regularity - HiReg has one, but it requires a hierarchy DAG which is annoying to generate.
  }
}

void Pass_fplan_analyzefp::pass(Eprp_var& var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs provided!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one root lgraph provided!");
  }

  Pass_fplan_analyzefp a(var);
}
