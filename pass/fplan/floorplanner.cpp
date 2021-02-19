//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplanner.hpp"

#include "cell.hpp"
#include "core/ann_place.hpp"
#include "core/lgedgeiter.hpp"
#include "annotate.hpp"

Lhd_floorplanner::Lhd_floorplanner(Node_tree&& nt_arg) : root_lg(nt_arg.get_root_lg()), nt(std::move(nt_arg)), na(root_lg->get_path()) {
  // set how many nodes of a given type must be encountered before they are put in a grid together
  // thresholds can be 0, in which case that type of leaf is never put in a grid.
  for (uint8_t type = 0; type < (uint8_t)Ntype_op::Last_invalid; type++) {
    grid_thresh[(Ntype_op)type] = 6;
  }

  // set memory elements with a lower threshold, because they should be grouped together
  grid_thresh[Ntype_op::Memory] = 4;
  grid_thresh[Ntype_op::Flop]   = 4;
  grid_thresh[Ntype_op::Latch]  = 4;
  grid_thresh[Ntype_op::Fflop]  = 4;
}

Lhd_floorplanner::~Lhd_floorplanner() {
  delete root_layout;
  root_layout = nullptr;
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

void Lhd_floorplanner::create(float ar) {
  bool success = root_layout->layout(HardAspectRatio, ar);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  ostream& fos = outputHotSpotHeader(filename.data());
  root_layout->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd_node() {
  Ann_node_place::clear(nt.get_root_lg()); // clear out any existing node placements
  clearCount(); // clear ArchFP name counts

  unsigned int placed_nodes = root_layout->outputLGraphLayout(nt, nt.root_index());

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
                 p.get_x(),
                 p.get_y(),
                 p.get_width(),
                 p.get_height());
    }

    return true;
  });

  // check that the correct number of nodes are in the floorplan
  I(node_count == placed_nodes);
}

void Lhd_floorplanner::write_lhd_lg() {
  fmt::print("(not imp)\n");
}
