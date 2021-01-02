#include "lg_hier_floorp.hpp"

void lg_hier_floorp::create_module(LGraph* lg) {
  (void)lg;
  /*
  attrs[lg].l = std::make_unique<geogLayout>();
  attrs[lg].l->addComponentCluster(std::string(lg->get_name()), 1, get_area(lg), 4.0, 1.0, Center);
  if (debug_print) {
    fmt::print("creating {}\n", lg->get_name());
  }
  */
}

void lg_hier_floorp::load_lg(LGraph* root, const std::string_view lgdb_path) {
  (void)root;
  (void)lgdb_path;

  // TODO: not currently working, nodes are re-floorplanned all over the place.

  /*
  root_lg = root;

  // create the root layout so the top-level module doesn't get included in its own floorplan
  attrs[root_lg].l = std::make_unique<geogLayout>();

  std::function<void(LGraph*)> load_lgs = [&](LGraph* lg) {
    // create and load all submodules
    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      load_lgs(sub_lg);
    });

    if (!has_module(lg) && lg->get_down_nodes_map().size() > 0) {
      create_module(lg);

      absl::flat_hash_map<LGraph*, unsigned int> sub_lg_count;
      lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
        (void)n;
        LGraph* sub_lg = LGraph::open(lgdb_path, lgid);
        sub_lg_count[sub_lg]++;
      });

      lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
        (void)n;
        LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

        if (sub_lg_count[sub_lg] > 0) {
          if (debug_print) {
            fmt::print("adding {} submodules of {} to module {}\n", sub_lg_count[sub_lg], sub_lg->get_name(), lg->get_name());
          }

          if (sub_lg->get_down_nodes_map().size() == 0) {
            attrs[lg].l->addComponentCluster(std::string(sub_lg->get_name()), sub_lg_count[sub_lg], get_area(sub_lg), 4.0, 1.0, Center);
          } else {
            attrs[lg].l->addComponent(attrs[sub_lg].l.get(), sub_lg_count[sub_lg], Center);
          }
          sub_lg_count[sub_lg] = 0;
        }
      });
    }
  };

  load_lgs(root);
  */
}