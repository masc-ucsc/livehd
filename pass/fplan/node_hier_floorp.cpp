//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_hier_floorp.hpp"

#include <functional>
#include <memory>

#include "ann_place.hpp"
#include "mmap_map.hpp"

Node_hier_floorp::Node_hier_floorp(Node_tree&& nt_arg) : Lhd_floorplanner(std::move(nt_arg)) {}

FPContainer* Node_hier_floorp::load_lg_nodes(const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, Lgraph* lg, const Tree_index tidx) {
  /*
    It would be very nice if we could skip floorplanning for nodes that have already been loaded into ArchFP elsewhere.
    However, ArchFP does not support calling addComponent more than once on the same pointer, so we are forced to deep copy repeated
    subgraphs.
  */

  FPContainer* l = makeNode(hint_map, tidx, lg->size());

  const std::string_view path = root_lg->get_path();

  // count instances of leaves and subnodes for later use
  absl::flat_hash_map<Lgraph*, unsigned int>  sub_lg_count;
  absl::flat_hash_map<Ntype_op, unsigned int> grid_count;
  for (auto child_idx : nt.children(tidx)) {
    const Node& child = nt.get_data(child_idx);
    if (child.is_type_sub_present()) {
      const auto child_lg = Lgraph::open(path, child.get_type_sub());
      sub_lg_count[child_lg]++;
    } else {  // if (child.is_type_synth())
      grid_count[child.get_type_op()]++;
    }
  }

  absl::flat_hash_set<Ntype_op> skip;
  for (auto child_idx : nt.children(tidx)) {
    const Node& child = nt.get_data(child_idx);

    if (child.is_type_sub_present()) {
      const auto child_lg = Lgraph::open(path, child.get_type_sub());
      if (sub_lg_count[child_lg] == 0) {
        continue;
      }

      if (debug_print) {
        fmt::print("generating subcomponent {}\n", child_lg->get_name());
      }

      FPContainer* subl = load_lg_nodes(hint_map, child_lg, child_idx);

      if (debug_print) {
        fmt::print("done.\n");
      }

      if (debug_print) {
        fmt::print("adding {} of subcomponent {} to cluster of lg {}\n",
                   sub_lg_count[child_lg],
                   child_lg->get_name(),
                   lg->get_name());
      }

      addSub(l, hint_map, child.get_compact(), subl, sub_lg_count[child_lg]);

      sub_lg_count[child_lg] = 0;  // set to zero so no other copies of the child lgraph get added

    } else {  // if (child.is_type_synth()) {
      Ntype_op op = child.get_type_op();
      if (skip.contains(op)) {
        continue;
      }

      if (!na.has_dim(op)) {
        std::string errstr = "node type ";
        errstr.append(Ntype::get_name(op));
        errstr.append(" has no area information!");
        throw std::runtime_error(errstr);
      }

      auto  dim       = na.get_dim(op);
      float node_area = dim.area;

      unsigned int count = 1;
      if (grid_count[op] >= grid_thresh[op] && grid_thresh[op] > 0) {
        count = grid_count[op];
        skip.emplace(op);
      }

      if (debug_print) {
        fmt::print("adding {} of leaf {} to cluster of lg {}", count, child.get_type_name(), lg->get_name());
        fmt::print("\tarea: {}, min asp: {}, max asp: {}\n", node_area, dim.min_aspect, dim.max_aspect);
      }

      addLeaf(l, hint_map, child.get_compact(), op, count, node_area, dim.max_aspect, dim.min_aspect);
    }
  }

  l->setName(lg->get_name().data());
  l->setType(Ntype_op::Sub);  // treat nodes placed by us as subnodes

  return l;
}

void Node_hier_floorp::load() {
  mmap_lib::map<Node::Compact, GeographyHint> hint_map(root_lg->get_path(), "node_hints");
  root_layout = load_lg_nodes(hint_map, root_lg, mmap_lib::Tree_index::root());
  I(root_layout);
}
