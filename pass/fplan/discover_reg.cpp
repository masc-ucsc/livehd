#include "hier_tree.hpp"

typedef std::unordered_set<Lg_type_id::type> generic_set_t;

// given a generic pattern, find all instantiations of that pattern in a subd
set_vec_t Hier_tree::find_all_patterns(const Hier_dag& subd, const generic_set_t& gpattern) {
  set_t global_found_nodes = subd.dag.vert_set();

  set_vec_t found_patterns;

  for (auto pv : subd.dag.verts()) {
    set_t found_pattern = subd.dag.vert_set();

    std::function<void(generic_set_t search_pattern, vertex_t v)> check_pattern = [&](generic_set_t search_pattern, vertex_t v) {
      if (search_pattern.find(ginfo.labels(v)) != search_pattern.end() && !found_pattern.contains(v)
          && !global_found_nodes.contains(v)) {
        // checked here:
        // 1. label of node is something that exists in our set
        // 2. we haven't already found it this iteration
        // 3. we haven't already found it some other iteration
        found_pattern.insert(v);

        for (auto e : subd.dag.out_edges(v)) {
          check_pattern(search_pattern, subd.dag.head(e));
        }
      }
    };

    check_pattern(gpattern, pv);
    if (found_pattern.size() == gpattern.size()) {
      found_patterns.push_back(found_pattern);
      for (auto v : found_pattern) {
        global_found_nodes.insert(v);
      }
    }
  }

  return found_patterns;
}

generic_set_t Hier_tree::make_generic(const Hier_dag& subd, const set_t& pat) {
  generic_set_t gpat;
  for (auto v : pat) {
    gpat.insert(subd.labels(v));
  }
  return gpat;
}

