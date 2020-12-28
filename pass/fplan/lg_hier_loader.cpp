#include "lg_hier_loader.hpp"

void lg_hier_floorp::create_layout(LGraph* lg) {
  attrs[lg].l = std::make_unique<geogLayout>();

  attrs[lg].l->addComponentCluster(std::string(lg->get_name()), attrs[lg].count, get_area(lg), 4.0, 1.0, Center);
}

void lg_hier_floorp::load_lg(LGraph* root, const std::string_view lgdb_path) {
  load_prep_lg(root, lgdb_path);

  std::function<void(LGraph*)> load_lgs = [&](LGraph* lg) {
    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      if (!(attrs[sub_lg].l)) {
        create_layout(sub_lg);
      }

      load_lgs(sub_lg);
    });

    create_layout(lg);

    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      if (debug_print) {
        fmt::print("adding component {} to module {}\n", sub_lg->get_name(), lg->get_name());
      }
      attrs[lg].l->addComponent(attrs[sub_lg].l.get(), attrs[sub_lg].count, Center);
    });
  };

  load_lgs(root);
}