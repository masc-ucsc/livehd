#pragma once

#include "FPContainer.hpp"
#include "floorplan.hpp"

// Here is the simplest possible layout manager.
// It takes a collection of identical objects, and lays them out in a grid.
// It tries to get them into a rectangle as close to the specified aspect ratio.
// TODO Consider adding an aspect ratio layout algorithm that is more aggressive.
// Presumably this would see if the grid can be made closer to the desired AR
//    by adding blank components when needed.
class gridLayout : public FPContainer {
  // Store the calculated x and y counts of components in the grid.
  int xCount;
  int yCount;

public:
  gridLayout(unsigned int rsize);

  bool                 layout(FPOptimization opt, double targetAR = 1.0);
  void                 outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
  virtual unsigned int outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);

  // A grid handles its counts different than other components?
};