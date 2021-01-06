#include "lg_flat_floorp.hpp"

void Lg_flat_floorp::load_lg(LGraph* root, const std::string_view lgdb_path) {
  absl::flat_hash_map<LGraph*, unsigned int> count;

  std::function<void(LGraph*)> count_lgs = [&](LGraph* lg) {
    if (lg->get_down_nodes_map().size() == 0) {
      count[lg]++;
    } else {
      lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
        (void)n;
        LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

        count_lgs(sub_lg);
      });
    }
  };

  /*
  fmt::print("root: {}\n", root->get_name());
  root->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    (void)n;
    LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

    fmt::print("  sub: {}\n", sub_lg->get_name());
  });
  fmt::print("size: {}\n", root->get_down_nodes_map().size());
  */

  if (root->get_down_nodes_map().size() == 0) {
    throw std::invalid_argument("root has no child lgraphs!");
  }
  
  count_lgs(root);

  for (const auto& pair : count) {
    const auto& name = pair.first->get_name().data();

    if (debug_print) {
      fmt::print("adding {} of component {} to root\n", pair.second, name);
    }
    root_layout->addComponentCluster(name, pair.second, get_area(pair.first), 8.0, 1.0, Center);
  }
}