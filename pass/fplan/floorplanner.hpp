//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <memory>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "floorplan.hpp"
#include "lgraph.hpp"
#include "node_tree.hpp"
#include "node_type_area.hpp"

class Lhd_floorplanner {
public:
  Lhd_floorplanner(Node_tree&& nt_arg);

  // load modules into ArchFP using verious kinds of traversals
  virtual void load() = 0;

  // create a floorplan from loaded modules
  void create();

  // dump floorplan to file
  void write_file(const std::string_view filename);

  // write the floorplan back to LiveHD for analysis and future floorplans
  void write_lhd();

  ~Lhd_floorplanner();

protected:
  /*
    NOTE: raw pointers, new, and delete are used because ArchFP has a built-in reference counting system, so
    all that is required in order to free all floorplanner memory is to free the top-level layout.  Freeing every layout
    will result in double-frees.

    A fix for this would be to replace the refcounting implementation with std::shared_ptr.
  */

  // using std::array for fixed max size
  constexpr static std::array<GeographyHint, 5> hint_seq = {
      GeographyHint::Center,
      GeographyHint::Top,
      GeographyHint::Bottom,
      GeographyHint::Left,
      GeographyHint::Right
  };

  // these hints are only valid for exactly two nodes
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

  // information for layout of root node, used frequently
  LGraph* root_lg;

  // hierarchy of node instances
  Node_tree nt;

  // node area map
  const Ntype_area na;

  // layout of all child nodes
  absl::flat_hash_map<Tree_index, geogLayout*> layouts;

  // at what number of nodes of a given type should they be laid out in a grid?
  absl::flat_hash_map<Ntype_op, unsigned int> grid_thresh;

  // print debug information
  constexpr static bool debug_print = false;
};
