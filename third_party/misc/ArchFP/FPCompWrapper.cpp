#include "FPCompWrapper.hpp"

// Methods for the dummy component class to help with some IO.
std::ostream& operator<<(std::ostream& s, dummyComponent& c) {
  s << "Component: " << c.getName() << " is of type: " << Ntype::get_name(c.getType()) << "\n";
  return s;
}

dummyComponent::dummyComponent(Ntype_op typeArg) {
  type = typeArg;
  name = std::string(Ntype::get_name(type));
}

dummyComponent::dummyComponent(std::string nameArg) {
  type = Ntype_op::Sub;
  name = nameArg;
  for (int i = 1; i < static_cast<int>(Ntype_op::Last_invalid); i++) {
    if (nameArg == Ntype::get_name(static_cast<Ntype_op>(i))) {
      type = static_cast<Ntype_op>(i);
      break;
    }
  }
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

FPCompWrapper::~FPCompWrapper() {
  delete component;
}

double FPCompWrapper::ARInRange(double AR) {
  double maxAR = getMaxAR();
  if ((AR < 1 && maxAR > 1) || (AR > 1 && maxAR < 1))
    flip();
  maxAR         = getMaxAR();
  double minAR  = getMinAR();
  double retval = AR;
  if (maxAR < 1) {
    assert(false);  // should never happen?
    // retval = std::min(retval, minAR);
    // retval = std::max(retval, maxAR);
  } else {
    retval = std::max(retval, minAR);
    retval = std::min(retval, maxAR);
  }
  if (verbose)
    std::cout << "Target AR=" << AR << " minAR=" << minAR << " maxAR=" << maxAR << " returnAR=" << retval << "\n";

  if (AR < minAR || AR > maxAR) {
    std::cerr << "WARNING: requested AR " << AR << " is outside legal range (" << minAR << ", " << maxAR << "), using " << retval << ".\n";
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
  ratio = ARInRange(ratio);
  // Calculate width height from area and AR.
  // We have w/h = ratio => w = ratio*h.
  // and w*h = area => ratio*h*h = area => h^2 = area/ratio => h = sqrt(area/ratio)
  height = sqrt(area / ratio);
  width  = area / height;
  return true;
}