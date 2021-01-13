#include "lg_flat_floorp.hpp"

#include <functional>

#include "lgedgeiter.hpp"
#include "node_type_area.hpp"

float Lg_flat_floorp::get_lg_area(LGraph* lg) {
  Ntype_area na(lg->get_path());

  // use the number of nodes as an approximation of area
  float temp_area = 0.0;
  for (auto node : lg->fast(true)) {
    temp_area += na.get_dim(node.get_type_op()).area;
  }

  return temp_area;
}

void Lg_flat_floorp::load(LGraph* root, const std::string_view lgdb_path) {
  root_lg = root;

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

  if (root->get_down_nodes_map().size() == 0) {
    throw std::invalid_argument("root has no child lgraphs!");
  }

  count_lgs(root);

  for (const auto& pair : count) {
    const auto& name = pair.first->get_name().data();

    if (debug_print) {
      fmt::print("adding {} of component {} to root\n", pair.second, name);
    }
    root_layout->addComponentCluster(name, pair.second, get_lg_area(pair.first), 8.0, 1.0, Center);
  }
}