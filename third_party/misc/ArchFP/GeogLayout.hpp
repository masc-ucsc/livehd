#pragma once

#include "floorplan.hpp"
#include "FPContainer.hpp"

// This class is a layout manager that takes geographic hints about where to place clusters.
// Hopefully, it will act as the replacement for at least the simple NoC layout above.
// Perhaps we will need some other NoC layout at some point if we address XBAR type configurations.
// It will do its layout recursively.  This will be our answer to the top-down vs bottom up question.
// We will make optimistic assumptions on the way down, and clean up on the way up.
// To handle the recursion, we will use a recursive helper to the layout routine.

class geogLayout : public FPContainer {
  bool layoutHelper(FPOptimization opt, double targetWidth, double targetHeight, double curX, double curY, FPObject** layoutStack,
                    int curDepth, FPObject** centerItems, int centerItemsCount);
public:
  geogLayout(unsigned int rsize);

  virtual bool      layout(FPOptimization opt, double targetAR = 1.0);
  virtual void      outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
  virtual FPObject* addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg,
                                        GeographyHint hint);
  virtual FPObject* addComponentCluster(std::string name, int count, double area, double maxARArg, double minARArg, GeographyHint hint);
  virtual void      addComponent(FPObject* comp, int count, GeographyHint hint);
};