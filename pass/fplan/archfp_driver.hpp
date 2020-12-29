#pragma once

#include <memory>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "floorplan.hpp"
#include "lgraph.hpp"

class floorplanner {
public:

  virtual void load_lg(LGraph* root, const std::string_view lgdb_path) = 0;

  // create a floorplan and dump to file
  void create_floorplan(const std::string_view filename);

  // TODO: write floorplan back to lgraph subnodes
  void store_floorplan();

  ~floorplanner() {
    std::cout << "deleting floorplanner..." << std::endl;
    for (auto& pair : attrs) {
      geogLayout* l = pair.second.l.release();

      // TODO: actually deleting geogLayouts segfaults for some reason...
      // delete l;
      // pair.second.l.reset() also fails...
    }
  }

protected:
  LGraph* root_lg;

  struct Attr {
    unsigned int count = 0;
    std::unique_ptr<geogLayout>  l;
  };

  absl::flat_hash_map<LGraph*, Attr> attrs;

  unsigned int get_area(LGraph* lg);

  void load_prep_lg(LGraph* root, const std::string_view lgdb_path);

  constexpr static bool debug_print = true;
};
