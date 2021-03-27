#pragma once

#include <iostream>

#include "FPObject.hpp"
#include "cell.hpp"
#include "floorplan.hpp"

// This class is meant to be a standin for a real component from M5 or whatever this eventually merge into.
class dummyComponent {
  Ntype_op    type;
  std::string name;

public:
  dummyComponent(Ntype_op typeArg);
  dummyComponent(std::string name);

  std::string getName() const { return name; }
  Ntype_op    getType() const { return type; }
  void        myPrint();
};

extern std::ostream& operator<<(std::ostream& s, dummyComponent& c);

// This class is the lowest level in the component hierarchy.
// It is just the wrapper around an M5 Component.
class FPCompWrapper : public FPObject {
  // For now, we will use a silly standin for the M5 Component object that just has a name.
  dummyComponent* component;  // The pointer to the component of interest.

  // Aspect ratio constraints.
  // Some components are very precise about their aspect ratio.
  // This is very true for example for memory or buses!
  // Therefore, we will store both a minimum and maximum aspect ratio.
  // These ratios are the long side over the short side, so they should be >= 1.
  // A value of 1.0 for maximum ratio forces a square.
  // If min and max are the same, the component is forced to have that ration.
  double minAspectRatio;  // The minimum ratio of the long side to the short side.
  double maxAspectRatio;  // The maximum ratio of the long side to the short side.

  double ARInRange(double AR);
  void   flip();

public:
  FPCompWrapper(dummyComponent* comp, double minAR, double maxAR, double area, int count);
  FPCompWrapper(std::string nameArg, double xArg, double yArg, double widthArg, double heightArg);
  ~FPCompWrapper();

  double          getMinAR() const { return minAspectRatio; }
  double          getMaxAR() const { return maxAspectRatio; }
  dummyComponent* getComp() const { return component; }

  virtual bool layout(FPOptimization opt, double targetAR = 1.0);
};