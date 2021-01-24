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

  absl::flat_hash_map<Ntype_op, unsigned int> grid_count;
  absl::flat_hash_set<Ntype_op> skip;
  for (auto n : lg->fast()) {
    Ntype_op op = n.get_type_op();
    if (!Ntype::is_synthesizable(op)) {
      continue;
    }

    grid_count[op]++;
  }

  // count and floorplan leaves
  for (auto n : lg->fast()) {
    Ntype_op op = n.get_type_op();
    if (!Ntype::is_synthesizable(op)) {
      continue;
    }

    if (skip.contains(op)) {
      continue;
    }

    I(na.has_dim(op));
    
    auto  dim       = na.get_dim(op);
    float node_area = dim.area;  // TODO: can we calculate some sort of bitwidth for the node?

    unsigned int count = 1;
    if (grid_count[op] >= grid_thresh[op] && grid_thresh[op] > 0) {
      count = grid_count[op];
      skip.emplace(op);
    }

    if (debug_print) {
      fmt::print("adding {} of leaf {} to cluster of lg {}", count, n.get_type_name(), lg->get_name());
      fmt::print("\tarea: {}, min asp: {}, max asp: {}\n", node_area, dim.min_aspect, dim.max_aspect);
    }

    l->addComponentCluster(n.get_type_op(), count, node_area, dim.max_aspect, dim.min_aspect, Center);
  }

  l->setName(lg->get_name().data());
  l->setType(Ntype_op::Sub);  // treat nodes placed by us as subnodes
  layouts[lg] = std::move(l);
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