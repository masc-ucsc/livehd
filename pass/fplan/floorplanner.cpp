//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplanner.hpp"

#include "cell.hpp"
#include "core/ann_place.hpp"
#include "core/lgedgeiter.hpp"

Lhd_floorplanner::Lhd_floorplanner(Node_tree&& nt_arg) : root_lg(nt_arg.get_root_lg()), nt(nt_arg), na(root_lg->get_path()) {
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

Lhd_floorplanner::~Lhd_floorplanner() {
  delete layouts[nt.root_index()];
  root_lg = nullptr;
}

GeographyHint Lhd_floorplanner::randomHint(int count) const {
  static size_t sel = 0;

  sel = (sel + 1) % hint_seq.size();
  if (count == 2) {
    return hint_seq_2[sel];
  }

  return hint_seq[sel];
}

void Lhd_floorplanner::create() {
  bool success = layouts[nt.root_index()]->layout(AspectRatio);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  ostream& fos = outputHotSpotHeader(filename.data());
  layouts[nt.root_index()]->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd() {
  // make sure all nodes have a hier color
  root_lg->each_hier_fast_direct([](Node& n) {
    n.set_hier_color(0);
    return true;
  });

  unsigned int placed_nodes = layouts[nt.root_index()]->outputLGraphLayout(nt, nt.root_index());

  unsigned int node_count = 0;
  root_lg->each_hier_fast_direct([&node_count](const Node& n) {
    if (!n.is_type_synth()) {
      return true;
    }

    node_count++;

    if (debug_print) {
      fmt::print("node {} ", n.debug_name());
    }

    // basic sanity checking for returned floorplans
    I(n.is_hierarchical());
    if (debug_print) {
      fmt::print("level {} pos {} ", n.get_hidx().level, n.get_hidx().pos);
    }

    I(n.has_place());
    I(n.get_place().is_valid());

    if (debug_print) {
      const Ann_place& p = n.get_place();
      fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f}, height: {:.3f}\n",
                 p.get_pos_x(),
                 p.get_pos_y(),
                 p.get_len_x(),
                 p.get_len_y());
    }

    return true;
  });

  // check that the correct number of nodes are in the floorplan
  I(node_count == placed_nodes);
}
