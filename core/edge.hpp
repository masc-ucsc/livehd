//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node.hpp"
#include "node_pin.hpp"

class XEdge { // FIXME: s/XEdge/Edge/g
public:
  // TODO??: just fields in src, to avoid 2 times the LGraph pointer
  const Node_pin src;
  const Node_pin dst;

  XEdge(const Node_pin &src_, const Node_pin &dst_);

  void del_edge();
};
