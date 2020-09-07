#include "hier_tree.hpp"

// given a generic pattern, find all instantiations of that pattern in a subg
set_vec_t Hier_tree::find_all_patterns(const set_t& subg, const generic_set_t& gpattern) {
  set_vec_t found_patterns;

  unsigned int count = 0;

  unsigned int pattern_size = 0;
  for (auto p : gpattern) {
    pattern_size += p.second;
  }

  auto qualifying_nodes   = ginfo.al.vert_set();
  auto global_found_nodes = ginfo.al.vert_set();

  for (auto v : subg) {
    if (gpattern.count(ginfo.labels(v)) > 0) {
      qualifying_nodes.insert(v);
    }
  }

  while (qualifying_nodes.size() > 0) {
    set_t         pattern_nodes = ginfo.al.vert_set();
    generic_set_t temp_gpat     = gpattern;

    // avoid looking at already-visited nodes, regardless of if they're in a pattern or not
    set_t visited_nodes = ginfo.al.vert_set();

    std::function<void(const vertex_t&)> find_pattern = [&](const vertex_t& v) {
      visited_nodes.insert(v);
      pattern_nodes.insert(v);

      I(temp_gpat.at(ginfo.labels(v)) > 0);

      temp_gpat.at(ginfo.labels(v))--;

      for (auto e : ginfo.al.out_edges(v)) {
        auto ov = ginfo.al.head(e);
        if (subg.contains(ov) && !visited_nodes.contains(ov) && temp_gpat.count(ginfo.labels(ov)) > 0
            && temp_gpat.at(ginfo.labels(ov)) && !global_found_nodes.contains(ov)) {
          find_pattern(ov);
        }
      }
    };

    find_pattern(*(qualifying_nodes.begin()));

    I(pattern_nodes.size() <= pattern_size);

    bool hit_all_nodes = true;
    if (pattern_nodes.size() == pattern_size) {
      // detailed check to make sure we got all the right nodes
      for (auto p : temp_gpat) {
        if (p.second != 0) {
          hit_all_nodes = false;
          break;
        }
      }
    } else {
      // we didn't find a pattern because the count is wrong
      hit_all_nodes = false;
    }

    if (hit_all_nodes) {
      count++;
      found_patterns.push_back(pattern_nodes);
      for (auto v : pattern_nodes) {
        global_found_nodes.insert(v);
      }
    }

    for (auto v : pattern_nodes) {
      qualifying_nodes.erase(v);
    }
  }

  I(count);
  I(count * pattern_size < subg.size());

  if (reg_verbose) {
    unsigned int value = count * subg.size() + pattern_size;
    //fmt::print("  value ({}) = count ({}) * G({}) + P({})\n", value, count, subg.size(), pattern_size);
  }

  return found_patterns;
}

Hier_tree::generic_set_t Hier_tree::make_generic(const set_t& pat) {
  generic_set_t gpat;
  for (auto v : pat) {
    gpat[ginfo.labels(v)] += 1;
  }
  return gpat;
}

// get value (# inst of P in G * size(G) + size(P))
unsigned int Hier_tree::find_value(const set_t& subg, const set_t& pattern) {
  auto         inst  = find_all_patterns(subg, make_generic(pattern));
  unsigned int value = inst.size() * subg.size() + pattern.size();
  if (reg_verbose) {
    //fmt::print("  value ({}) = count ({}) * G({}) + P({})\n", value, inst.size(), subg.size(), pattern.size());
  }
  return inst.size() * subg.size() + pattern.size();
}

