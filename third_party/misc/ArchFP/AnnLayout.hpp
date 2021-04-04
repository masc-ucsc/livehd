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
    size_t left_idx;
    size_t right_idx;

    FPObject& obj;

    bnode(FPObject& _obj) : obj(_obj) {}
  };

  bstar()                   = delete;  // avoid due to large number of memory allocations
  bstar(const bstar& other) = delete;  // avoid, very slow
  bstar(size_t rsize) : nodes() { nodes.reserve(rsize); }

  void  setRoot(const bnode& bn) {
    I(nodes.size() == 0); // makes no sense to call this if a tree already exists
    nodes.emplace_back(bn);
  }

  std::vector<bnode> nodes; // TODO: make this private eventually
  std::vector<bnode> cont;

private:
};

// a layout class implementing Simulated Annealing
// https://scholars.lib.ntu.edu.tw/bitstream/123456789/147652/1/1467.pdf

class annLayout : public FPContainer {
protected:
  bstar horiz;  // horizontal B*-tree
  bstar vert;  // vertical B*-tree
public:
  annLayout(unsigned int rsize);

  virtual void addComponent(FPObject* comp, int count);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};
