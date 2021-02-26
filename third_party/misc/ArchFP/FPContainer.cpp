#include "FPContainer.hpp"

#include <stdexcept>

#include "FPCompWrapper.hpp"
#include "FPObject.hpp"
#include "helpers.hpp"

// Default constructor
FPContainer::FPContainer(unsigned int rsize) : items(), yMirror(false), xMirror(false) {
  items.reserve(rsize);  // avoid many memory reallocations due to vector resizing, if possible
}

FPContainer::~FPContainer() {
  // It's very important to only delete items when their refcount hits zero.
  // cout << "deleting container.\n";
  for (int i = 0; i < (int)items.size(); i++) {
    FPObject* item     = items[i];
    int       newCount = item->decRefCount();
    if (newCount == 0)
      delete item;
  }
}

// To properly handle refCount, we will only allow one method to actually add (or remove) items from the item list.

void FPContainer::addComponentAtIndex(FPObject* comp, int index) {
  if (index < 0 || index > (int)items.size())
    throw std::invalid_argument("Attempt to add item to Container at illegal index.");

  const int oldsize = items.size();
  items.resize(items.size() + 1);

  // See if we need to move things to open space.
  if (index < oldsize && oldsize > 0) {
    for (int i = oldsize; i > index; i--) {
      items[i] = items[i - 1];
    }
  }

  items[index] = comp;
  comp->incRefCount();
}

FPObject* FPContainer::removeComponentAtIndex(int index) {
  if (index < 0 || index >= (int)items.size())
    throw std::invalid_argument("Attempt to add item to Container at illegal index.");
  FPObject* comp = items[index];
  // Now fill in the hole.
  for (int i = index; i < (int)items.size() - 1; i++) {
    items[i] = items[i + 1];
  }

  items.resize(items.size() - 1);

  // Often this component is about to be added somewhere else.
  // So, if the refCount is now zero, don't handle it here.
  // If the caller doesn't put it somewhere else, they will have to delete it themselves.
  comp->decRefCount();
  return comp;
}

void FPContainer::replaceComponent(FPObject* comp, int index) {
  removeComponentAtIndex(index);
  addComponentAtIndex(comp, index);
}

void FPContainer::addComponent(FPObject* comp) { addComponentAtIndex(comp, items.size()); }

void FPContainer::addComponentToFront(FPObject* comp) { addComponentAtIndex(comp, 0); }

void FPContainer::addComponent(FPObject* comp, int countArg) {
  comp->setCount(countArg);
  addComponent(comp);
}

FPObject* FPContainer::addComponentCluster(Ntype_op type, int count, double area, double maxAspectRatio, double minAspectRatio) {
  // First generate a dummy component with the correct information.
  dummyComponent* comp = new dummyComponent(type);
  // We first need to create a wrapper for the component.
  FPCompWrapper* wrapComp = new FPCompWrapper(comp, minAspectRatio, maxAspectRatio, area, count);
  addComponent(wrapComp);
  return wrapComp;
}

FPObject* FPContainer::addComponentCluster(std::string name, int count, double area, double maxAspectRatio, double minAspectRatio) {
  // First generate a dummy component with the correct information.
  dummyComponent* comp = new dummyComponent(name);
  // We first need to create a wrapper for the component.
  FPCompWrapper* wrapComp = new FPCompWrapper(comp, minAspectRatio, maxAspectRatio, area, count);
  addComponent(wrapComp);
  return wrapComp;
}

FPObject* FPContainer::removeComponent(int index) { return removeComponentAtIndex(index); }

// We need to sort items by total area.
// There are so few items, just use bubble sort.
// We want a descending sort.
// We know this won't change item counts, so just it have at the item list.
void FPContainer::sortByArea() {
  int len = items.size();
  for (int i = 0; i < len; i++) {
    double maxArea      = -1;
    int    maxAreaIndex = 0;
    for (int j = i; j < len; j++) {
      double newArea = items[j]->getArea() * items[j]->getCount();
      if (newArea > maxArea) {
        maxArea      = newArea;
        maxAreaIndex = j;
      }
    }
    FPObject* temp      = items[i];
    items[i]            = items[maxAreaIndex];
    items[maxAreaIndex] = temp;
  }
}

double FPContainer::totalArea() {
  area          = 0;
  int itemCount = getComponentCount();
  for (int i = 0; i < itemCount; i++) {
    FPObject* item = getComponent(i);
    area += item->totalArea();
  }
  area *= count;
  return area;
}

unsigned int FPContainer::outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  pushMirrorContext(startX, startY);

  unsigned int total = 0;

  for (int i = 0; i < getComponentCount(); i++) {
    FPObject* obj = getComponent(i);
    total += obj->findNode(tree, tidx, x + startX, y + startY);
  }

  popMirrorContext();

  return total;
}

void FPContainer::pushMirrorContext(double startX, double startY) {
  if (xMirror) {
    if (xMirrorDepth == maxMirrorDepth)
      throw std::out_of_range("X Mirror Depth exceeds maximum allowed.");
    xReflect             = !xReflect;
    xLeft[xMirrorDepth]  = startX + x;
    xRight[xMirrorDepth] = startX + x + width;
    xMirrorDepth += 1;
  }
  if (yMirror) {
    if (xMirrorDepth == maxMirrorDepth)
      throw std::out_of_range("Y Mirror Depth exceeds maximum allowed.");
    yReflect              = !yReflect;
    yBottom[yMirrorDepth] = startY + y;
    yTop[yMirrorDepth]    = startY + y + height;
    yMirrorDepth += 1;
  }
}

void FPContainer::popMirrorContext() {
  if (xMirror) {
    xReflect = !xReflect;
    xMirrorDepth -= 1;
  }
  if (yMirror) {
    yReflect = !yReflect;
    yMirrorDepth -= 1;
  }
}