#include "node_hier_floorp.hpp"

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

void Node_hier_floorp::color_lg_nodes(LGraph* lg, const std::string_view lgdb_path) {
  static int color_counter = 2;
  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    (void)n;
    LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

    color_lg_nodes(sub_lg, lgdb_path);
  });

  /*
  Coloring is supposed to be unique, except if there are multiple submodules of the same type within the scope of a module.  If
  this is the case, they will be given the same color, since their layout can't really be determined that well from area
  information alone.
  */

  absl::flat_hash_map<LGraph*, int> seen_lgs;
  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    (void)n;
    LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

    if (seen_lgs.contains(sub_lg)) {
      color_lg(sub_lg, seen_lgs[sub_lg]);
      if (debug_print) {
        fmt::print("coloring lg {} with color {} (rep)\n", sub_lg->get_name(), seen_lgs[sub_lg]);
      }
    } else {
      if (debug_print) {
        fmt::print("coloring lg {} with color {} (new)\n", sub_lg->get_name(), color_counter);
      }
      seen_lgs[sub_lg] = color_counter;
      color_lg(sub_lg, color_counter++);
    }
  });
}

void Node_hier_floorp::load(LGraph* root, const std::string_view lgdb_path) {
  fmt::print("\n");

  auto check_area_exists = [&lgdb_path](LGraph* lg) {
    const Ntype_area na(lgdb_path);
    for (auto n : lg->fast()) {
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

  // not used yet
  for (auto n : root->fast()) {
    n.set_color(0);
  }

  // not used yet
  color_lg_nodes(root, lgdb_path);

  // since coloring is only applied to subnodes, color all leaf children of root with color 1
  color_lg(root, 1);
  if (debug_print) {
    fmt::print("coloring root with color 1\n\n");
  }

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