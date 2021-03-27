#pragma once

#include "FPContainer.hpp"
#include "floorplan.hpp"

// This class is a layout manager that takes geographic hints about where to place clusters.

// It implements two different strategies for floorplanning, one for situations where overlap is tolerated and one for where it is
// not. The first strategy makes optimistic assumptions about the aspect ratio of nodes as they are floorplanned. The second
// strategy implements simulated annealing and uses a slice tree in order to develop a floorplan with no overlap.

// We will make optimistic assumptions on the way down, and clean up on the way up.
// To handle the recursion, we will use a recursive helper to the layout routine.

class geogLayout : public FPContainer {
  bool layoutHelper(double targetWidth, double targetHeight, double curX, double curY, FPObject** layoutStack, int curDepth,
                    FPObject** centerItems, int centerItemsCount);

public:
  geogLayout(unsigned int rsize);

  bool layout(const FPOptimization opt, const double targetAR = 1.0);

  virtual void      outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
  virtual FPObject* addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg,
                                        GeographyHint hint);
  virtual FPObject* addComponentCluster(std::string name, int count, double area, double maxARArg, double minARArg,
                                        GeographyHint hint);
  virtual void      addComponent(FPObject* comp, int count, GeographyHint hint);
};