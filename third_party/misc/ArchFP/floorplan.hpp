#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "core/cell.hpp"
#include "node_tree.hpp"
using namespace std;

int    Name2Count(const string& arg);
void clearCount();
string getStringFromInt(int in);
void   setNameMode(bool);

// Here is an enumeration for the optimazation goals for a layout manager.
// Area: optimize for smallest area (not implemented)
// SoftAspectRatio: optimize for a given aspect ratio, but generate a legal floorplan (not implemented)
// HardAspectRatio: optimize for a given aspect ratio, possibly forming a floorplan with gaps / overlaps
enum FPOptimization { Area, SoftAspectRatio, HardAspectRatio };

// Here is the enumeration for layout hints for the geographic layout.
enum GeographyHint {
  Center,  // make center the default, since it's considered to be valid
  Left,
  Right,
  Top,
  Bottom,
  LeftRight,
  LeftRightMirror,
  LeftRight180,
  TopBottom,
  TopBottomMirror,
  TopBottom180,
  Periphery,         // not supported
  UnknownGeography,  // not supported
};

// This class is meant to be a standin for a real component from M5 or whatever this eventually merge into.
class dummyComponent {
  Ntype_op type;
  string   name;

public:
  dummyComponent(Ntype_op typeArg);
  dummyComponent(string name);

  string   getName() const { return name; }
  Ntype_op getType() const { return type; }
  void     myPrint();
};

extern ostream& operator<<(ostream& s, dummyComponent& c);

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
  string        name;    // The name of the component.
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
  virtual double   getX() const { return x; }
  virtual double   getY() const { return y; }
  virtual double   getWidth() const { return width; }
  virtual double   getHeight() const { return height; }
  virtual double   getArea() const { return area; }
  virtual double   totalArea() { return area * count; }
  virtual bool     valid() const { return x >= 0.0 && y >= 0.0 && width > 0.0 && height > 0.0 && area > 0.0; }
  virtual string   getName() const { return name; }
  virtual Ntype_op getType() const { return type; }
  virtual int      getCount() const { return count; }
  string           getUniqueName() const;

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
  virtual void          setName(string nameArg) { name = nameArg; }
  virtual void          setType(Ntype_op typeArg) { type = typeArg; }

  virtual bool         layout(FPOptimization opt, double targetAR = 1.0) = 0;
  virtual void         outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
  virtual unsigned int outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);

  // find a node in tree that can be mapped to this FPObject
  unsigned int findNode(Node_tree& tree, Tree_index tidx, double calcX, double calcY);
};

// This class is the lowest level in the component hierarchy.
// It is just the wrapper around an M5 Component.
class FPCompWrapper : public FPObject {
  // For now, we will use a silly standin for the M5 Component object that just has a name.
  dummyComponent* component;  // The pointer to the component of interest.

  // Aspect ratio constraints.
  // Some components are very precise about their aspect ratio.
  // This is very true for example for memory or buses!
  // Therefore, we will store both a minimum and maximum aspect ratio.
  // These ratios are the long side over the short side, so they should be >= 1.
  // A value of 1.0 for maximum ratio forces a square.
  // If min and max are the same, the component is forced to have that ration.
  double minAspectRatio;  // The minimum ratio of the long side to the short side.
  double maxAspectRatio;  // The maximum ratio of the long side to the short side.

  double ARInRange(double AR);
  void   flip();

public:
  FPCompWrapper(dummyComponent* comp, double minAR, double maxAR, double area, int count);
  FPCompWrapper(string nameArg, double xArg, double yArg, double widthArg, double heightArg);
  ~FPCompWrapper();

  double          getMinAR() const { return minAspectRatio; }
  double          getMaxAR() const { return maxAspectRatio; }
  dummyComponent* getComp() const { return component; }

  virtual bool layout(FPOptimization opt, double targetAR = 1.0);
};

