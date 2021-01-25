//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplanner.hpp"

#include "cell.hpp"
#include "core/ann_place.hpp"
#include "core/lgedgeiter.hpp"

Lhd_floorplanner::Lhd_floorplanner() {
  // set how many nodes of a given type must be encountered before they are put in a grid together
  // thresholds can be 0, in which case that type of leaf is never put in a grid.
  for (uint8_t type = 0; type < (uint8_t)Ntype_op::Last_invalid; type++) {
    grid_thresh[(Ntype_op)type] = 6;
  }

  // set memory elements with a lower threshold, because they should be grouped together
  grid_thresh[Ntype_op::Memory] = 4;
  grid_thresh[Ntype_op::Sflop]  = 4;
  grid_thresh[Ntype_op::Aflop]  = 4;
  grid_thresh[Ntype_op::Latch]  = 4;
  grid_thresh[Ntype_op::Fflop]  = 4;
}

Lhd_floorplanner::~Lhd_floorplanner() { delete layouts[root_lg]; }

GeographyHint Lhd_floorplanner::randomHint() {
  static size_t sel = 0;

  sel = (sel + 1) % hint_seq.size();
  return hint_seq[sel];
}

void Lhd_floorplanner::create() {
  bool success = layouts[root_lg]->layout(AspectRatio);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  ostream& fos = outputHotSpotHeader(filename.data());
  layouts[root_lg]->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd() {
  // make sure all nodes have a hier color
  root_lg->each_hier_fast_direct([](Node& n) {
    n.set_hier_color(0);
    return true;
  });

  absl::flat_hash_set<Hierarchy_index> hidx_used_set;
  layouts[root_lg]->outputLGraphLayout(root_lg, root_lg, root_lg->ref_htree()->root_index(), hidx_used_set);

  root_lg->each_hier_fast_direct([](const Node& n) {
    if (!n.is_type_synth()) {
      return true;
    }

    // basic sanity checking for returned floorplans
    I(n.is_hierarchical());
    I(n.has_hier_color());
    I(n.get_hier_color() == 1);  // all (synthesizable) nodes have been visited by floorplanner
    I(n.has_place());

    if (debug_print) {
      fmt::print("node {} ", n.debug_name());
      fmt::print("level {} pos {} ", n.get_hidx().level, n.get_hidx().pos);

      const Ann_place& p = n.get_place();
      fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f}, height: {:.3f}\n",
                 p.get_pos_x(),
                 p.get_pos_y(),
                 p.get_len_x(),
                 p.get_len_y());
    }

    return true;
  });
}
