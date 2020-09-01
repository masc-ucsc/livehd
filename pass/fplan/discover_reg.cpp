#include <map>
#include <set>

#include "hier_tree.hpp"

set_t Hier_tree::find_most_freq_pattern(set_t subgraph, const size_t bwidth) const {
  // TODO: make sure patterns with repeated lgraph ids are accounted for properly

  set_vec_t vp;  // vector of patterns

  // remove verts with duplicate labels
  for (auto v : subgraph) {
    bool found = false;
    for (auto pat : vp) {
      if (pat.contains(v)) {
        found = true;
      }
    }
    if (!found) {
      vp.push_back(ginfo.al.vert_set());
      vp[vp.size() - 1].insert(v);
    }
  }

  set_t best = vp[0];

  std::map<std::set<Lg_type_id>, unsigned int> memo_map;

  while (vp.size() > 0) {
    std::vector<set_t> new_vp;
    for (auto pat : vp) {
      for (auto v : pat) {
        for (auto e : ginfo.al.out_edges(v)) {
          auto ov = ginfo.al.head(e);
          if (!pat.contains(ov) && subgraph.contains(ov)) {
            auto npat = pat;
            npat.insert(ov);
            new_vp.push_back(npat);
          }
        }
      }
    }

    // get value (# inst of P in G * size(G) + size(P))
    auto find_value = [&](const std::set<Lg_type_id> search_pattern) -> unsigned int {
      set_t        global_found_nodes = ginfo.al.vert_set();
      unsigned int count              = 0;

      for (auto pv : subgraph) {
        set_t found_pattern = ginfo.al.vert_set();

        std::function<void(std::set<Lg_type_id> search_pattern, vertex_t v)> check_pattern
            = [&](std::set<Lg_type_id> search_pattern, vertex_t v) {
                if (search_pattern.find(ginfo.labels(v)) != search_pattern.end() && !found_pattern.contains(v)
                    && !global_found_nodes.contains(v) && subgraph.contains(v)) {
                  found_pattern.insert(v);

                  for (auto e : ginfo.al.out_edges(v)) {
                    check_pattern(search_pattern, ginfo.al.head(e));
                  }
                }
              };

        check_pattern(search_pattern, pv);
        if (found_pattern.size() == search_pattern.size()) {
          count++;
          for (auto v : found_pattern) {
            global_found_nodes.insert(v);
          }
        }
      }

      unsigned int value = count * subgraph.size() + search_pattern.size();

      fmt::print("value ({}) = count ({}) * G({}) + P({})\n", value, count, subgraph.size(), search_pattern.size());
      return value;
    };

    for (unsigned int i = 0; i < new_vp.size(); i++) {
      fmt::print("pattern {}:\n", i);
      std::set<Lg_type_id> spat;
      for (auto v : new_vp[i]) {
        spat.insert(ginfo.labels(v));
      }

      auto         mval = memo_map.find(spat);
      unsigned int value;
      if (mval != memo_map.end()) {
        value = mval->second;
      } else {
        value          = find_value(spat);
        memo_map[spat] = value;
      }

      for (auto v : new_vp[i]) {
        fmt::print("  {}\n", ginfo.debug_names[v]);
      }
    }

    // TODO: sort using find_value and get first bwidth elems, replace vp, track pbest

    // TODO: currently not checking if the same set is created multiple times in the new_vp.
    // might want to do this later to avoid excessive operations on useless graphs...

    break;
  }

  return ginfo.al.vert_set();
}

void Hier_tree::compress_hier(std::vector<set_t>& plist) { plist.clear(); }

void Hier_tree::discover_regularity(size_t hier_index) {
  std::cout << "  discovering regularity...";

  auto hier = hiers[hier_index];
  I(hier != nullptr);

  std::vector<set_t> pattern_list;
  int                curr_min_depth = find_tree_depth(hier);

  // get all leaves with depth >= minlevel
  std::function<void(phier, unsigned int, unsigned int, set_t&)> get_level_children
      = [this, &get_level_children](phier node, unsigned int level, unsigned int minlevel, set_t& level_set) {
          if (node->is_leaf()) {
            if (level >= minlevel) {
              for (auto v : ginfo.sets[node->graph_subset]) {
                level_set.insert(v);
              }
            }
            return;
          }

          get_level_children(node->children[0], level + 1, minlevel, level_set);
          get_level_children(node->children[1], level + 1, minlevel, level_set);
        };

  while (curr_min_depth >= 0) {
    set_t level_children = ginfo.al.vert_set();
    get_level_children(hier, 0, curr_min_depth, level_children);

#ifdef FPLAN_DBG_VERBOSE
    fmt::print("\ndepth: {}, nodes:\n", curr_min_depth);
    unsigned int total = 0;
    for (auto v : level_children) {
      fmt::print("  node: {:<15}\n", ginfo.debug_names[v]);
      total++;
    }
    fmt::print("  total: {}\n", total);
#endif

    do {
      set_t most_freq_pattern = find_most_freq_pattern(level_children, 100);
      pattern_list.push_back(most_freq_pattern);
      compress_hier(pattern_list);
    } while (false);  // no repeating patterns in subgraph

    curr_min_depth--;
  }

  std::cout << "done." << std::endl;
}
