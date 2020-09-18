#include <iostream>
#include <list>
#include <random>
#include <set>
#include <sstream>

#include "fmt/core.h"
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"

/*
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
      //if (dist(gen) == 0) {
        //chosen_patterns.push_back({i, j});
      //}

      if (i == 0 && j == 0) {
        chosen_patterns.push_back({i, j});
      }
      if (i == 2 && j == 0) {
        chosen_patterns.push_back({i, j});
      }
    }
  }
}
*/

void Hier_tree::floorplan_point() {
  // TODO: replace this mess with some maps or something
  // auto& pat = pattern_sets[pid.pset][pid.p];
  // auto& layout = floorplan_sets[pid.pset][pid.p];
  // auto& dag    = dags[pid.pset].pat_dag_map[pat];

  // the tree we're using to generate floorplans is NOT A HIERARCHY TREE
  // 1. output earlier in the paper says "set of regular hierarchies"
  // 2. alg 5 mentions "for all children in T", not just for the two children like earlier in the paper
  //    This means the tree isn't binary like the hierarchy tree.
}

void Hier_tree::floorplan_dag_set(const std::list<Dag::pdag>& set, std::stringstream& outstr) {
  std::stringstream instr;
  unsigned int      node_counter = 0;

  I(set.size() > 0);

  instr << fmt::format("{}\n", set.size());

  for (auto pd : set) {
    I(pd->width != 0);
    I(pd->height != 0);
    // account for the fact that there may be more than one children of a given type
    node_counter++;
    // keep a few digits of precision to ourselves
    instr << fmt::format("{:.12f} {:.12f}\n", pd->width, pd->height);
  }

  if (floor_verbose) {
    fmt::print("\ninput string stream:\n{}", instr.str());
  }
  
  invoke_blobb(instr, outstr, node_counter > 8);
}

// generates a list of possible floorplans ranging from all leaf nodes (best wire connectivity)
// to all pattern nodes (best regularity)
void Hier_tree::generate_floorplans() {
  for (size_t i = 0; i < hiers.size(); i++) {
    auto& dag         = dags[i];
    auto& pattern_set = pattern_sets[i];

    // TODO: replace this with an unordered set
    std::list<Dag::pdag> subpatterns;

    auto push_children = [&](Dag::pdag pd) {
      for (size_t ci = 0; ci < pd->children.size(); ci++) {
        for (size_t count = 0; count < pd->child_edge_count[ci]; count++) {
          subpatterns.push_back(pd->children[ci]);
        }
      }
    };

    I(dag.root != nullptr);

    push_children(dag.root);

    std::stringstream outstr;

    bool has_patterns;
    do {
      // we're floorplanning a lot of nodes, so switching to hierarchical seems like a good idea
      // TODO: maybe also enumeration mode in BloBB? we aren't really going after optimal packings...
      floorplan_dag_set(subpatterns, outstr);

      has_patterns = false;

      for (auto it = subpatterns.begin(); it != subpatterns.end(); it++) {
        auto pd = *it;
        if (!pd->is_leaf()) {
          subpatterns.erase(it);

          push_children(pd);
          has_patterns = true;
          break;
        }
      }
    } while (has_patterns);

    // floorplan all the leaf nodes too
    floorplan_dag_set(subpatterns, outstr);

    /*
    fp_set.emplace_back();  // create a new floorplan
        double pat_width, pat_height;
        outstr >> pat_width;
        outstr >> pat_height;

        pd->width  = pat_width;
        pd->height = pat_height;

        size_t out_count;
        outstr >> out_count;

        I(out_count == in_count);

        auto& gi   = collapsed_gis[dag_id];
        auto  allg = gi.al.vert_set();
        for (auto v : gi.al.verts()) {
          allg.insert(v);
        }

        for (size_t i = 0; i < out_count; i++) {
          double child_width, child_height;
          outstr >> child_width;
          outstr >> child_height;
          fp_set[floorplan_i].emplace_back(child_width, child_height);
        }

        for (size_t i = 0; i < out_count; i++) {
          double child_xpos, child_ypos;
          outstr >> child_xpos;
          outstr >> child_ypos;
          fp_set[floorplan_i][i].xpos = child_xpos;
          fp_set[floorplan_i][i].ypos = child_ypos;
        }

    */
  }
}

void Hier_tree::construct_floorplans() {
  // HiReg states that we should select outlines and start constructing floorplans from those outlines.
  // It's going to be easier (and more popular) to just recursively floorplan the whole design at once.

  generate_floorplans();
}