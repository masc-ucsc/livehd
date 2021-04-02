#include "AnnLayout.hpp"

#include <cmath>
#include <functional>
#include <random>

annLayout::annLayout(unsigned int rsize) : FPContainer(rsize), horiz(rsize), vert(rsize) {
  type = Ntype_op::Invalid;
  name = "Ann";
}

void annLayout::addComponent(FPObject* comp, int count) {
  comp->setType(Ntype_op::Sub);
  FPContainer::addComponent(comp, count);
}

float annLayout::getSide(float area, float ar) { return sqrt(area / abs(ar)); }

bool annLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;

  // TODO: handle leaves with count > 1!

  // NOTE: paper says AR is h / w, but ArchFP says AR is w / h
  float area      = totalArea();
  float maxHeight = sqrt(area / abs(targetAR));
  float maxWidth  = area / maxHeight;

  if (getComponentCount() == 0) {
    return true;
  }

  std::default_random_engine gen;

  FPObject*                             root = getComponent(0);
  std::uniform_real_distribution<float> rootAR(root->getMinAR(), root->getMaxAR());

  horiz.setRoot(*root);

  // TODO: build vert tree!

  vert.setRoot(*root);

  // construct horizontal B*-tree with a random (but legal) floorplan
  std::function<void(FPContainer*)> add_object = [&](FPContainer* obj) {
    for (size_t i = 0; i < obj->getComponentCount(); i++) {
      FPObject* c = obj->getComponent(i);

      if (c->getType() == Ntype_op::Sub || c->getType() == Ntype_op::Invalid) {
        add_object((FPContainer*)c);
      }

      std::uniform_real_distribution<float> randAR(c->getMinAR(), c->getMaxAR());
      std::uniform_int_distribution<size_t> randUpRight(0, 1);

      bool repeat = false;
      do {
        // start with random (but legal) AR and adjust if required
        float childWidth  = getSide(c->totalArea(), randAR(gen));
        float childHeight = 1 / childWidth;

        // NOTE:

        // if we randomly alternate between left and right node allocations,
        // then we have to rebuild the contour on every insertion.

        // need a better approach.

      } while (repeat);
    }
  };

  add_object(this);

  return false;
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
