//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "floorplanner.hpp"
#include "lgedgeiter.hpp"
#include "node_pin.hpp"

#include "absl/container/flat_hash_map.h"

class Node_hier_floorp : public Lhd_floorplanner {
public:
  void load(const Node_tree& tree, const std::string_view lgdb_path);

private:
  void load_lg_nodes(LGraph* lg, const std::string_view lgdb_path);

  // Coloring is supposed to be unique, except if there are multiple submodules of the same type within the scope of a module.  If
  // this is the case, they will be given the same color, since their layout can't really be determined that well from area
  // information alone.

  /*
  class Tag_info {
  public:
    long node_color;    // if node isn't organized into a grid, assign it a unique value
    long parent_color;  // if node is part of a grid, track both parent and node color

    Tag_info() : node_color(0), parent_color(0) {}
    Tag_info(long _node_color, long _parent_color) : node_color(_node_color), parent_color(_parent_color) {}

    friend bool operator==(const Tag_info& lhs, const Tag_info& rhs) {
      return (lhs.node_color == rhs.node_color) && (lhs.parent_color == rhs.parent_color);
    }

    // needed to make Tag_info hashable
    template <typename H>
    friend H AbslHashValue(H h, const Tag_info& ti) {
      return H::combine(std::move(h), ti.node_color, ti.parent_color);
    }
  };
  */

  // map used when sending values to ArchFP
  // absl::flat_hash_map<Node::Compact, Tag_info> send_map;

  // map used when receiving floorplans back from ArchFP
  // absl::flat_hash_map<Tag_info, Node::Compact> recv_map;

  //void color_nodes(LGraph* root, const std::string_view lgdb_path);
  // void color_nodes();
};