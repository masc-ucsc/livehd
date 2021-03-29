#pragma once

#include <optional>
#include <utility>  // for std::pair
#include <vector>

#include "FPContainer.hpp"
#include "floorplan.hpp"
#include "node_tree.hpp"

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

  // returns bottom left module
  bnode get_root() const { return nodes[0]; }
  void  set_root(const bnode bn) {
    I(nodes.size() == 0);
    nodes.push_back(bn);
  }

  absl::flat_hash_map<FPObject*, float> cont; // TODO: make this private eventually

private:
  std::vector<bnode> nodes;
};

// a layout class implementing Simulated Annealing
// https://scholars.lib.ntu.edu.tw/bitstream/123456789/147652/1/1467.pdf

class annLayout : public FPContainer {
protected:
  bstar hbt;  // horizontal B*-tree
  bstar wbt;  // vertical B*-tree
public:
  annLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};