// Should the container and the layout manager be two separate classes or one class?
// For some reason the GUI engines have them as separate things, that are independent.
// But here, we assume that all containers will have a layout manager.
// So, the obvious choice is to have the container as a base class,
// and the specific layouts as specializations.
// The container class will define the "interface" for a layout manager
// by defining abstract methods which are then implemented in the specific layout manager.
class FPContainer : public FPObject {
  // In order to avoid deep copies, we will allow an FPObject to appear
  //    in more than one container.
  // That means we need a way to know when to delete.
  // We will make the following members private so that even other
  //    containers will need to use the accessors.
  // In this way, we can maintain proper refcounts that can tell us
  //    when things can be deleted.
  std::vector<FPObject*> items;
  void                   addComponentAtIndex(FPObject* comp, int index);
  FPObject*              removeComponentAtIndex(int index);

protected:
  // These allow safe access to the item list.
  int       getComponentCount() const { return items.size(); }
  FPObject* getComponent(int index) const { return items[index]; }
  FPObject* removeComponent(int index);
  void      replaceComponent(FPObject* comp, int index);
  void      addComponentToFront(FPObject* comp);
  void      sortByArea();

public:
  FPContainer(unsigned int rsize = 0);
  ~FPContainer();

  // We often need the total area of a container before it is layout out.
  // This will cause a recurse on containers til we get to objects at the bottom.
  virtual double totalArea();
  // Push or pop context for Mirror printing.
  // TODO.  This shouldn't be public.
  bool yMirror;
  bool xMirror;
  void pushMirrorContext(double startX, double startY);
  void popMirrorContext();

  // Give the command for this container to lay itself out.
  // return a bool to indicate success or failure.
  virtual bool layout(FPOptimization opt, double targetAR = 1.0)                         = 0;
  virtual void outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0) = 0;

  // Ways to add components.
  virtual FPObject* addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg);
  virtual FPObject* addComponentCluster(string name, int count, double area, double maxARArg, double minARArg);
  virtual void      addComponent(FPObject* comp);
  virtual void      addComponent(FPObject* comp, int count);

  // Writes current container and all subcontainers to the specified root lgraph
  virtual unsigned int outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);
};

// This will just be a collection of components to lay out in the given aspect ratio.
// If any of the components have a count > 1, a grid will be created to put it in.
class bagLayout : public FPContainer {
protected:
  bool locked;
  void recalcSize();

public:
  bagLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
};

class fixedLayout : public bagLayout {
  void morph(double xFactor, double yFactor);

public:
  bool layout(FPOptimization opt, double targetAR = 1.0);
  fixedLayout(const char* filename, double scalingFactor = 1.0);
};

// Here is the simplest possible layout manager.
// It takes a collection of identical objects, and lays them out in a grid.
// It tries to get them into a rectangle as close to the specified aspect ratio.
// TODO Consider adding an aspect ratio layout algorithm that is more aggressive.
// Presumably this would see if the grid can be made closer to the desired AR
//    by adding blank components when needed.
class gridLayout : public FPContainer {
  // Store the calculated x and y counts of components in the grid.
  int xCount;
  int yCount;

public:
  gridLayout(unsigned int rsize);

  bool                 layout(FPOptimization opt, double targetAR = 1.0);
  void                 outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
  virtual unsigned int outputLGraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);

  // A grid handles its counts different than other components?
};

// This class is a layout manager that takes geographic hints about where to place clusters.
// Hopefully, it will act as the replacement for at least the simple NoC layout above.
// Perhaps we will need some other NoC layout at some point if we address XBAR type configurations.
// It will do its layout recursively.  This will be our answer to the top-down vs bottom up question.
// We will make optimistic assumptions on the way down, and clean up on the way up.
// To handle the recursion, we will use a recursive helper to the layout routine.

class geogLayout : public FPContainer {
  // FPObject** centerItems;    // During layout, we will store up the center items, stick them in a bag, and lay them out last.
  // int        centerItemsCount;
  bool layoutHelper(FPOptimization opt, double targetWidth, double targetHeight, double curX, double curY, FPObject** layoutStack,
                    int curDepth, FPObject** centerItems, int centerItemsCount);

  void checkHint(int count, GeographyHint hint) const;

public:
  geogLayout(unsigned int rsize);

  virtual bool      layout(FPOptimization opt, double targetAR = 1.0);
  virtual void      outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
  virtual FPObject* addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg,
                                        GeographyHint hint);
  virtual FPObject* addComponentCluster(string name, int count, double area, double maxARArg, double minARArg, GeographyHint hint);
  virtual void      addComponent(FPObject* comp, int count, GeographyHint hint);
};

// Output Helper Functions.
ostream& outputHotSpotHeader(const char* filename);
void     outputHotSpotFooter(ostream& o);
