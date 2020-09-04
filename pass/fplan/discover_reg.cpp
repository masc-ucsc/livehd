#include <map>
#include <set>

#include "hier_tree.hpp"

set_t Hier_tree::find_most_freq_pattern(set_t subgraph, const size_t bwidth) const {
  set_vec_t                     vp;
  std::vector<Lg_type_id::type> initlabel;

  // remove verts with duplicate labels
  for (auto v : subgraph) {
    bool found = false;
    for (size_t i = 0; i < vp.size(); i++) {
      if (initlabel[i] == ginfo.labels(v)) {
        found = true;
      }
    }
    if (!found) {
      vp.push_back(ginfo.al.vert_set());
      vp[vp.size() - 1].insert(v);

      initlabel.push_back(ginfo.labels(v));
    }
  }

  // get value (# inst of P in G * size(G) + size(P))
  auto find_value = [&](const set_t pattern) -> unsigned int {
    set_t        global_found_nodes = ginfo.al.vert_set();
    unsigned int count              = 0;

    typedef std::unordered_set<Lg_type_id::type> generic_set_t;  // doesn't need to store unique elements
    generic_set_t                                gpat;

    for (auto v : pattern) {
      gpat.insert(ginfo.labels(v));
    }

    for (auto pv : subgraph) {
      set_t found_pattern = ginfo.al.vert_set();

      std::function<void(generic_set_t search_pattern, vertex_t v)> check_pattern = [&](generic_set_t search_pattern, vertex_t v) {
        if (search_pattern.find(ginfo.labels(v)) != search_pattern.end() && !found_pattern.contains(v)
            && !global_found_nodes.contains(v) && subgraph.contains(v)) {
          // checked here:
          // 1. label of node is something that exists in our set
          // 2. we haven't already found it this iteration
          // 3. we haven't already found it some other iteration
          // 4. node is in our subgraph
          found_pattern.insert(v);

          for (auto e : ginfo.al.out_edges(v)) {
            check_pattern(search_pattern, ginfo.al.head(e));
          }
        }
      };

      check_pattern(gpat, pv);
      if (found_pattern.size() == pattern.size()) {
        count++;
        for (auto v : found_pattern) {
          global_found_nodes.insert(v);
        }
      }
    }

    unsigned int value = count * subgraph.size() + pattern.size();

    if (reg_verbose) {
      fmt::print("value ({}) = count ({}) * G({}) + P({})\n", value, count, subgraph.size(), pattern.size());
    }
    return value;
  };

  // copy set by value since std::move doesn't work and '=' isn't defined
  auto copy_set = [](set_t& dst, set_t& src) {
    dst.clear();
    for (auto v : src) {
      dst.insert(v);
    }
  };

  set_t best_pat = vp[0];
  int   best_val = find_value(best_pat);

  while (vp.size() > 0) {
    std::vector<int> vval(vp.size(), -1);  // hacky map of set_t -> int
    // using two vectors because std::map and absl::flat_hash_map fails when passed a set_t...

    set_vec_t new_vp;
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

    for (unsigned int i = 0; i < new_vp.size(); i++) {
      if (reg_verbose) {
        fmt::print("pattern {}:\n", i);
      }

      auto         mval = vval[i];
      unsigned int value;
      if (mval != -1) {
        value = mval;
        if (reg_verbose) {
          fmt::print("  repeat.\n");
        }
      } else {
        value   = find_value(new_vp[i]);
        vval[i] = value;
        for (auto v : new_vp[i]) {
          if (reg_verbose) {
            fmt::print("  {}\n", ginfo.debug_names[v]);
          }
        }
      }
    }

    std::vector<std::pair<size_t, int>> sortvec;
    for (size_t i = 0; i < new_vp.size(); i++) {
      sortvec.emplace_back(i, vval[i]);
    }

    auto cmp = [](auto a, auto b) -> bool { return a.second < b.second; };

    std::sort(sortvec.begin(), sortvec.end(), cmp);

    vp.clear();

    sortvec.resize(std::min(bwidth, sortvec.size()));

    for (size_t i = 0; i < sortvec.size(); i++) {
      vp.push_back(ginfo.al.vert_set());
      copy_set(vp[i], new_vp[sortvec[i].first]);
    }

    if (sortvec.size() > 0 && sortvec[0].second > best_val) {
      best_val = sortvec[0].second;
      copy_set(best_pat, new_vp[sortvec[0].first]);
    }

    // TODO: currently not checking if the same set is created multiple times in the new_vp.
    // might want to do this later to avoid excessive operations on useless graphs...
  }

  return best_pat;
}

// TODO: write this
void Hier_tree::compress_hier(std::vector<set_t>& plist) { plist.clear(); }

void Hier_tree::discover_regularity(size_t hier_index) {
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

    if (reg_verbose) {
      fmt::print("\ndepth: {}, nodes:\n", curr_min_depth);
      unsigned int total = 0;
      for (auto v : level_children) {
        fmt::print("  node: {:<15}\n", ginfo.debug_names[v]);
        total++;
      }
      fmt::print("  total: {}\n", total);
    }

    do {
      set_t most_freq_pattern = find_most_freq_pattern(level_children, 100);
      pattern_list.push_back(most_freq_pattern);
      compress_hier(pattern_list);
    } while (false);  // no repeating patterns in subgraph

    curr_min_depth--;
  }
}
