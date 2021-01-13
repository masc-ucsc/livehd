#include "node_hier_floorp.hpp"

#include <functional>
#include <memory>

#include "ann_place.hpp"
#include "node_type_area.hpp"

void Node_hier_floorp::load_lg_nodes(LGraph* lg, const std::string_view lgdb_path) {
  if (layouts[lg]) {
    if (debug_print) {
      fmt::print("(layout for {} already exists)\n", lg->get_name());
    }
    return;
  }

  auto l = std::make_unique<geogLayout>();

  Ntype_area na(lgdb_path);

  // count and floorplan subnodes
  absl::flat_hash_map<LGraph*, unsigned int> sub_lg_count;
  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    (void)n;
    LGraph* sub_lg = LGraph::open(lgdb_path, lgid);
    sub_lg_count[sub_lg]++;
  });

  for (auto pair : sub_lg_count) {
    const auto& sub_lg = pair.first;

    I(layouts[sub_lg]);
    if (debug_print) {
      fmt::print("adding {} of subcomponent {} to cluster of lg {}\n", sub_lg_count[sub_lg], sub_lg->get_name(), lg->get_name());
    }

    l->addComponent(layouts[sub_lg].get(), sub_lg_count[sub_lg], Center);
  }

  // floorplan leaves
  for (auto n : lg->fast()) {
    Ntype_op op = n.get_type_op();
    if (!Ntype::is_synthesizable(op)) {
      continue;
    }

    I(na.has_dim(op));
    auto  dim       = na.get_dim(op);
    float node_area = dim.area;  // TODO: can we calculate some sort of bitwidth for the node?

    // count is 1 because even if nodes are the same type, they aren't really identical,
    // and if count > 1 the cluster is treated as identical.
    if (debug_print) {
      fmt::print("adding leaf {} to cluster of lg {}", n.get_type_name(), lg->get_name());
      fmt::print(", area: {}, min asp: {}, max asp: {}\n", node_area, dim.min_aspect, dim.max_aspect);
    }
    l->addComponentCluster(n.get_type_name().data(), 1, node_area, dim.max_aspect, dim.min_aspect, Center);
  }

  layouts[lg] = std::move(l);
}

