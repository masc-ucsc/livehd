//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <memory>
#include <string_view>

#include "AnnLayout.hpp"
#include "GeogLayout.hpp"
#include "absl/container/flat_hash_map.h"
#include "lgraph.hpp"
#include "mmap_map.hpp"
#include "node_tree.hpp"
#include "node_type_area.hpp"

class Lhd_floorplanner {
public:
  Lhd_floorplanner(Node_tree&& nt_arg);

  // load modules into ArchFP using verious kinds of traversals
  virtual void load() = 0;

  // create a floorplan from loaded modules
  void create(FPOptimization opt, float ar);

  // dump floorplan to file
  void write_file(const std::string_view filename);

  // write a node level floorplan back to LiveHD (must match livehd hierarchy exactly)
  void write_lhd_node();

  // write a module level floorplan back to LiveHD (nodes ignored)
  // TODO: write this
  void write_lhd_lg();

  ~Lhd_floorplanner();

protected:
  /*
    NOTE: raw pointers, new, and delete are used because ArchFP has a built-in reference counting system, so
    all that is required in order to free all floorplanner memory is to free the top-level layout.  Freeing every layout
    will result in double-frees.

    A fix for this would be to replace the refcounting implementation with std::shared_ptr.
  */

  // using std::array for fixed max size
  constexpr static std::array<GeographyHint, 5> hint_seq
      = {GeographyHint::Center, GeographyHint::Top, GeographyHint::Bottom, GeographyHint::Left, GeographyHint::Right};

  // these hints are only valid for node counts divisible by 2
  constexpr static std::array<GeographyHint, 6> hint_seq_2 = {
      GeographyHint::LeftRight,
      GeographyHint::LeftRightMirror,
      GeographyHint::LeftRight180,
      GeographyHint::TopBottom,
      GeographyHint::TopBottomMirror,
      GeographyHint::TopBottom180,
  };

  // return a hint based on the number of components
  GeographyHint randomHint(int count) const;

  // create a node with the proper type (geog or ann layout)
  FPContainer* makeNode(const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, const Tree_index tidx, size_t size);

  void addSub(FPContainer* c, const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, const Node::Compact& child_c, FPObject* comp,
              int count);

  void addLeaf(FPContainer* c, const mmap_lib::map<Node::Compact, GeographyHint>& hint_map, const Node::Compact& child_c, Ntype_op type,
               int count, double area, double maxARArg, double minARArg);

  // information for layout of root node, used frequently
  Lgraph* root_lg;

  // hierarchy of node instances
  Node_tree nt;

  // node area map
  const Ntype_area na;

  // layout of all child nodes
  FPContainer* root_layout;

  // at what number of nodes of a given type should they be laid out in a grid?
  absl::flat_hash_map<Ntype_op, unsigned int> grid_thresh;

  // print debug information
  constexpr static bool debug_print = false;
};
