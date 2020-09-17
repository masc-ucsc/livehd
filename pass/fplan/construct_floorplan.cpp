#include <iostream>
#include <random>

#include "fmt/core.h"
#include "hier_tree.hpp"

void Hier_tree::manual_select_points() {
  for (size_t i = 0; i < pattern_sets.size(); i++) {
    for (size_t j = 0; j < pattern_sets[i].size(); j++) {
      auto& dag = dags[i].pat_dag_map[pattern_sets[i][j]];

      I(dag->width != 0.0);
      I(dag->height != 0.0);
      double asp = dag->width / dag->height;

      fmt::print("floorplan {}, pattern {}: width {:.3f}, height {:.3f}, aspect ratio {:.3f}.\n",
                 i,
                 j,
                 dag->width,
                 dag->height,
                 asp);
      fmt::print("keep? (y/n/q) > ");
      char ans;
      std::cin >> ans;
      if (ans == 'y') {
        chosen_patterns.push_back({i, j});
        fmt::print("pattern chosen.\n");
      } else if (ans == 'n') {
        fmt::print("pattern skipped.\n");
      } else if (ans == 'q') {
        return;
      } else {
        fmt::print("unknown choice '{}'!", ans);
        j--;  // redo the same pattern
      }
    }
  }
}

void Hier_tree::auto_select_points() {
  static std::default_random_engine    gen;
  static std::uniform_int_distribution dist(0, 3);  // range is actually [0, 1]!
  for (size_t i = 0; i < pattern_sets.size(); i++) {
    for (size_t j = 0; j < pattern_sets[i].size(); j++) {
      /*
      if (dist(gen) == 0) {
        chosen_patterns.push_back({i, j});
      }
      */

      if (i == 0 && j == 0) {
        chosen_patterns.push_back({i, j});
      }
      if (i == 2 && j == 0) {
        chosen_patterns.push_back({i, j});
      }
    }
  }
}

void Hier_tree::construct_floorplans() {
  bool automatic = true;
  if (automatic) {
    auto_select_points();
    fmt::print("selected patterns ");
    for (auto pati : chosen_patterns) {
      fmt::print("({}, {}) ", pati.pattern_set_id, pati.pattern_id);
    }
    fmt::print("\n");
  } else {
    manual_select_points();
  }

  // actually construct floorplans here
}