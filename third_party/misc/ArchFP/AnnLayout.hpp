#pragma once

#include "FPContainer.hpp"
#include "floorplan.hpp"

// a layout class implementing Simulated Annealing
// https://scholars.lib.ntu.edu.tw/bitstream/123456789/147652/1/1467.pdf
class annLayout : public FPContainer {
protected:
public:
  annLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};