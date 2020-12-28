#pragma once

#include "archfp_driver.hpp"

class lg_hier_floorp : public floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
  void create_layout(LGraph* lg);
};