// NOTE: advanced global hierarchy work with nodes basically requires the extraction of the root hierarchy tree.
// Any other method will bring you much pain and suffering.
void Node_hier_floorp::color_nodes() {
  long counter = 1;  // 0 reserved for sub of root

  auto htree = root_lg->ref_htree();

  // 1. write information for leaf nodes, which is easy since they are never put in grids.
  for (const auto& hidx : htree->depth_preorder()) {
    LGraph* lg = htree->ref_lgraph(hidx);
    for (auto fn : lg->fast()) {
      if (!fn.is_type_synth()) {
        continue;
      }

      Node hn(root_lg, hidx, fn.get_compact_class());

      Tag_info ti(counter++, -1);
      send_map[hn.get_compact()] = ti;

      recv_map[ti] = hn.get_compact();

      /*
      fmt::print("mapping synth leaf {} (l: {} p: {}) to id {}, parent {}\n",
                 hn.debug_name(),
                 hn.get_hidx().level,
                 hn.get_hidx().pos,
                 ti.node_color,
                 ti.parent_color);
      */
    }
  }

  // since subnodes can be floorplanned as grids, more work is needed to track which subnode maps to which floorplan instance
  for (const auto& hidx : htree->depth_preorder()) {
    LGraph* lg = htree->ref_lgraph(hidx);
    // 2. count subnodes for a given node
    absl::flat_hash_map<LGraph*, unsigned int> seen_nodes;
    lg->each_sub_fast([&](Node& sn, Lg_type_id lgid) {
      (void)sn;

      LGraph* sub_lg = LGraph::open(root_lg->get_path(), lgid);
      seen_nodes[sub_lg]++;
    });

    // 3. if there is more than one subnode type for a given node, they are placed in a grid.
    // To track this placement, write the same color for all instances of the subnode and write the parent.
    absl::flat_hash_map<LGraph*, unsigned int> counter_val;
    lg->each_sub_fast([&](Node& sn, Lg_type_id lgid) {
      LGraph* sn_lg = LGraph::open(root_lg->get_path(), lgid);
      Node    hsn(root_lg, hidx, sn.get_compact_class());

      Tag_info ti;

      if (seen_nodes[sn_lg] > 1) {
        if (counter_val[sn_lg] == 0) {
          ti.node_color      = counter;  // make sure all repeated instances have the same node color
          counter_val[sn_lg] = counter++;
        } else {
          ti.node_color = counter_val[sn_lg];
        }

        if (hsn.is_root()) {
          ti.parent_color = send_map[hsn.get_compact()].node_color;
        } else {
          auto up         = hsn.get_up_node().get_compact();
          ti.parent_color = send_map[up].node_color;
        }

        /*
        fmt::print("mapping rep sub {} (l: {} p: {}) to id {}, parent {}\n",
                   hsn.debug_name(),
                   hsn.get_hidx().level,
                   hsn.get_hidx().pos,
                   ti.node_color,
                   ti.parent_color);
        */

      } else {
        ti = {counter++, -1};

        /*
        fmt::print("mapping first sub {} (l: {} p: {}) to id {}, parent {}\n",
                   hsn.debug_name(),
                   hsn.get_hidx().level,
                   hsn.get_hidx().pos,
                   ti.node_color,
                   ti.parent_color);
        */
      }

      send_map[hsn.get_compact()] = ti;

      recv_map[ti] = hsn.get_compact();
    });
  }
  
  for (const auto& hidx : htree->depth_preorder()) {
    LGraph* lg = htree->ref_lgraph(hidx);
    lg->each_sub_fast([&](Node& sn, Lg_type_id lgid) { Node hsn(root_lg, hidx, sn.get_compact_class()); });

    for (auto fn : lg->fast()) {
      Node hn(root_lg, hidx, fn.get_compact_class());
      if (!fn.is_type_synth() && !fn.is_type_sub()) {
        continue;
      }

      fmt::print("node {}: ", hn.debug_name());

      if (hn.is_root()) {
        fmt::print("root node ");
      } else {
        fmt::print("child of {} ", hn.get_up_node().debug_name());
      }

      fmt::print("level: {}, pos: {} ", hn.get_hidx().level, hn.get_hidx().pos);
      fmt::print("nc: {}, pc {} ", send_map[hn.get_compact()].node_color, send_map[hn.get_compact()].parent_color);
      fmt::print("\n");
    }
  }
}

void Node_hier_floorp::load(LGraph* root, const std::string_view lgdb_path) {
  fmt::print("\n");
  root_lg = root;

  auto check_area_exists = [&lgdb_path](LGraph* lg) {
    const Ntype_area na(lgdb_path);
    for (auto n : lg->fast(true)) {
      Ntype_op op = n.get_type_op();
      if (Ntype::is_synthesizable(op) && !(na.has_dim(op))) {
        std::string errstr = "node type ";
        errstr.append(Ntype::get_name(op));
        errstr.append(" has no area information!");
        throw std::runtime_error(errstr);
      }
    }
  };

  check_area_exists(root);

  /*
  // not used yet
  for (auto n : root->fast(true)) {
    n.set_color(0);
  }
  */

  color_nodes();
  return;

  // since coloring is only applied to subnodes, color all leaf children of root with color 1
  // color_lg(root, 1);
  // if (debug_print) {
  // fmt::print("coloring root with color 1\n\n");
  //}

  std::function<void(LGraph*)> load_nodes = [&](LGraph* lg) {
    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      load_nodes(sub_lg);
    });

    load_lg_nodes(lg, lgdb_path);
  };

  load_nodes(root);

  root_layout = std::move(layouts[root]);
}