Hier_tree::generic_set_t Hier_tree::find_most_freq_pattern(const set_t& subg, const size_t bwidth) {
  set_vec_t                     vp;
  std::vector<Lg_type_id::type> initlabel;

  // remove verts with duplicate labels
  for (auto v : subg) {
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

  if (reg_verbose) {
    // fmt::print("initial pattern:\n");
    for (auto v : vp[0]) {
      // fmt::print("  {}\n", ginfo.debug_names(v));
    }
  }

  set_t best_pat = vp[0];
  int   best_val = find_value(subg, best_pat);

  auto cmp_set = [](const set_t& a, const set_t& b) -> bool {
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
    set_vec_t new_vp;
    for (auto pat : vp) {
      for (auto v : pat) {
        for (auto e : ginfo.al.out_edges(v)) {
          auto ov = ginfo.al.head(e);
          if (!pat.contains(ov) && subg.contains(ov)) {
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

    // pair of [owner of value in new_vp, value]
    std::vector<std::pair<size_t, int>> sortvec;
    for (size_t i = 0; i < new_vp.size(); i++) {
      sortvec.emplace_back(i, find_value(subg, new_vp[i]));
    }

    auto cmp = [](auto a, auto b) -> bool { return a.second > b.second; };

    std::sort(sortvec.begin(), sortvec.end(), cmp);

    sortvec.resize(std::min(bwidth, sortvec.size()));

    if (reg_verbose) {
      if (sortvec.size()) {
        //fmt::print("best pattern, value {}:\n", sortvec[0].second);
        for (auto v : new_vp[sortvec[0].first]) {
          // fmt::print("  {}\n", ginfo.debug_names(v));
        }
      }
    }

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

  return make_generic(best_pat);
}

void Hier_tree::compress_hier(const set_t& subg, const generic_set_t& gpat) {
  auto vinst = find_all_patterns(subg, gpat);
  if (vinst.size() > 1) {
    fmt::print("stuff\n");
    // std::vector<double>                            internal_vert_info;
    /*
    for (auto inst : vinst) {
      std::vector<std::pair<vertex_t, unsigned int>> connect_edge_info;

      for (auto v : inst) {
        for (auto e : ginfo.al.out_edges(v)) {
          auto ov = ginfo.al.head(e);

          if (!inst.contains(ov)) {
            // only track incoming or outgoing edges
            connect_edge_info.emplace_back(ginfo.al.head(e), ginfo.weights(e));
          }
        }
        // internal_vert_info.push_back(subg.areas(v));
        // ginfo.al.erase_vert(v);
      }


    }
    */
  }
}

// when compressing, only compress the hierarchy - not the actual graph! we need the netlist!
// when items are compressed, remove them from the set of active nodes (subg)

void Hier_tree::discover_regularity(size_t hier_index, const size_t beam_width) {
  auto hier = hiers[hier_index];
  I(hier != nullptr);

  std::vector<generic_set_t> pattern_list;
  int                        curr_min_depth = find_tree_depth(hier);

  while (curr_min_depth >= 0) {
    auto hier_nodes = ginfo.al.vert_set();

    // get all leaves with depth >= minlevel
    std::function<void(phier, unsigned int, unsigned int)> get_level_nodes
        = [&](phier node, unsigned int level, unsigned int minlevel) {
            if (node->is_leaf()) {
              if (level >= minlevel) {
                for (auto v : ginfo.sets[node->graph_subset]) {
                  hier_nodes.insert(v);
                }
              }
              return;
            }

            get_level_nodes(node->children[0], level + 1, minlevel);
            get_level_nodes(node->children[1], level + 1, minlevel);
          };

    get_level_nodes(hier, 0, curr_min_depth);

    if (reg_verbose) {
      fmt::print("\ndepth: {}, nodes:\n", curr_min_depth);
      unsigned int total = 0;
      for (auto v : hier_nodes) {
        fmt::print("  node: {:<15}\n", ginfo.debug_names(v));
        total++;
      }
      fmt::print("  total: {}\n", total);
    }

    do {
      generic_set_t most_freq_pattern = find_most_freq_pattern(hier_nodes, beam_width);
      pattern_list.push_back(most_freq_pattern);
      compress_hier(hier_nodes, most_freq_pattern);
    } while (false);  // until no repeating patterns in subg (best pattern size is 1)

    curr_min_depth--;
  }
}
