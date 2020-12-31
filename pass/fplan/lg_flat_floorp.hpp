#pragma once

#include "floorplanner.hpp"

class lg_flat_floorp : public floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
  void add_layout(LGraph* existing_lg, LGraph* lg);
};