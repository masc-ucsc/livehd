#pragma once

#include "floorplanner.hpp"
#include "lgedgeiter.hpp"
#include "node_pin.hpp"

class Node_hier_floorp : public Lhd_floorplanner {
public:
  void load(LGraph* root, const std::string_view lgdb_path);

private:
  void load_lg_nodes(LGraph* lg, const std::string_view lgdb_path);
  void color_lg_nodes(LGraph* lg, const std::string_view lgdb_path);

  void color_lg(LGraph* lg, int color) {
    for (auto n : lg->fast()) {
      n.set_color(color);
    }
  }
};