// get value (# inst of P in G * size(G) + size(P))
// duplicated from above because we can get a speed increase by only tracking the count of found patterns, not the instantiations
// themselves
unsigned int Hier_tree::find_value(const Hier_dag& subd, const set_t& pattern) {
  set_t global_found_nodes = subd.dag.vert_set();

  unsigned int count = 0;

  auto gpattern = make_generic(subd, pattern);

  for (auto pv : subd.dag.verts()) {
    set_t found_pattern = subd.dag.vert_set();

    std::function<void(generic_set_t search_pattern, vertex_t v)> check_pattern = [&](generic_set_t search_pattern, vertex_t v) {
      if (search_pattern.find(subd.labels(v)) != search_pattern.end() && !found_pattern.contains(v)
          && !global_found_nodes.contains(v)) {
        found_pattern.insert(v);

        for (auto e : subd.dag.out_edges(v)) {
          check_pattern(search_pattern, subd.dag.head(e));
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

  unsigned int value = count * subd.dag.order() + pattern.size();

  if (reg_verbose) {
    fmt::print("  value ({}) = count ({}) * G({}) + P({})\n", value, count, subd.dag.order(), pattern.size());
  }

  return value;
}

generic_set_t Hier_tree::find_most_freq_pattern(const Hier_dag& subd, const size_t bwidth) {
  set_vec_t                     vp;
  std::vector<Lg_type_id::type> initlabel;

  // remove verts with duplicate labels
  for (auto v : subd.dag.verts()) {
    bool found = false;
    for (size_t i = 0; i < vp.size(); i++) {
      if (initlabel[i] == subd.labels(v)) {
        found = true;
      }
    }
    if (!found) {
      vp.push_back(subd.dag.vert_set());
      vp[vp.size() - 1].insert(v);

      initlabel.push_back(subd.labels(v));
    }
  }

  if (reg_verbose) {
    fmt::print("initial pattern:\n");
  }

  set_t best_pat = vp[0];
  int   best_val = find_value(subd, best_pat);

  auto cmp_set = [&](const set_t& a, const set_t& b) -> bool {
    if (a.size() != b.size()) {
      return false;
    }

    for (auto v : a) {
      if (!b.contains(v)) {
        return false;
      }
    }

    return true;
  };

  auto copy_set = [](set_t& dst, const set_t& src) {
    dst.clear();
    for (auto v : src) {
      dst.insert(v);
    }
  };

  while (vp.size() > 0) {
    // hacky set_t -> value map, since hash tables don't take set_t since it doesn't have a default constructor.
    std::vector<int> memo_vec(subd.dag.size(), -1);

    set_vec_t new_vp;
    for (auto pat : vp) {
      for (auto v : pat) {
        for (auto e : subd.dag.out_edges(v)) {
          auto ov = subd.dag.head(e);
          if (!pat.contains(ov)) {
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
        value       = find_value(subd, new_vp[i]);
        memo_vec[i] = value;
        for (auto v : new_vp[i]) {
          if (reg_verbose) {
            fmt::print("  {}\n", subd.debug_names(v));
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

    vp.clear();

    for (size_t i = 0; i < sortvec.size(); i++) {
      vp.emplace_back(new_vp[sortvec[i].first]);
    }

    if (sortvec.size() > 0 && sortvec[0].second > best_val) {
      best_val = sortvec[0].second;
      copy_set(best_pat, new_vp[sortvec[0].first]);
    }

    // break;
  }

  return make_generic(subd, best_pat);
}

void Hier_tree::compress_hier(const Hier_dag& subd, const generic_set_t& gpat) {
  auto vinst = find_all_patterns(subd, gpat);
  if (vinst.size() > 1) {
    /*
    std::vector<std::pair<vertex_t, unsigned int>> connect_edge_info;
    std::vector<double>                            internal_vert_info;
    for (auto inst : vinst) {
      for (auto v : inst) {
        for (auto e : ginfo.al.out_edges(v)) {
          connect_edge_info.push_back(ginfo.al.head(e), ginfo.weights(e));
        }
        internal_vert_info.push_back(ginfo.areas(v));
        ginfo.al.erase_vert(v);
      }

      // make a new vert.
      // there's no need to generate a whole new hierarchy here,
      // what we have right now maps pretty darn well to what to what we need.
    }
    */
  }
}

void Hier_tree::discover_regularity(size_t hier_index, const size_t beam_width) {
  auto hier = hiers[hier_index];
  I(hier != nullptr);

  std::vector<generic_set_t> pattern_list;
  int                        curr_min_depth = find_tree_depth(hier);

  while (curr_min_depth >= 0) {
    auto hd = Hier_dag();

    // original graph -> dag graph vert map
    auto v_dag_map = ginfo.al.vert_map<vertex_t>();

    auto hier_nodes = ginfo.al.vert_set();

    // get all leaves with depth >= minlevel
    std::function<void(phier, unsigned int, unsigned int)> get_level_nodes
        = [&](phier node, unsigned int level, unsigned int minlevel) {
            if (node->is_leaf()) {
              if (level >= minlevel) {
                for (auto v : ginfo.sets[node->graph_subset]) {
                  auto dag_v   = hd.make_vert(ginfo.debug_names[v], ginfo.labels[v], v);
                  v_dag_map[v] = dag_v;
                  hier_nodes.insert(v);
                }
              }
              return;
            }

            get_level_nodes(node->children[0], level + 1, minlevel);
            get_level_nodes(node->children[1], level + 1, minlevel);
          };

    get_level_nodes(hier, 0, curr_min_depth);

    // add edges to DAG that mirror the edges in ginfo
    for (auto v : hier_nodes) {
      for (auto e : ginfo.al.out_edges(v)) {
        auto ov = ginfo.al.head(e);
        if (hier_nodes.contains(ov)) {
          hd.dag.insert_edge(v_dag_map[v], v_dag_map[ov]);
        }
      }
    }

    if (reg_verbose) {
      fmt::print("\ndepth: {}, nodes:\n", curr_min_depth);
      unsigned int total = 0;
      for (auto v : hd.dag.verts()) {
        fmt::print("  node: {:<15}\n", hd.debug_names(v));
        total++;
      }
      fmt::print("  total: {}\n", total);
    }

    do {
      generic_set_t most_freq_pattern = find_most_freq_pattern(hd, beam_width);
      pattern_list.push_back(most_freq_pattern);
      compress_hier(hd, most_freq_pattern);
    } while (false);  // no repeating patterns in subd

    curr_min_depth--;
  }
}
