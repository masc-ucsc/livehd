#pragma once

#include <optional>
#include <utility>  // for std::pair
#include <vector>

#include "FPContainer.hpp"
#include "floorplan.hpp"

// B*-tree class for storing floorplan adjacency information
// NOTE: although multiple papers refer to this data structure as a B*-tree,
// it is not the same as a B-tree with >2 children (which is also referred to as a B*-tree).
class bstar {
public:
  class bnode {
  public:
    int left_idx;
    int right_idx;

    FPObject& obj;

    bnode(FPObject& _obj) : left_idx(-1), right_idx(-1), obj(_obj) {}
  };

  bstar()                   = delete;  // avoid due to large number of memory allocations
  bstar(const bstar& other) = delete;  // avoid, very slow
  bstar(size_t rsize) : nodes(), cont() { nodes.reserve(rsize); }

  int root_idx = -1; // bottom left element in floorplan

  // first element is root
  std::vector<bnode> nodes;
  std::vector<FPObject*> cont; // would use ref, but need to add and remove elements directly (ref_wrapper?)

private:
};

// a layout class implementing Simulated Annealing
// https://scholars.lib.ntu.edu.tw/bitstream/123456789/147652/1/1467.pdf

class annLayout : public FPContainer {
private:
  // contours used as doubly linked lists
  bstar horiz;  // horizontal B*-tree
  bstar vert;   // vertical B*-tree

  // basic insert/delete methods used by perturbation methods
  bool insert_obj(size_t idx); // insert a module into the B* layout, returns false if insertion is impossible
  void delete_obj(size_t idx); // delete a module

  // perturbation methods used by SA algorithm, returns false if operation is impossible
  bool rotate(FPObject* obj); // rotate a module 90 degrees
  bool move(FPObject* obj); // move a module somewhere else
  bool swap(FPObject* obj1, FPObject* obj2); // swap two modules
  bool moveExt(FPObject* obj); // move a module to the outside of the floorplan

public:
  annLayout(unsigned int rsize);

  virtual void addComponent(FPObject* comp, int count);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};
