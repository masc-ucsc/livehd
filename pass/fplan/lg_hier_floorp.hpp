#pragma once

#include "floorplanner.hpp"

class lg_hier_floorp : public floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
  void create_layout(LGraph* lg);
  bool has_layout(LGraph* lg) { return attrs[lg].l.operator bool(); }
};