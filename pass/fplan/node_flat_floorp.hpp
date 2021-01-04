#pragma once

#include "floorplanner.hpp"
#include "node_pin.hpp"
#include "lgedgeiter.hpp"

class Node_flat_floorp : public Lhd_floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
};