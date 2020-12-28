#include "lg_flat_loader.hpp"

void lg_flat_floorp::add_layout(LGraph* existing_lg, LGraph* lg) {
  attrs[existing_lg].l->addComponentCluster(std::string(lg->get_name()), attrs[lg].count, get_area(lg), 50.0, 1.0, Center);
}

void lg_flat_floorp::load_lg(LGraph* root, const std::string_view lgdb_path) {
  load_prep_lg(root, lgdb_path);

  attrs[root].l = std::make_unique<geogLayout>();

  std::function<void(LGraph*)> load_lgs = [&](LGraph* lg) {
    if (lg->get_down_nodes_map().size() == 0) {
      if (debug_print) {
        fmt::print("creating component {}\n", lg->get_name());
      }
      add_layout(root, lg);
      return;
    }

    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      load_lgs(sub_lg);
    });
  };

  load_lgs(root);
}