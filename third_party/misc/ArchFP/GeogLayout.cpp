#include "GeogLayout.hpp"

#include "BagLayout.hpp"
#include "GridLayout.hpp"
#include "mathutil.hpp"

geogLayout::geogLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "Geog";
}

void geogLayout::addComponent(FPObject* comp, int count, GeographyHint hint) {
  comp->setHint(hint);
  comp->setType(Ntype_op::Sub);
  FPContainer::addComponent(comp, count);
}

FPObject* geogLayout::addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg,
                                          GeographyHint hint) {
  FPObject* comp = FPContainer::addComponentCluster(type, count, area, maxARArg, minARArg);
  comp->setHint(hint);
  return comp;
}

FPObject* geogLayout::addComponentCluster(std::string name, int count, double area, double maxARArg, double minARArg,
                                          GeographyHint hint) {
  FPObject* comp = FPContainer::addComponentCluster(name, count, area, maxARArg, minARArg);
  comp->setHint(hint);
  return comp;
}

bool geogLayout::hardARLayoutHelper(double remWidth, double remHeight, double curX, double curY,
                              FPObject** layoutStack, int curDepth, FPObject** centerItems, int centerItemsCount) {
  int itemCount = getComponentCount();
  // Check for the end of the recursion.
  if (itemCount == 0 && centerItemsCount == 0)
    return true;

  // Check if it time to layout the center items.
  // For now we will just stick them in a bag layout, and then use all remaining area to lay it out.
  if (itemCount == 0 && centerItemsCount > 0) {
    // First find out if there are repeating units that need a grid to layout.
    // We will do this by finding the GCD of the counts in the collection of components.
    int gcd = centerItems[0]->getCount();
    for (int i = 1; i < centerItemsCount; i++) gcd = GCD(gcd, centerItems[i]->getCount());
    bagLayout* BL = new bagLayout(centerItemsCount);
    for (int i = 0; i < centerItemsCount; i++) {
      FPObject* item = centerItems[i];
      BL->addComponent(item, item->getCount() / gcd);
    }

    if (verbose)
      std::cout << "Laying out Center items.  Count=" << centerItemsCount << " GCD=" << gcd << "\n";

    // Use all remaining area.
    double       targetAR = remWidth / remHeight;
    FPContainer* FPLayout = BL;
    if (gcd > 1) {
      // Put the bag container into a grid container and lay it out.
      gridLayout* GL = new gridLayout(1);
      GL->addComponent(BL, gcd);
      FPLayout = GL;
    }
    // Not sure if we need to set the hint, but when I missed this for the grid below, it was a bug.
    FPLayout->setHint(Center);
    bool correct = FPLayout->layout(HardAspectRatio, targetAR);
    assert(FPLayout->valid());
    if (verbose)
      std::cout << "Laying out Center item(s).  Current x and y are(" << curX << "," << curY << ")\n";
    FPLayout->setLocation(curX, curY);
    // Put the layout on the stack.
    // DO we really need to do this????
    layoutStack[curDepth] = FPLayout;
    return correct;
  }

  // Start by pealing off the first componet cluster.
  // And getting the standard information about it.
  FPObject*     comp     = removeComponent(0);
  GeographyHint compHint = comp->getHint();

  // In this case, we will just save them up for now, and then lay them out last.
  if (compHint == Center) {
    centerItems[centerItemsCount] = comp;
    centerItemsCount += 1;
    hardARLayoutHelper(remWidth, remHeight, curX, curY, layoutStack, curDepth, centerItems, centerItemsCount);
  }

  if (compHint == LeftRight || compHint == TopBottom || compHint == LeftRightMirror || compHint == TopBottomMirror
      || compHint == LeftRight180 || compHint == TopBottom180) {
    // Check if the count is a multiple of two, and if half will fit on a side.
    // If not, just put it in one side.
    // If so, split the component into two, and handle the opposite sides separately.
    // In order to handle the split in a general way (with nested layouts)
    //    we will have to put the component into a bag for each side.
    // TODO Put some quick calc here to see if we can fit with half the items
    // If not, don't split.
    if (comp->getCount() % 2 == 0) {
      int newCount = comp->getCount() / 2;
      comp->setCount(newCount);
      bagLayout* BL1 = new bagLayout(1);
      bagLayout* BL2 = new bagLayout(1);
      BL1->addComponent(comp);
      BL2->addComponent(comp);
      if (compHint == LeftRight || compHint == LeftRightMirror || compHint == LeftRight180) {
        BL1->setHint(Left);
        BL2->setHint(Right);
        if (compHint == LeftRightMirror)
          BL2->xMirror = true;
      } else if (compHint == TopBottom || compHint == TopBottomMirror || compHint == TopBottom180) {
        BL1->setHint(Top);
        BL2->setHint(Bottom);
        if (compHint == TopBottomMirror)
          BL2->yMirror = true;
      }
      if (compHint == TopBottom180 || compHint == LeftRight180) {
        BL2->xMirror = true;
        BL2->yMirror = true;
      }
      comp = BL1;
      addComponentToFront(BL2);
    } else if (compHint == LeftRight || compHint == LeftRightMirror || compHint == LeftRight180)
      comp->setHint(Left);
    else
      comp->setHint(Top);
    compHint = comp->getHint();
  }

  bool correct = false;

  // Now handle left, right, top, bottom.
  double newX, newY;
  if (compHint == Left || compHint == Top || compHint == Right || compHint == Bottom) {
    // Stuff the comp into a grid, and see if we can layout it out in the desired shape.
    // TODO TODO This assumes a component has an area.  For a container, it will not have an area until it gets layed out.

    double totalArea = comp->totalArea();
    assert(totalArea > 0);

    if (verbose)
      std::cout << "In geog for " << comp->getName() << ", total component area=" << totalArea << "\n";
    
    // TODO: adding components in this way assumes everything will fit and lays out components with insane aspect ratios.
    double targetWidth, targetHeight;
    newX = curX;
    newY = curY;
    if (compHint == Left || compHint == Right) {
      targetHeight = remHeight;
      targetWidth  = totalArea / targetHeight;
    } else  // if (compHint == Top || compHint == Bottom)
    {
      targetWidth  = remWidth;
      targetHeight = totalArea / targetWidth;
    }

    if (compHint == Left)
      newX += targetWidth;
    if (compHint == Right)
      curX = curX + (remWidth - targetWidth);
    if (compHint == Top)
      curY = curY + (remHeight - targetHeight);
    if (compHint == Bottom)
      newY += targetHeight;

    double    targetAR = targetWidth / targetHeight;
    FPObject* FPLayout = comp;
    if (comp->getCount() > 1) {
      gridLayout* grid = new gridLayout(1);
      grid->addComponent(comp);
      grid->setHint(compHint);
      FPLayout = grid;
    }
    correct = FPLayout->layout(HardAspectRatio, targetAR);
    assert(FPLayout->valid());
    FPLayout->setLocation(curX, curY);
    // Put the layout on the stack.
    layoutStack[curDepth] = FPLayout;
    if (compHint == Left || compHint == Right)
      remWidth -= FPLayout->getWidth();
    else if (compHint == Top || compHint == Bottom)
      remHeight -= FPLayout->getHeight();
    hardARLayoutHelper(remWidth, remHeight, newX, newY, layoutStack, curDepth + 1, centerItems, centerItemsCount);
  } else if (compHint != Center) {
    std::cerr << "Hint is not any of the recognized hints.  Hint=" << compHint << "\n";
    std::cerr << "Component is of type " << Ntype::get_name(comp->getType()) << "\n";
  }

  return correct;
}

