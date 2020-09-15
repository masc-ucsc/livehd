#include "hier_tree.hpp"

#include "fmt/core.h"

void Hier_tree::construct_floorplans() { 
  auto points = d.select_points();

  for (auto point : points) {
    // point is a location on the DAG
  }
}