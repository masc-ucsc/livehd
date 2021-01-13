#pragma once

#include "floorplanner.hpp"

class Lg_flat_floorp : public Lhd_floorplanner {
public:
  void load(LGraph* root, const std::string_view lgdb_path);

private:
  float get_lg_area(LGraph* lg);
};