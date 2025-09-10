#include "FPCompWrapper.hpp"

// Methods for the dummy component class to help with some IO.
std::ostream& operator<<(std::ostream& s, dummyComponent& c) {
  s << "Component: " << c.getName() << " is of type: " << Ntype::get_name(c.getType()) << "\n";
  return s;
}

dummyComponent::dummyComponent(Ntype_op typeArg) {
  type = typeArg;
  name = Ntype::get_name(type);
}

dummyComponent::dummyComponent(const std::string& nameArg) {
  type = Ntype::get_op(nameArg);
  if (type == Ntype_op::Invalid) {
    type = Ntype_op::Sub;
  }
  name = nameArg;
}

void dummyComponent::myPrint() { std::cout << "Component: " << getName() << " is of type: " << Ntype::get_name(getType()) << "\n"; }

FPCompWrapper::FPCompWrapper(dummyComponent* comp, double minAR, double maxAR, double areaArg, int countArg) {
  component      = comp;
  minAspectRatio = minAR;
  maxAspectRatio = maxAR;
  area           = areaArg;
  count          = countArg;
  name           = comp->getName();
  type           = comp->getType();
}

FPCompWrapper::FPCompWrapper(std::string nameArg, double xArg, double yArg, double widthArg, double heightArg) {
  dummyComponent* DC = new dummyComponent(nameArg);
  component          = DC;
  setLocation(xArg, yArg);
  setSize(widthArg, heightArg);
  minAspectRatio = width / height;
  maxAspectRatio = width / height;
  count          = 1;
  name           = DC->getName();
  type           = DC->getType();
}

FPCompWrapper::~FPCompWrapper() { delete component; }

double FPCompWrapper::ARInRange(double AR) {
  double maxAR = getMaxAR();
  if ((AR < 1 && maxAR > 1) || (AR > 1 && maxAR < 1)) {
    flip();
  }
  maxAR         = getMaxAR();
  double minAR  = getMinAR();
  double retval = AR;

  if (maxAR < 1) {
    double temp = maxAR;
    maxAR       = minAR;
    minAR       = temp;
  }

  retval = std::max(retval, minAR);
  retval = std::min(retval, maxAR);

  if (verbose) {
    std::cout << "Target AR=" << AR << " minAR=" << minAR << " maxAR=" << maxAR << " returnAR=" << retval << "\n";
  }

  return retval;
}

void FPCompWrapper::flip() {
  double temp    = width;
  width          = height;
  height         = temp;
  minAspectRatio = 1 / minAspectRatio;
  maxAspectRatio = 1 / maxAspectRatio;

  if (minAspectRatio > maxAspectRatio) {
    double temp    = minAspectRatio;
    minAspectRatio = maxAspectRatio;
    maxAspectRatio = temp;
  }
}

bool FPCompWrapper::layout(FPOptimization opt, double ratio) {
  (void)opt;
  // Make sure the ratio is within the stated constraints.
  double oldratio = ratio;
  ratio           = ARInRange(ratio);
  // Calculate width height from area and AR.
  // We have w/h = ratio => w = ratio*h.
  // and w*h = area => ratio*h*h = area => h^2 = area/ratio => h = sqrt(area/ratio)
  height = sqrt(area / ratio);
  width  = area / height;

  if (oldratio < minAspectRatio || oldratio > maxAspectRatio) {
    std::cerr << "WARNING: requested AR " << oldratio << " is outside legal range (" << minAspectRatio << ", " << maxAspectRatio
              << "), using " << ratio << ".\n";
  }

  return oldratio == ratio;
}
