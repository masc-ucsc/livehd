#pragma once

#include "floorplanner.hpp"

class Lg_hier_floorp : public Lhd_floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
  void create_module(LGraph* lg);
  // bool has_module(LGraph* lg) { return attrs[lg].l.operator bool(); }
};