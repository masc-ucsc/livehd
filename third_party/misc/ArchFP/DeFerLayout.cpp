#include "DeFerLayout.hpp"

#include <cmath>

deferLayout::deferLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "defer";
}

bool deferLayout::layout(FPOptimization opt, double targetAR) {

  if (target == fixed) {
    double ta = totalArea();
    boundX = sqrt((1 + maxWhitespace) * ta / targetAR);
    boundY = sqrt((1 + maxWhitespace) * ta * targetAR);
  }

  // allocate point curves?

  for (size_t i = 0; i < numRuns; i++) {
    // steps:
    // 1. Run partition (using metis replacement)
    // 2. Combine shape curve
    // 3. Find valid points
  }

}

void deferLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {

}