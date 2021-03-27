#include "AnnLayout.hpp"

annLayout::annLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "Ann";
}

// https://github.com/r3ntru3w4n9/b-star/tree/master/src

bool annLayout::layout(FPOptimization opt, double targetAR) {}

void annLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {}
