#include "FixedLayout.hpp"

#include <fstream>
#include <cmath>

#include "FPCompWrapper.hpp"
#include "FPObject.hpp"

void fixedLayout::morph(double xFactor, double yFactor) {
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    comp->setLocation(comp->getX() * xFactor, comp->getY() * yFactor);
    comp->setSize(comp->getWidth() * xFactor, comp->getHeight() * yFactor);
  }
}

// This will read in a hotspot file, and create a bag layout that is locked.
// NOTE: In general, the layout method on a bag layout manager would not have been able to create the resultant layout.
fixedLayout::fixedLayout(const char* filename, double scalingFactor) : bagLayout(1) {
  locked = true;

  std::ifstream in(filename);

  // cout << "In read of fixedLayout\n";
  while (true) {
    char first = in.peek();
    if (in.eof() || in.bad() || in.fail())
      break;
    if (first == '\n') {
      in.get();
      continue;
    }
    if (first == '#')  // || first == ' ')
    {
      char getChar;
      while (!in.eof()) {
        getChar = in.get();
        // cout << getChar;
        if (getChar == '\n')
          break;
      }
      continue;
    }
    // Now we should have a real line.
    std::string name;
    if (in.peek() == ' ')
      name = "";
    else
      in >> name;
    // cout << "name is'" << name << "'\n";
    double x, y, width, height;
    in >> width >> height >> x >> y;
    this->addComponent(new FPCompWrapper(name, x * 1000, y * 1000, width * 1000, height * 1000));
  }

  // Let's find out if the baseline x,y positions are at 0, 0.
  // If not, renormalize all the locations.
  double minX = std::numeric_limits<double>::max();
  double minY = std::numeric_limits<double>::max();
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    minX           = std::min(minX, comp->getX());
    minY           = std::min(minY, comp->getY());
  }
  if (minX != 0.0 || minY != 0) {
    for (int i = 0; i < getComponentCount(); i++) {
      FPObject* comp = getComponent(i);
      comp->setLocation(comp->getX() - minX, comp->getY() - minY);
    }
  }

  // Perform scaling if needed.
  // NOTE.  This needs to happen after location normalization to avoid wierd offset effects.
  if (scalingFactor != 1.0) {
    morph(scalingFactor, scalingFactor);
  }

  // Now that the inferiors are renormalized, we can calculate the container size.
  recalcSize();
}

bool fixedLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;
  // Compare targetAR to our originalAR to calculate x and y scaling factors.
  double currentAR = width / height;
  double xFactor   = sqrt(targetAR / abs(currentAR));
  double yFactor   = 1.0 / xFactor;
  morph(xFactor, yFactor);
  width  = xFactor * width;
  height = yFactor * height;
  return true;
}