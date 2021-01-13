#include "floorplanner.hpp"

void Lhd_floorplanner::create() {
  bool success = root_layout->layout(AspectRatio, 1);
  if (!success) {
    throw std::runtime_error("unable to lay out floorplan!");
  }
}

void Lhd_floorplanner::write_file(const std::string_view filename) {
  ostream& fos = outputHotSpotHeader(filename.data());
  root_layout->outputHotSpotLayout(fos);
  outputHotSpotFooter(fos);
}

void Lhd_floorplanner::write_lhd() {
  
}