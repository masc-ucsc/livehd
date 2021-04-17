#include "BagLayout.hpp"

#include <iostream>

#include "FPObject.hpp"
#include "GridLayout.hpp"
#include "cell.hpp"

bagLayout::bagLayout(unsigned int rsize) : FPContainer(rsize) {
  type   = Ntype_op::Invalid;
  name   = "Bag";
  locked = false;
}

bool bagLayout::layout(FPOptimization opt, double targetAR) {
  // If we are locked, don't layout.
  if (locked)
    return true;

  // Calculate our area, and the implied target width and height.
  area             = totalArea();
  double remHeight = sqrt(area / abs(targetAR));
  double remWidth  = area / remHeight;
  double nextX     = 0;
  double nextY     = 0;

  if (verbose) {
    std::cout << "In BagLayout for " << getName() << ", Area=" << area << " Width= " << remWidth << " Height=" << remHeight
              << " Target AR=" << targetAR << "\n";
  }

  // Sort the components, placing them in decreasing order of size.
  sortByArea();
  int  itemCount = getComponentCount();
  bool correct   = true;
  for (int i = 0; i < itemCount; i++) {
    FPObject* comp     = getComponent(i);
    double    compArea = comp->totalArea();

    // Take the largest component, place it in the min dimension.
    // Except for the last component which gets places in the max dimension.
    // First calculate the AR, and the rest fill follow.
    double AR
        = (remWidth > remHeight || i == itemCount - 1) ? (compArea / remHeight) / remHeight : remWidth / (compArea / remWidth);
    if (comp->getCount() > 1) {
      // We have more than one component of the same type, so let the grid layout do the work.
      gridLayout* GL = new gridLayout(1);
      GL->addComponent(comp);
      replaceComponent(GL, i);
      comp = GL;
    }
    correct = correct && comp->layout(opt, AR);
    // Now we have the final component, we can set the location.
    comp->setLocation(nextX, nextY);
    // Prepare for the next round.
    if (remWidth > remHeight || i == itemCount - 1) {
      double compWidth = comp->getWidth();
      remWidth -= compWidth;
      nextX += compWidth;
    } else {
      double compHeight = comp->getHeight();
      remHeight -= compHeight;
      nextY += compHeight;
    }

    if (verbose)
      std::cout << " remWidth=" << remWidth << " remHeight=" << remHeight << " AR=" << AR << "\n";
  }

  recalcSize();

  if (verbose) {
    std::cout << "Total Container Width=" << width << " Height=" << height << " Area=" << width * height << " AR=" << width / height
              << "\n";
  }

  return correct;
}

void bagLayout::recalcSize() {
  // Go through the layout components and find the containers width and height.
  width  = 0;
  height = 0;
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* comp = getComponent(i);
    width          = std::max(width, comp->getX() + comp->getWidth());
    height         = std::max(height, comp->getY() + comp->getHeight());
  }
  area = width * height;
}

void bagLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  pushMirrorContext(startX, startY);
  int         itemCount = getComponentCount();
  std::string groupName;
  if (itemCount != 1) {
    groupName = getUniqueName();
    o << "# " << groupName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
      << ", area=" << area << "mm²\n";
    o << "# start " << groupName << " " << Ntype::get_name(getType()) << " bag " << itemCount << "\n";
  }
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    obj->outputHotSpotLayout(o, x + startX, y + startY);
  }
  if (itemCount != 1)
    o << "# end " << groupName << "\n";
  popMirrorContext();
}
