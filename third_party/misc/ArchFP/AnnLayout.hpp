#pragma once

#include <optional>
#include <utility>  // for std::pair
#include <vector>

#include "FPContainer.hpp"
#include "floorplan.hpp"
#include "node_tree.hpp"

// a layout class implementing Simulated Annealing
// https://scholars.lib.ntu.edu.tw/bitstream/123456789/147652/1/1467.pdf

class annLayout : public FPContainer {
protected:
  template <class K, class V>
  class bstar {
  public:
    class bnode {
      size_t left_idx;
      size_t right_idx;

      FPObject& obj;

      bnode(FPObject& _obj) : obj(_obj) {}
    };

    // construct a B* tree from the children of a specified index
    bstar(const Tree_index idx);

    // returns bottom left module
    bnode root() const { return nodes[0]; }

  private:
    std::vector<bnode> nodes;
  };

public:
  annLayout(unsigned int rsize);

  bool layout(FPOptimization opt, double targetAR = 1.0);
  void outputHotSpotLayout(std::ostream& o, double startX = 0.0, double startY = 0.0);
};
