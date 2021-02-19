#pragma once

#include "FPContainer.hpp"
#include "floorplan.hpp"

// This will just be a collection of components to lay out in the given aspect ratio.
// If any of the components have a count > 1, a grid will be created to put it in.
class bagLayout : public FPContainer {
protected:
  bool locked;
  void recalcSize();

public:
  bagLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};