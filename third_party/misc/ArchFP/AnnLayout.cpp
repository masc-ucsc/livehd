#include "AnnLayout.hpp"

annLayout::annLayout(unsigned int rsize) : FPContainer(rsize) {
  type   = Ntype_op::Invalid;
  name   = "Ann";
}

bool annLayout::layout(FPOptimization opt, double targetAR) {}

void annLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {}