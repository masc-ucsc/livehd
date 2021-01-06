#pragma once

#include "floorplanner.hpp"

class Lg_flat_floorp : public Lhd_floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
};