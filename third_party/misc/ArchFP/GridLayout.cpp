#include "mathutil.hpp"

#include "GridLayout.hpp"

gridLayout::gridLayout(unsigned int rsize) : FPContainer(rsize) {
  type = Ntype_op::Invalid;
  name = "Grid";
}

bool gridLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;
  // We assume that a grid is a repeating unit of a single object.
  // However, that object can be either a leaf component or a container.
  if (getComponentCount() != 1) {
    std::cerr << "Attempt to layout a grid with other than one component.\n";
    return false;
  }
  double tarea   = totalArea();
  double theight = sqrt(tarea / abs(targetAR));
  double twidth  = tarea / theight;
  if (verbose)
    std::cout << "Begin Grid Layout for " << getName() << ", TargetAR=" << targetAR << " My area=" << tarea << " Implied W=" << twidth
         << " H=" << theight << "\n";

  FPObject* obj   = getComponent(0);
  int       total = obj->getCount();

  // Calculate the grid size closest to the target AR.
  xCount = balanceFactors(total, targetAR);
  yCount = total / xCount;
  // We want the gridRatio to express the actual width to height.
  double gridRatio = ((double)xCount) / yCount;
  // Now see what we want as the component ratio.
  double ratio = abs(targetAR) / gridRatio;

  if (verbose) {
    std::cout << "In Grid Layout, xCount=" << xCount << " yCount=" << yCount << "\n";
    std::cout << "In Grid Layout, Asking my inferior for AR=" << ratio << "\n";
  }

  // Now layout whatever is below us.
  // We need to set the count to 1 to avoid double counting area.
  obj->setCount(1);
  bool correct = obj->layout(opt, ratio);
  assert(obj->valid());
  obj->setCount(total);
  double compWidth  = obj->getWidth();
  double compHeight = obj->getHeight();

  // Now we can set our own width and height.
  width  = compWidth * xCount;
  height = compHeight * yCount;
  area   = width * height;
  if (verbose)
    std::cout << "At End Grid Layout, TargetAR=" << targetAR << " actualAR=" << width / height << "\n";

  return correct;
}

void gridLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  if (getComponentCount() != 1) {
    std::cerr << "Attempt to output a grid with other than one component.\n";
    return;
  }

  double    compWidth, compHeight;
  std::string    compName;
  FPObject* obj = getComponent(0);
  compWidth     = obj->getWidth();
  compHeight    = obj->getHeight();
  compName      = obj->getName();

  int    compCount = xCount * yCount;
  std::string GridName  = getUniqueName();
  o << "# " << GridName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
    << ", area=" << area << "mm²\n";
  o << "# start " << GridName << " " << Ntype::get_name(getType()) << " grid " << compCount << " " << xCount << " " << yCount
    << "\n";
  int compNum = 1;
  for (int i = 0; i < yCount; i++) {
    double cy = (i * compHeight) + y + startY;
    for (int j = 0; j < xCount; j++) {
      double cx = (j * compWidth) + x + startX;
      obj->outputHotSpotLayout(o, cx, cy);
      compNum += 1;
    }
  }
  o << "# end " << GridName << "\n";
}

unsigned int gridLayout::outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  if (getComponentCount() != 1) {
    throw std::invalid_argument("Attempt to output a grid with other than one component.\n");
  }

  double    compWidth, compHeight;
  std::string    compName;
  FPObject* obj = getComponent(0);
  compWidth     = obj->getWidth();
  compHeight    = obj->getHeight();
  compName      = obj->getName();

  int compNum = 1;

  unsigned int total = 0;

  for (int i = 0; i < yCount; i++) {
    double cy = (i * compHeight) + y + startY;
    for (int j = 0; j < xCount; j++) {
      double cx = (j * compWidth) + x + startX;

      total += obj->findNode(tree, tidx, cx, cy);

      compNum += 1;
    }
  }

  return total;
}