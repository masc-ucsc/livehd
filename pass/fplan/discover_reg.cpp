#include <functional>

#include "fmt/core.h"
#include "hier_tree.hpp"
#include "profile_time.hpp"

// given a generic pattern, find all instantiations of that pattern in a subg
set_vec_t Hier_tree::find_all_patterns(Graph_info<g_type>& gi, const set_t& subg, const Pattern& gpattern) const {
  set_vec_t found_patterns;

  unsigned int count = 0;

  unsigned int pattern_size = gpattern.count();

  I(pattern_size > 1);

  auto qualifying_nodes   = gi.al.vert_set();
  auto global_found_nodes = gi.al.vert_set();

  for (auto v : subg) {
    if (gpattern.verts.count(gi.labels(v)) > 0) {
      qualifying_nodes.insert(v);
    }
  }

  while (qualifying_nodes.size() > 0) {
    set_t   pattern_nodes = gi.al.vert_set();
    Pattern temp_gpat     = gpattern;

    // avoid looking at already-visited nodes, regardless of if they're in a pattern or not
    set_t visited_nodes = gi.al.vert_set();

    std::function<void(const vertex_t&)> find_pattern = [&](const vertex_t& v) {
      visited_nodes.insert(v);
      pattern_nodes.insert(v);

      I(temp_gpat.verts.at(gi.labels(v)) > 0);

      temp_gpat.verts[gi.labels(v)]--;

      for (auto e : gi.al.out_edges(v)) {
        auto ov = gi.al.head(e);
        if (subg.contains(ov) && !visited_nodes.contains(ov) && temp_gpat.verts.count(gi.labels(ov)) > 0
            && temp_gpat.verts[gi.labels(ov)] && !global_found_nodes.contains(ov)) {
          find_pattern(ov);
        }
      }
    };

    find_pattern(*(qualifying_nodes.begin()));

    I(pattern_nodes.size() <= pattern_size);

    bool hit_all_nodes = true;
    if (pattern_nodes.size() == pattern_size) {
      // detailed check to make sure we got all the right nodes
      for (auto p : temp_gpat.verts) {
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
  I(count * pattern_size <= subg.size());

  return found_patterns;
}

Pattern Hier_tree::make_generic(Graph_info<g_type>& gi, const set_t& pat) const {
  Pattern gpat;
  for (auto v : pat) {
    gpat.verts[gi.labels(v)] += 1;
  }
  return gpat;
}

Pattern Hier_tree::find_most_freq_pattern(Graph_info<g_type>& gi, const set_t& subg, const size_t bwidth) const {
  set_vec_t                     vp;
  std::vector<Lg_type_id::type> initlabel;

  // remove verts with duplicate labels
  for (auto v : subg) {
    bool found = false;
    for (size_t i = 0; i < vp.size(); i++) {
      if (initlabel[i] == gi.labels(v)) {
        found = true;
      }
    }
    if (!found) {
      vp.push_back(gi.al.vert_set());
      vp[vp.size() - 1].insert(v);

      initlabel.push_back(gi.labels(v));
    }
  }

  if (reg_verbose) {
    /*
    fmt::print("initial pattern:\n");
    for (auto v : vp[0]) {
      fmt::print("  {}\n", gi.debug_names(v));
    }
    */
  }

  if (vp.size() == 0) {
    return Pattern();
  }

  set_t best_pat = vp[0];
  int   best_val = -1;

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
        for (auto e : gi.al.out_edges(v)) {
          auto ov = gi.al.head(e);
          if (!pat.contains(ov) && subg.contains(ov)) {
            auto npat = pat;
            npat.insert(ov);

            // check if the pattern already exists in another set before creating a new one
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
      auto inst = find_all_patterns(gi, subg, make_generic(gi, new_vp[i]));

      // value = (# instantiations of pattern in subg) * size(subg) + size(pattern)
      unsigned int value = inst.size() * subg.size() + new_vp[i].size();
      if (reg_verbose) {
        // fmt::print("  value ({}) = count ({}) * G({}) + P({})\n", value, inst.size(), subg.size(), new_vp[i].size());
      }

      if (inst.size() > 1) {
        sortvec.emplace_back(i, value);
      }
    }

    auto cmp = [](auto a, auto b) -> bool { return a.second > b.second; };

    std::sort(sortvec.begin(), sortvec.end(), cmp);

    sortvec.resize(std::min(bwidth, sortvec.size()));

    if (reg_verbose) {
      if (sortvec.size()) {
        fmt::print("best pattern, value {}:\n", sortvec[0].second);
        /*
        for (auto v : new_vp[sortvec[0].first]) {
          fmt::print("  {}\n", gi.debug_names(v));
        }
        */
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
  }

  return make_generic(gi, best_pat);
}

vertex_t Hier_tree::compress_inst(Graph_info<g_type>& gi, set_t& subg, set_t& inst) {
  auto patv = gi.make_set_vertex("pat_vert", inst);

  for (auto v : inst) {
    subg.erase(v);
  }

  return patv;
}

void Hier_tree::discover_regularity(const size_t beam_width) {
  pattern_sets.resize(hiers.size());
  dags.resize(collapsed_gis.size());

  profile_time::timer t;
  for (size_t i = 0; i < pattern_sets.size(); i++) {
    fmt::print("  discovering regularity on hier {} (max patterns: {})...", i, beam_width);
    t.start();
    discover_regularity(i, beam_width);
    dags[i].init(pattern_sets[i], collapsed_gis[i]);
    fmt::print("done ({} ms).\n", t.time());
  }

  dags[0].dump();
  fmt::print("\n\n");
}

void Hier_tree::discover_regularity(const size_t hier_index, const size_t beam_width) {
  auto  hier = hiers[hier_index];
  auto& gi   = collapsed_gis[hier_index];
  I(hier != nullptr);

  auto& pattern_set = pattern_sets[hier_index];

  int curr_min_depth = find_tree_depth(hier);

  unsigned int old_size = 0;
  unsigned int curr_size;

  while (curr_min_depth >= 0) {
    // profile_time::timer t;
    auto hier_nodes = gi.al.vert_set();

    // verts used in patterns
    auto pat_set = gi.al.vert_set();

    // get all leaves with depth >= minlevel
    std::function<void(phier, unsigned int, unsigned int)> get_level_nodes
        = [&](phier node, unsigned int level, unsigned int minlevel) {
            if (node->is_leaf()) {
              if (level >= minlevel) {
                for (auto v : node->graph_set) {
                  hier_nodes.insert(v);
                }
              }
              return;
            }

            get_level_nodes(node->children[0], level + 1, minlevel);
            get_level_nodes(node->children[1], level + 1, minlevel);
          };

    // t.start();
    // fmt::print("    getting level nodes...");
    get_level_nodes(hier, 0, curr_min_depth);
    // fmt::print("done ({} ms).\n", t.time());

    curr_size = hier_nodes.size();

    if (reg_verbose) {
      fmt::print("\ndepth: {}, ", curr_min_depth);
      if (curr_size == old_size) {
        fmt::print("repeat.");
      } else {
        fmt::print("nodes:\n");
        unsigned int total = 0;
        for (auto v : hier_nodes) {
          fmt::print("  node: {:<15}\n", gi.debug_names(v));
          total++;
        }
        fmt::print("  total: {}\n", total);
      }
    }

    // t.start();
    // fmt::print("    finding most frequent pattern...");
    Pattern most_freq_pattern = find_most_freq_pattern(gi, hier_nodes, beam_width);
    // fmt::print("done ({} ms).\n", t.time());

    // if we have a pattern consisting of more than one element, find_most_frequent_pattern ensures it has >1 instantiation,
    // so we have to collapse it.
    while (most_freq_pattern.count() > 1) {
      if (curr_size == old_size) {
        break;  // nothing new to add to pattern_sets since our set of nodes didn't grow at all
      }

      bool dup = false;
      for (size_t i = 0; i < pattern_set.size(); i++) {
        if (pattern_set[i] == most_freq_pattern) {
          dup = true;
          if (bound_verbose) {
            fmt::print("    pattern {} is a duplicate of {}.\n", i + 1, i);
          }
        }
      }

      if (!dup) {
        pattern_set.push_back(most_freq_pattern);
      }

      // t.start();
      // fmt::print("    compressing hierarchy...");

      auto         vinst = find_all_patterns(gi, hier_nodes, most_freq_pattern);
      unsigned int ctr   = 0;
      for (auto inst : vinst) {
        if (bound_verbose) {
          fmt::print("\npattern {} has {} instantiation(s).\n", ctr++, vinst.size());
        }
        auto comp_v = compress_inst(gi, hier_nodes, inst);
        pat_set.insert(comp_v);
      }

      // fmt::print("done ({} ms).\n", t.time());

      // t.start();
      // fmt::print("    finding most frequent pattern...");
      most_freq_pattern = find_most_freq_pattern(gi, hier_nodes, beam_width);
      // fmt::print("done ({} ms).\n", t.time());
    }

    old_size = curr_size;

    gi.erase_set_verts(pat_set);

    curr_min_depth--;
    if (bound_verbose) {
      fmt::print("    going up a level.\n");
    }
  }
}
