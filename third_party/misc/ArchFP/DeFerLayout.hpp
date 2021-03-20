#pragma once

#include "floorplan.hpp"
#include "FPContainer.hpp"

class deferLayout : public FPContainer {
public:

  // eventually these options will be made available as config options, but constexpr right now

  // what to optimize for
  enum targetVal {fixed, area, area_hpwl};
  constexpr static targetVal target = fixed;

  constexpr static unsigned int numRuns = 8; // number of runs of DeFer to perform
  constexpr static unsigned int maxWhitespace = 10; // maximum whitespace percentage
  constexpr static double aspect = 1.0; // aspect ratio (height / width)
  constexpr static unsigned int metisRuns = 1; // number of hmetis runs (need to translate to some other lib!)
  // hmetis has a "ubFactor"?

  constexpr static int baseN = 9; // "maxN in EP (10 for maximum)"??
  constexpr static int tbaseN = 7; // "maxN in high-level EP (10 for maximum)"??
  constexpr static int shConst = 10000; // Soft-Hard block constant??
  constexpr static int swapN = 2; // Loops of detailed swapping??

  double boundX;
  double boundY;

  deferLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};