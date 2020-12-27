#pragma once

#include <memory>
#include <string_view>

#include "lgraph.hpp"

#include "floorplan.hpp"

class archfp_driver {
public:
  void load_hier_lg(LGraph* root, const std::string_view lgdb_path);
  void load_flat_lg(LGraph* root, const std::string_view lgdb_path);

  void create_floorplan(const std::string_view filename);

private:

  LGraph* root_lg;

  struct Attr {
    unsigned int count = 0;
    geogLayout* l = nullptr;
  };

  absl::flat_hash_map<LGraph*, Attr> attrs;

  void create_layout(LGraph* lg);
  void add_layout(LGraph* existing_lg, LGraph* lg);

  void load_prep(LGraph* root, const std::string_view lgdb_path);

  constexpr static bool debug_print = true;
};