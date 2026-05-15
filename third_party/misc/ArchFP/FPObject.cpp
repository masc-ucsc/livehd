#include "FPObject.hpp"

#include "ann_place.hpp"
#include "cell.hpp"
#include "helpers.hpp"
#include "lgraph.hpp"

FPObject::FPObject()
    : refCount(0)
    , x(0.0)
    , y(0.0)
    , width(0.0)
    , height(0.0)
    , area(0.0)
    , type(Ntype_op::Invalid)
    , name(Ntype::get_name(Ntype_op::Invalid))
    , hint(UnknownHint)
    , count(1) {}

void FPObject::setSize(double widthArg, double heightArg) {
  width  = widthArg;
  height = heightArg;
  area   = width * height;
}

void FPObject::setLocation(double xArg, double yArg) {
  x = xArg;
  y = yArg;
}

void FPObject::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  o << getUniqueName() << "\t" << getWidth() / 1000 << "\t" << getHeight() / 1000 << "\t" << calcX(startX) / 1000 << "\t"
    << calcY(startY) / 1000 << "\n";
}

std::string FPObject::getUniqueName() const {
  if (name == " " || name == "") {
    return name;
  }

  return absl::StrCat(name, std::to_string(Name2Count(name)));
}

double FPObject::calcX(double startX) const {
  if (xReflect) {
    return xLeft[xMirrorDepth - 1] - (startX + x - xRight[xMirrorDepth - 1] + width);
  } else {
    return startX + x;
  }
}

double FPObject::calcY(double startY) const {
  if (yReflect) {
    return yTop[yMirrorDepth - 1] - (startY + y - yBottom[yMirrorDepth - 1] + height);
  } else {
    return startY + y;
  }
}
