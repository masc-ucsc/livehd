/* -*- Mode: C++ ; indent-tabs-mode: nil ; c-file-style: "stroustrup" -*-

   Rapid Prototyping Floorplanner Project
   Author: Greg Faust

   File:   Floorplan.hh     C++ Header file for floorplanner.

*/

#include <iostream>
#include <map>
#include <string>
using namespace std;

// This will be used to keep track of user's request for more output during layout.
extern bool verbose;

// This is an emum for the types of components that can be included in a floorplan.
// In particular, the various layout managers will want to find various components
//     they expect to be in their layout.
// We will use this enum as the key for a map of components neeeded in a layout manager.
// TODO  Undoubtedly there is already an enum or other type system for components in M5.
//       As part of integration, we will see if we can change to the standard,
//       or continue to maintain a simpler one for floorplanning.
#define ComponentTypeCount 18
enum ComponentType {
  UnknownType = 0,
  Core,
  Cache,
  ICache,
  DCache,
  L1,
  L2,
  L3,
  RF,
  FPRF,
  ALU,
  NoC,
  Control,
  CrossBar,
  MemController,
  Grid,
  Group,
  Cluster
};
extern string           TypeNames[];
extern map<string, int> NameCounts;
inline string           Type2Name(ComponentType compType) { return TypeNames[compType]; }
extern int              TypeCounts[];
inline int              Type2Count(ComponentType compType) { return ++TypeCounts[compType]; }
inline int              Name2Count(string arg) { return ++NameCounts[arg]; }
string                  getStringFromInt(int in);
void                    setNameMode(bool);

// Here is an enumeration for the optimazation goals for a layout manager.
// These are barely used as the moment, as everything lays it self out in the smallest area
//      that is closest to the desired aspect ratio.
enum FPOptimization { Area, AspectRatio };

// Here is the enumeration for layout hints for the geographic layout.
enum GeographyHint {
  UnknownGeography,
  Left,
  Right,
  Top,
  Bottom,
  Center,
  LeftRight,
  LeftRightMirror,
  LeftRight180,
  TopBottom,
  TopBottomMirror,
  TopBottom180,
  Periphery
};

// This class is meant to be a standin for a real component from M5 or whatever this eventually merge into.
class dummyComponent {
  ComponentType type;
  string        name;

public:
  dummyComponent(ComponentType typeArg);
  dummyComponent(string name);

  string        getName() { return name; }
  ComponentType getType() { return type; }
  void          myPrint();
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
  ComponentType type;    // The type of the component.
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
  virtual double        getX() { return x; }
  virtual double        getY() { return y; }
  virtual double        getWidth() { return width; }
  virtual double        getHeight() { return height; }
  virtual double        getArea() { return area; }
  virtual double        totalArea() { return area * count; }
  virtual string        getName() { return name; }
  virtual ComponentType getType() { return type; }
  virtual int           getCount() { return count; }
  string                getUniqueName() {
    if (name == " " || name == "")
      return name;
    else
      return name + getStringFromInt(Name2Count(name));
  }

  // Calculate the output position taking starting offset and mirroring.
  double calcX(double startX);
  double calcY(double startY);

  // The default  behavior for ARs is to clamp to the actual width and height.
  virtual double        getMaxAR() { return getWidth() / getHeight(); }
  virtual double        getMinAR() { return getMaxAR(); }
  virtual GeographyHint getHint() { return hint; }

  virtual int           setCount(int newCount) { return count = newCount; }
  virtual GeographyHint setHint(GeographyHint newHint) { return hint = newHint; }
  virtual void          setSize(double widthArg, double heightArg);
  virtual void          setLocation(double xArg, double yArg);

  virtual bool layout(FPOptimization opt, double targetAR = 1.0) = 0;
  virtual void outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
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

  double          getMinAR() { return minAspectRatio; }
  double          getMaxAR() { return maxAspectRatio; }
  dummyComponent* getComp() { return component; }

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
  int        itemCount;
  FPObject** items;
  void       addComponentAtIndex(FPObject* comp, int index);
  FPObject*  removeComponentAtIndex(int index);

protected:
  static int maxItemCount;

  // These allow safe access to the item list.
  int       getComponentCount() { return itemCount; }
  FPObject* getComponent(int index) { return items[index]; }
  FPObject* removeComponent(int index);
  void      replaceComponent(FPObject* comp, int index);
  void      addComponentToFront(FPObject* comp);
  void      sortByArea();

public:
  FPContainer();
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
  virtual FPObject* addComponentCluster(ComponentType type, int count, double area, double maxARArg, double minARArg);
  virtual FPObject* addComponentCluster(string name, int count, double area, double maxARArg, double minARArg);
  virtual void      addComponent(FPObject* comp);
  virtual void      addComponent(FPObject* comp, int count);
};

// This will just be a collection of components to lay out in the given aspect ratio.
// If any of the components have a count > 1, a grid will be created to put it in.
class bagLayout : public FPContainer {
protected:
  bool locked;
  void recalcSize();

public:
  bagLayout();

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
  gridLayout();

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);

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
  bool layoutHelper(double targetWidth, double targetHeight, double curX, double curY, FPObject** layoutStack, int curDepth,
                    FPObject** centerItems, int centerItemsCount);

public:
  geogLayout();

  virtual bool      layout(FPOptimization opt, double targetAR = 1.0);
  virtual void      outputHotSpotLayout(ostream& o, double startX = 0.0, double startY = 0.0);
  virtual FPObject* addComponentCluster(ComponentType type, int count, double area, double maxARArg, double minARArg,
                                        GeographyHint hint);
  virtual FPObject* addComponentCluster(string name, int count, double area, double maxARArg, double minARArg, GeographyHint hint);
  virtual void      addComponent(FPObject* comp, int count, GeographyHint hint);
};

// Output Helper Functions.
ostream& outputHotSpotHeader(const char* filename);
void     outputHotSpotFooter(ostream& o);
string   getStringFromInt(int in);
