#pragma once

#include <memory>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "floorplan.hpp"
#include "lgraph.hpp"

class Lhd_floorplanner {
public:

  Lhd_floorplanner() : root_layout(std::make_unique<geogLayout>()) {}

  // load modules into ArchFP using verious kinds of traversals
  virtual void load(LGraph* root, const std::string_view lgdb_path) = 0;

  // create a floorplan from loaded modules
  void create();

  // dump floorplan to file
  void write_file(const std::string_view filename);

  // write the floorplan back to LiveHD for analysis and future floorplans
  void write_lhd();

  virtual ~Lhd_floorplanner() {
    for (auto& pair : layouts) {
      geogLayout* l = pair.second.release();
      (void)l;

      // TODO: actually deleting geogLayouts segfaults for some reason...
      // delete l;
      // (pair.second.l.reset() also fails)
    }

    geogLayout* l = root_layout.release();
    (void)l;
  }

protected:
  
  // layout of root node, used frequently
  LGraph* root_lg;
  std::unique_ptr<geogLayout> root_layout;

  // layout of all child nodes
  absl::flat_hash_map<LGraph*, std::unique_ptr<geogLayout>> layouts;

  constexpr static bool debug_print = false;
};
