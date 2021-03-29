#include "AnnLayout.hpp"

#include <cmath>
#include <functional>
#include <random>

annLayout::annLayout(unsigned int rsize) : FPContainer(rsize), hbt(rsize), wbt(rsize) {
  type = Ntype_op::Invalid;
  name = "Ann";
}

bool annLayout::layout(FPOptimization opt, double targetAR) {
  (void)opt;
  (void)targetAR;

  // NOTE: paper says AR is h / w, but ArchFP says AR is w / h
  float area  = totalArea();
  float max_h = sqrt(area / abs(targetAR));
  float max_w = area / max_h;

  if (getComponentCount() == 0) {
    return true;
  }

  FPObject* root = getComponent(0);
  hbt.set_root(*root);
  hbt.cont[root] = max_h;
  wbt.cont[root] = max_w;

  std::default_random_engine gen;

  // construct horizontal B*-tree with a random (but legal) placement of objects
  std::function<void(size_t)> add_object = [&](size_t obj) {
    for (size_t i = 0; i < getComponentCount(); i++) {
      FPObject* c = getComponent(i);

      std::uniform_real_distribution<float> dist(c->getMinAR(), c->getMaxAR());

      bool repeat = false;
      do {
        // start with random (but legal) AR and adjust if required
        float c_max_w = dist(gen);
        float c_max_h = totalArea() / c_max_w;

        // TODO: need contour here

      } while (repeat);
    }
  };

  add_object(0);

  return false;
}

void annLayout::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  (void)o;
  (void)startX;
  (void)startY;
  fmt::print("writing layout to file...\n");
}
