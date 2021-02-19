#pragma once

#include "BagLayout.hpp"
#include "floorplan.hpp"

class fixedLayout : public bagLayout {
  void morph(double xFactor, double yFactor);

public:
  bool layout(FPOptimization opt, double targetAR = 1.0);
  fixedLayout(const char* filename, double scalingFactor = 1.0);
};