bool geogLayout::hardARLayout(double targetAR) {
  // All this routine does is set up some data structures,
  //   and then call the helper to recursively do the layout work.
  // We will keep track of containers we make in a "stack".
  // We will also need to keep track of center items so they can be layed out last.

  // Assume no item will be more than split in two.
  int        itemCount    = getComponentCount();
  int        maxArraySize = itemCount * 2;
  FPObject** layoutStack  = new FPObject*[maxArraySize];
  for (int i = 0; i < maxArraySize; i++) layoutStack[i] = 0;
  FPObject** centerItems = new FPObject*[itemCount];
  for (int i = 0; i < itemCount; i++) centerItems[i] = 0;

  // Calculate the total area, and the implied target width and height.
  area             = totalArea();
  double remHeight = sqrt(area / abs(targetAR));
  double remWidth  = area / remHeight;

  if (verbose)
    std::cout << "In geogLayout for " << getName() << ".  A=" << area << " W=" << remWidth << " H=" << remHeight << "\n";

  // Now do the real work.
  bool correct = hardARLayoutHelper(remWidth, remHeight, 0, 0, layoutStack, 0, centerItems, 0);

  // By now, the item list should be empty.
  if (getComponentCount() != 0) {
    std::cerr << "Non empty item list after recursive layout in geographic layout.\n";
    std::cerr << "Remaining Component count=" << getComponentCount() << "\n";
    for (int i = 0; i < getComponentCount(); i++)
      std::cerr << "Component " << i << " is of type " << getComponent(i)->getName() << "\n";
    throw std::runtime_error("non-empty item list!");
  }

  // Now install the list of containers as our items.
  // Now go through the layout components and find the containers width and height.
  width  = 0;
  height = 0;
  for (int i = 0; i < maxArraySize; i++) {
    FPObject* comp = layoutStack[i];
    if (!comp)
      break;
    FPContainer::addComponent(comp);
    width  = std::max(width, comp->getX() + comp->getWidth());
    height = std::max(height, comp->getY() + comp->getHeight());
  }
  area = width * height;

  delete[] centerItems;
  delete[] layoutStack;

  return correct;
}

bool geogLayout::layout(const FPOptimization opt, const double targetAR) {
  switch (opt) {
    case HardAspectRatio: return hardARLayout(targetAR);
    case SliceTree: assert(false);
  }
}

void geogLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  pushMirrorContext(startX, startY);
  std::string layoutName = getUniqueName();
  o << "# " << layoutName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
    << ", area=" << area << "mm²\n";
  o << "# start " << layoutName << " " << Ntype::get_name(getType()) << " geog " << getComponentCount() << "\n";
  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    obj->outputHotSpotLayout(o, x + startX, y + startY);
  }
  o << "# end " << layoutName << "\n";
  popMirrorContext();
}