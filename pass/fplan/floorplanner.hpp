#pragma once

#include <memory>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "floorplan.hpp"
#include "lgraph.hpp"

class floorplanner {
public:

  floorplanner() : root_layout(std::make_unique<geogLayout>()) {}

  // load a floorplan into ArchFP using verious kinds of traversals
  virtual void load_lg(LGraph* root, const std::string_view lgdb_path) = 0;

  // create a floorplan and dump to file
  void create_floorplan(const std::string_view filename);

  ~floorplanner() {
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
  std::unique_ptr<geogLayout> root_layout;

  absl::flat_hash_map<LGraph*, std::unique_ptr<geogLayout>> layouts;

  unsigned int get_area(LGraph* lg);

  constexpr static bool debug_print = false;
};
