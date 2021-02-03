//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "floorplanner.hpp"
#include "node_pin.hpp"
#include "lgedgeiter.hpp"

class Node_flat_floorp : public Lhd_floorplanner {
public:
  Node_flat_floorp(Node_tree&& nt_arg);
  void load();

private:
};