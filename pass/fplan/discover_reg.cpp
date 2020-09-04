#include "hier_tree.hpp"

// TODO: clean up typedefs!
typedef std::unordered_set<vertex_t> hash_set_t;
typedef std::vector<hash_set_t>      hset_vec_t;

// TODO: using hash_set_t instead of set_t is almost pointless since I can't make hash tables out of hash_set_t.
// sets are move-constructible, so there's no point in using hash_set_t.

// given a pattern, find all other patterns in a subgraph
// returned set vec includes the pattern given
hset_vec_t Hier_tree::find_other_patterns(const hash_set_t& subgraph, const hash_set_t& pattern) {
  hash_set_t global_found_nodes;

  hset_vec_t found_patterns;
  found_patterns.push_back(pattern);

  generic_set_t gpat;

  for (auto v : pattern) {
    gpat.first.insert(ginfo.labels(v));
  }

  gpat.second = pattern.size();

  return find_all_patterns(subgraph, gpat);
}

// given a generic pattern, find all instantiations of that pattern in a subgraph
hset_vec_t Hier_tree::find_all_patterns(const hash_set_t& subgraph, const generic_set_t& gpattern) {
  hash_set_t global_found_nodes;

  hset_vec_t found_patterns;

  for (auto pv : subgraph) {
    hash_set_t found_pattern;

    std::function<void(generic_set_t search_pattern, vertex_t v)> check_pattern = [&](generic_set_t search_pattern, vertex_t v) {
      if (search_pattern.first.find(ginfo.labels(v)) != search_pattern.first.end() && found_pattern.find(v) == found_pattern.end()
          && global_found_nodes.find(v) == global_found_nodes.end() && subgraph.find(v) != subgraph.end()) {
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

    check_pattern(gpattern, pv);
    if (found_pattern.size() == gpattern.second) {
      found_patterns.push_back(found_pattern);
      for (auto v : found_pattern) {
        global_found_nodes.insert(v);
      }
    }
  }

  return found_patterns;
}

// get value (# inst of P in G * size(G) + size(P))
// duplicated from above because we can get a speed increase by only tracking the count of found patterns, not the instantiations
// themselves
unsigned int Hier_tree::find_value(const hash_set_t& subgraph, const hash_set_t& pattern) {
  hash_set_t global_found_nodes;

  generic_set_t gpattern;

  unsigned int count = 0;

  for (auto v : pattern) {
    gpattern.first.insert(ginfo.labels(v));
  }

  for (auto pv : subgraph) {
    hash_set_t found_pattern;

    std::function<void(generic_set_t search_pattern, vertex_t v)> check_pattern = [&](generic_set_t search_pattern, vertex_t v) {
      if (search_pattern.first.find(ginfo.labels(v)) != search_pattern.first.end() && found_pattern.find(v) == found_pattern.end()
          && global_found_nodes.find(v) == global_found_nodes.end() && subgraph.find(v) != subgraph.end()) {
        found_pattern.insert(v);

        for (auto e : ginfo.al.out_edges(v)) {
          check_pattern(search_pattern, ginfo.al.head(e));
        }
      }
    };

    check_pattern(gpattern, pv);
    if (found_pattern.size() == pattern.size()) {
      count++;
      for (auto v : found_pattern) {
        global_found_nodes.insert(v);
      }
    }
  }

  unsigned int value = count * subgraph.size() + pattern.size();

  if (reg_verbose) {
    fmt::print("  value ({}) = count ({}) * G({}) + P({})\n", value, count, subgraph.size(), pattern.size());
  }

  return count;
}

hash_set_t Hier_tree::find_most_freq_pattern(hash_set_t subgraph, const size_t bwidth) {
  hset_vec_t                    vp;
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
      vp.emplace_back();
      vp[vp.size() - 1].insert(v);

      initlabel.push_back(ginfo.labels(v));
    }
  }

  if (reg_verbose) {
    fmt::print("initial pattern:\n");
  }

  hash_set_t best_pat = vp[0];
  int        best_val = find_value(subgraph, best_pat);

  auto best_pair = std::pair(best_pat, best_val);

  auto cmp_set = [&](const hash_set_t& a, const hash_set_t& b) -> bool {
    if (a.size() != b.size()) {
      return false;
    }

    for (auto v : a) {
      if (b.find(v) == b.end()) {
        return false;
      }
    }

    return true;
  };

  while (vp.size() > 0) {
    // hacky hash_set_t -> value map, since hash tables don't take hash_set_t since it doesn't have a default constructor.
    std::vector<int> memo_vec(subgraph.size(), -1);

    hset_vec_t new_vp;
    for (auto pat : vp) {
      for (auto v : pat) {
        for (auto e : ginfo.al.out_edges(v)) {
          auto ov = ginfo.al.head(e);
          if (pat.find(ov) == pat.end() && subgraph.find(ov) != subgraph.end()) {
            auto npat = pat;
            npat.insert(ov);

            // check if the set already exists before creating one
            bool exists = false;
            for (auto opat : new_vp) {
              if (cmp_set(npat, opat)) {
                exists = true;
                break;
              }
            }

            if (!exists) {
              new_vp.push_back(npat);
            }
          }
        }
      }
    }

    for (unsigned int i = 0; i < new_vp.size(); i++) {
      if (reg_verbose) {
        fmt::print("pattern {}:\n", i);
      }

      auto         mval = memo_vec[i];
      unsigned int value;
      if (mval != -1) {
        value = mval;
        if (reg_verbose) {
          fmt::print("  repeat.\n");
        }
      } else {
        value       = find_value(subgraph, new_vp[i]);
        memo_vec[i] = value;
        for (auto v : new_vp[i]) {
          if (reg_verbose) {
            fmt::print("  {}\n", ginfo.debug_names(v));
          }
        }
      }
    }

    // pair of [owner of value in new_vp, value]
    std::vector<std::pair<size_t, int>> sortvec;
    for (size_t i = 0; i < new_vp.size(); i++) {
      sortvec.emplace_back(i, memo_vec[i]);
    }

    auto cmp = [](auto a, auto b) -> bool { return a.second < b.second; };

    std::sort(sortvec.begin(), sortvec.end(), cmp);

    sortvec.resize(std::min(bwidth, sortvec.size()));

    vp.resize(sortvec.size());

    for (size_t i = 0; i < sortvec.size(); i++) {
      vp[i] = new_vp[sortvec[i].first];
    }

    if (sortvec.size() > 0 && sortvec[0].second > best_val) {
      best_val = sortvec[0].second;
      best_pat = std::move(new_vp[sortvec[0].first]);
    }

    //break;
  }

  return best_pat;
}

// void Hier_tree::compress_hier(generic_set_t& gpat) {}

void Hier_tree::discover_regularity(size_t hier_index) {
  auto hier = hiers[hier_index];
  I(hier != nullptr);

  hset_vec_t pattern_list;
  int        curr_min_depth = find_tree_depth(hier);

  // get all leaves with depth >= minlevel
  std::function<void(phier, unsigned int, unsigned int, hash_set_t&)> get_level_children
      = [this, &get_level_children](phier node, unsigned int level, unsigned int minlevel, hash_set_t& level_set) {
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
    hash_set_t level_children;
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
      hash_set_t most_freq_pattern = find_most_freq_pattern(level_children, 100);
      pattern_list.push_back(most_freq_pattern);
      // compress_hier(most_freq_pattern);
    } while (false);  // no repeating patterns in subgraph

    curr_min_depth--;
  }
}
