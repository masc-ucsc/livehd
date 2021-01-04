#pragma once

#include "floorplanner.hpp"
#include "node_pin.hpp"
#include "lgedgeiter.hpp"

class node_flat_floorp : public floorplanner {
public:
  void load_lg(LGraph* root, const std::string_view lgdb_path);

private:
};