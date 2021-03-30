#pragma once

#include "FPObject.hpp"
#include "core/node_tree.hpp"
#include "floorplan.hpp"

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
  virtual bool layout(FPOptimization opt, double targetAR = 1.0)                              = 0;
  virtual void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0) = 0;

  // Ways to add components.
  virtual FPObject* addComponentCluster(Ntype_op type, int count, double area, double maxARArg, double minARArg);
  virtual FPObject* addComponentCluster(std::string name, int count, double area, double maxARArg, double minARArg);
  virtual void      addComponent(FPObject* comp);
  virtual void      addComponent(FPObject* comp, int count);

  // Writes current container and all subcontainers to the specified root lgraph
  virtual unsigned int outputLgraphLayout(Node_tree& tree, Tree_index tidx, double startX = 0.0, double startY = 0.0);
};