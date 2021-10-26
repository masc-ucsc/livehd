//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplanner.hpp"

#include <stdexcept>
#include <typeinfo>

#include "annotate.hpp"
#include "cell.hpp"
#include "core/ann_place.hpp"
#include "core/lgedgeiter.hpp"
#include "helpers.hpp"

Lhd_floorplanner::Lhd_floorplanner(Node_tree&& nt_arg)
    : root_lg(nt_arg.get_root_lg()), nt(std::move(nt_arg)), na(root_lg->get_path()), root_layout(nullptr) {
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
  if (root_layout != nullptr) {
    // only call delete on root_layout if it exists (may not if root floorplan fails)
    delete root_layout;
  }
  root_layout = nullptr;
  root_lg     = nullptr;
}

GeographyHint Lhd_floorplanner::randomHint(int count) const {
  static size_t sel = 0;

  sel = (sel + 1) % hint_seq.size();
  if (count % 2 == 0) {
    return hint_seq_2[sel];
  }

  return hint_seq[sel];
}

FPContainer* Lhd_floorplanner::makeNode(const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, const Tree_index tidx,
                                        size_t size) {
  FPContainer* l;
  if (!tidx.is_root() && hint_map.has(nt.get_data(tidx).get_compact())) {
    l = new geogLayout(size);
  } else {
    l = new annLayout(size);
  }

  return l;
}

void Lhd_floorplanner::addSub(FPContainer* c, const mmap_lib::map<Node::Compact, GeographyHint>& hint_map,
                              const Node::Compact& child_c, FPObject* comp, int count) {
  if (typeid(*c) == typeid(geogLayout)) {
    if (hint_map.has(child_c)) {
      GeographyHint hint = hint_map.get(child_c);
      static_cast<geogLayout*>(c)->addComponent(comp, count, hint);
    } else {
      static_cast<geogLayout*>(c)->addComponent(comp, count, randomHint(count));
    }
  } else {
    static_cast<annLayout*>(c)->addComponent(comp, count);
  }
}

void Lhd_floorplanner::addLeaf(FPContainer* c, const mmap_lib::map<Node::Compact, GeographyHint>& hint_map,
                               const Node::Compact& child_c, Ntype_op type, int count, double area, double maxARArg,
                               double minARArg) {
  if (typeid(*c) == typeid(geogLayout)) {
    if (hint_map.has(child_c)) {
      GeographyHint hint = hint_map.get(child_c);
      static_cast<geogLayout*>(c)->addComponentCluster(type, count, area, maxARArg, minARArg, hint);
    } else {
      static_cast<geogLayout*>(c)->addComponentCluster(type, count, area, maxARArg, minARArg, randomHint(count));
    }
  } else {
    static_cast<annLayout*>(c)->addComponentCluster(type, count, area, maxARArg, minARArg);
  }
}

void Lhd_floorplanner::create(FPOptimization opt, float ar) {
  bool success = root_layout->layout(opt, ar);
  if (!success) {
    if (typeid(*root_layout) == typeid(geogLayout)) {
      // geographic layouts assume that the user's input is valid and generates illegal floorplans if this is not the case
      fmt::print("WARNING: floorplan may contain overlapping layouts.  Adjusting the overall aspect ratio is recommended.\n");
    } else {
      throw std::runtime_error("floorplan failed!");
    }
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  std::ostream& fos = outputHotSpotHeader(filename.data());
  root_layout->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd_node() {
  Ann_node_place::clear(nt.get_root_lg());  // clear out any existing node placements
  clearCount();                             // clear ArchFP name counts

  unsigned int placed_nodes = root_layout->outputLgraphLayout(nt, mmap_lib::Tree_index::root());

  unsigned int node_count = 0;
  for(const auto n:root_lg->fast(true)) {
    if (!n.is_type_synth()) {
      continue;
    }

    node_count++;

    if (debug_print) {
      fmt::print("node {} ", n.debug_name());
    }

    // basic sanity checking for returned floorplans
    I(n.is_hierarchical());
    if (debug_print) {
      fmt::print("hidx {} ", n.get_hidx());
    }

    I(n.has_instance_name());

    I(n.has_place());
    I(n.get_place().is_valid());

    if (debug_print) {
      const Ann_place& p = n.get_place();
      fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f}, height: {:.3f}\n", p.get_x(), p.get_y(), p.get_width(), p.get_height());
    }
  }

  // check that the correct number of nodes are in the floorplan
  I(node_count == placed_nodes);
}

void Lhd_floorplanner::write_lhd_lg() { fmt::print("(not imp)\n"); }
