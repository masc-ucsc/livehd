//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "GeogLayout.hpp"
#include "absl/container/flat_hash_map.h"
#include "floorplanner.hpp"
#include "lgedgeiter.hpp"
#include "node_pin.hpp"
#include "AnnLayout.hpp"

class Node_hier_floorp : public Lhd_floorplanner {
public:
  Node_hier_floorp(Node_tree&& nt_arg);
  void load();

private:
  // load all the nodes in a given lgraph into an ArchFP geogLayout instance and return that instance
  FPContainer* load_lg_nodes(const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, Lgraph* lg, const Tree_index tidx);
};