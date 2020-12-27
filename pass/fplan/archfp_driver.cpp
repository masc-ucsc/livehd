#include "archfp_driver.hpp"

void archfp_driver::create_layout(LGraph* lg) {
  attrs[lg].l = new geogLayout();
  attrs[lg].l->addComponentCluster(std::string(lg->get_name()), attrs[lg].count, 10, 50.0, 1.0, Center);
}

void archfp_driver::add_layout(LGraph* existing_lg, LGraph* lg) {
  attrs[existing_lg].l->addComponentCluster(std::string(lg->get_name()), attrs[lg].count, 10, 50.0, 1.0, Center);
}

void archfp_driver::load_prep(LGraph* root, const std::string_view lgdb_path) {
  root_lg = root;

  std::function<void(LGraph*)> count_lgs = [&](LGraph* lg) {
    attrs[lg].count++;

    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      (void)n;
      LGraph* sub_lg = LGraph::open(lgdb_path, lgid);

      count_lgs(sub_lg);
    });
  };

  count_lgs(root);
}

// load LGraphs into ArchFP hierarchically
void archfp_driver::load_hier_lg(LGraph* root, const std::string_view lgdb_path) {
  load_prep(root, lgdb_path);

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
      attrs[lg].l->addComponent(attrs[sub_lg].l, attrs[sub_lg].count, Center);
    });
  };

  load_lgs(root);
}

// load leaf LGraph modules only, ignoring hierarchy (much faster!)
void archfp_driver::load_flat_lg(LGraph* root, std::string_view lgdb_path) {
  load_prep(root, lgdb_path);

  attrs[root].l = new geogLayout();

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

void archfp_driver::create_floorplan(const std::string_view filename) {
  auto root_layout = attrs[root_lg].l;
  bool success     = root_layout->layout(AspectRatio, 1);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  } else {
    ostream& fos = outputHotSpotHeader(filename.data());
    root_layout->outputHotSpotLayout(fos);
    outputHotSpotFooter(fos);
  }
}