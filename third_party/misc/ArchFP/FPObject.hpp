#pragma once

#include "core/node_tree.hpp"
#include "floorplan.hpp"

// This is a wrapper class for MV5 components that wish to be included in a floorplan.
// If we wanted to use multiple inheritance, we chould use this as a mix-in.
// Or, this information could be included in the MV5 component base class.
// For now, we will use instead a wrapper class that points back to the component object.
// And will keep all the information needed to participate in a floor plan.
// For starters, the floorplan will ONLY need to operate on these wrappers.

// We will pull out the basic variables into a base class.
class FPObject {
  int refCount;

  // Make the common variables available to the inheriting classes.
protected:
  // The x,y values will be relative to the enclosing object.
  double        x;       // The location in the container.  0.0 is on the left.
  double        y;       // The location in the container.  0.0 is on the bottom.
  double        width;   // The width of the component in millimeters.
  double        height;  // The height of the component in millimeters.
  double        area;    // The area of the component in sq millimeters.
  Ntype_op      type;    // The type of the component.
  std::string   name;    // The name of the component.
  GeographyHint hint;    // Arguably, we should form a map of hints to components.
                         // But for now this is simpler.
  int count;             // The number of the these components in this group of components.
                         // This makes it easier to add, say, 8 identical cores as one object.

public:
  FPObject();
  virtual ~FPObject() {}

  // Needed to maintain the refcounts for proper memory management.
  // No one should call these except for the container add/remove component methods.
  int incRefCount() { return refCount += 1; }
  int decRefCount() { return refCount -= 1; }

  // Allow anyone to get the values of things.
  virtual double      getX() const { return x; }
  virtual double      getY() const { return y; }
  virtual double      getWidth() const { return width; }
  virtual double      getHeight() const { return height; }
  virtual double      getArea() const { return area; }
  virtual std::string getName() const { return name; }
  virtual Ntype_op    getType() const { return type; }
  virtual int         getCount() const { return count; }
  std::string         getUniqueName() const;

  virtual double totalArea() { return area * count; }

  // check if object has valid parameters (some overlap ok, so position is not checked)
  virtual bool valid() const { return width > 0.0 && height > 0.0 && area > 0.0; }

  // Calculate the output position taking starting offset and mirroring.
  double calcX(double startX) const;
  double calcY(double startY) const;

  // The default  behavior for ARs is to clamp to the actual width and height.
  virtual double        getMaxAR() const { return getWidth() / getHeight(); }
  virtual double        getMinAR() const { return getMaxAR(); }
  virtual GeographyHint getHint() const { return hint; }

  virtual void          setX(int newX) { x = newX; }
  virtual void          setY(int newY) { y = newY; }
  virtual int           setCount(int newCount) { return count = newCount; }
  virtual void          setArea(double newArea) { area = newArea; }
  virtual GeographyHint setHint(GeographyHint newHint) { return hint = newHint; }
  virtual void          setSize(double widthArg, double heightArg);
  virtual void          setLocation(double xArg, double yArg);
  virtual void          setName(std::string nameArg) { name = nameArg; }
  virtual void          setType(Ntype_op typeArg) { type = typeArg; }

  virtual bool         layout(FPOptimization opt, double targetAR = 1.0) = 0;
  virtual void         outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
  virtual unsigned int outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);

  // find a node in tree that can be mapped to this FPObject
  unsigned int findNode(Node_tree& tree, Tree_index tidx, double calcX, double calcY);
};