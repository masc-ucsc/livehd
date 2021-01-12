#include "floorplanner.hpp"

#include <functional>

#include "node_type_area.hpp"
#include "lgedgeiter.hpp"

float Lhd_floorplanner::get_lg_area(LGraph* lg) {
  Ntype_area na(lg->get_path());

  // use the number of nodes as an approximation of area
  float temp_area = 0.0;
  for (auto node : lg->fast(true)) {
    temp_area += na.get_dim(node.get_type_op()).area;
  }

  return temp_area;
}

void Lhd_floorplanner::create_floorplan(const std::string_view filename) {
  bool success = root_layout->layout(AspectRatio, 1);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  } else {
    ostream& fos = outputHotSpotHeader(filename.data());
    root_layout->outputHotSpotLayout(fos);
    outputHotSpotFooter(fos);
  }
}