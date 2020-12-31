#include "floorplanner.hpp"

#include <functional>

#include "lgedgeiter.hpp"

unsigned int floorplanner::get_area(LGraph* lg) {
  // use the number of nodes as an approximation of area
  unsigned int num_nodes = 0;
  for (auto node : lg->fast(true)) {
    (void)node;
    num_nodes++;
  }

  return num_nodes;
}

void floorplanner::load_prep_lg(LGraph* root, const std::string_view lgdb_path) {
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

void floorplanner::create_floorplan(const std::string_view filename) {
  auto& root_layout = attrs[root_lg].l;
  bool success     = root_layout->layout(AspectRatio, 1);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  } else {
    ostream& fos = outputHotSpotHeader(filename.data());
    root_layout->outputHotSpotLayout(fos);
    outputHotSpotFooter(fos);
  }
}
