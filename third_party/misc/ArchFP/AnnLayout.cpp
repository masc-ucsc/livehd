#include "AnnLayout.hpp"

#include <cmath>
#include <functional>

#include "BagLayout.hpp"
#include "GridLayout.hpp"
#include "mathutil.hpp"

annLayout::annLayout(unsigned int rsize) : FPContainer(rsize), horiz(rsize), vert(rsize) {
  type = Ntype_op::Invalid;
  name = "Ann";
}

void annLayout::addComponent(FPObject* comp, int count) {
  comp->setType(Ntype_op::Sub);
  FPContainer::addComponent(comp, count);
}

bool annLayout::insert_obj(size_t idx) {
  

  return true;
}

bool annLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;

  // NOTE: paper says AR is h / w, but ArchFP says AR is w / h
  float area      = totalArea();
  float maxHeight = sqrt(area / abs(targetAR));
  float maxWidth  = area / maxHeight;

  if (getComponentCount() < 2) {
    return true;
  }

  // run super simple grid optimization pass to collapse repeated nodes into grids if possible
  int gcd = getComponent(0)->getCount();
  for (size_t i = 1; i < getComponentCount(); i++) {
    gcd = GCD(gcd, getComponent(i)->getCount());
  }

  // if all child objects have a gcd of > 1, then each object can be split into two sub objects.
  // this means we can make copies of the bagLayout and put it into a grid to save work.

  bagLayout* bag = new bagLayout(getComponentCount());
  for (size_t i = 0; i < getComponentCount(); i++) {
    FPObject* item = getComponent(i);
    bag->addComponent(item, item->getCount() / gcd);
  }

  FPContainer* initLayout = bag;
  if (gcd > 1) {
    gridLayout* grid = new gridLayout(1);
    grid->addComponent(bag, gcd);
    initLayout = grid;
  }

  // floorplan everything in a horizontal row
  // (very likely not legal in terms of this module's AR, but easy to create B* from)
  float currX = 0.0f;
  for (size_t i = 0; i < initLayout->getComponentCount(); i++) {
    FPObject* comp = initLayout->getComponent(i);

    bool success = comp->layout(opt, (comp->getMaxAR() - comp->getMinAR()) / 2.0f);
    assert(success);

    comp->setX(currX);
    comp->setY(0);

    currX += comp->getWidth();

    assert(comp->valid());

    horiz.nodes.emplace_back(*comp);
    size_t last_idx = horiz.nodes.size() - 1;
    if (i > 0) {
      horiz.nodes[last_idx - 1].left_idx = last_idx;
    }

    if (comp->getX() == 0 && comp->getY() == 0) {
      fmt::print("root object: {}\n", comp->getName());
      horiz.root_idx = i;
    }
  }

  vert.cont.emplace_back(getComponent(getComponentCount() - 1));

  // TODO: perform SA algorithm

  return true;
}

void annLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  pushMirrorContext(startX, startY);
  std::string layoutName = getUniqueName();
  o << "# " << layoutName << " stats: X=" << calcX(startX) << ", Y=" << calcY(startY) << ", W=" << width << ", H=" << height
    << ", area=" << area << "mm²\n";
  o << "# start " << layoutName << " " << Ntype::get_name(getType()) << " ann " << getComponentCount() << "\n";
  for (size_t i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    obj->outputHotSpotLayout(o, x + startX, y + startY);
  }
  o << "# end " << layoutName << "\n";
  popMirrorContext();
}
