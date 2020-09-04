#include <fmt/core.h>

#include <limits>  // for most negative value in min cut
#include <vector>  // for min cut

#include "hier_tree.hpp"
#include "i_resolve_header.hpp"

std::pair<int, int> Hier_tree::min_wire_cut(Graph_info<g_type>& info, int cut_set) {
  auto&      g    = info.al;
  set_vec_t& sets = info.sets;

  auto cut_verts = g.verts() | ranges::view::remove_if([&](auto v) -> bool { return !sets[cut_set].contains(v); });

  // TODO: graph lib needs a specific version of range-v3, and that version doesn't have size or conversion utilities yet.
  // this method isn't pretty, but it works.  [[maybe_unused]] directive silences warning about "v" being unused.
  unsigned int cut_size = 0;
  for ([[maybe_unused]] auto v : cut_verts) {
    cut_size++;
  }

  I(cut_size >= 2);

  // if there are only two elements in the graph, we can exit early.
  if (cut_size == 2) {
    int      which_vert = 0;
    vertex_t v1, v2;
    for (auto v : cut_verts) {
      if (which_vert == 0) {
        v1 = v;
        which_vert++;
      } else {
        v2 = v;
      }
    }

    auto triv_sets = std::pair(sets.size(), sets.size() + 1);

    sets.push_back(g.vert_set());
    sets.push_back(g.vert_set());

    sets[triv_sets.first].insert(v1);
    sets[triv_sets.second].insert(v2);

    if (hier_verbose) {
      std::cout << "\ntrivial partition:" << std::endl;
      fmt::print("{:<30}a (aka {}), area {:.2f}\n", info.debug_names(v1), triv_sets.first, info.areas(v1));
      fmt::print("{:<30}b (aka {}), area {:.2f}\n", info.debug_names(v2), triv_sets.second, info.areas(v2));
      fmt::print("imb: {:.3f}\n", std::max(info.areas(v1), info.areas(v2)) / (info.areas(v1) + info.areas(v2)));
    }

    return triv_sets;
  }

  // if there are an odd number of elements, we need to insert one to make the graph size even.
  vertex_t temp_even_vertex = g.null_vert();
  if (cut_size % 2 == 1) {
    temp_even_vertex = info.make_temp_vertex(std::string("temp_even"), 0.0, cut_set);
    cut_size++;
  }

  //  The reason why I made vert_set a new variable is because views carry no state of their own,
  //  so the view recomputes what should be contained in it every time we access the view.

  //  In this case, after we adjust the smap values, the original view decides that nothing should
  //  be in the cut_verts view anymore and removes everything.

  //  To resolve this, a new view should be created with the correct condition.

  auto new_sets = std::pair(sets.size(), sets.size() + 1);
  sets.push_back(g.vert_set());
  sets.push_back(g.vert_set());

  // given a vertex, find the set containing that vert, or an invalid set if not found.
  // TODO: replace with std::optional
  auto find_set = [&](auto v) -> size_t {
    for (size_t i = 0; i < sets.size(); i++) {
      if (sets[i].contains(v)) {
        return i;
      }
    }
    return std::numeric_limits<size_t>::max();
  };

  auto is_valid_set = [&, new_sets](auto v) -> bool { return find_set(v) == new_sets.first || find_set(v) == new_sets.second; };

  auto vert_set = g.verts() | ranges::view::remove_if([=](auto v) { return !is_valid_set(v); });

  unsigned int set_size = cut_size / 2;

  auto is_in_a = [&, new_sets](auto v) -> bool { return find_set(v) == new_sets.first; };

  auto is_in_b = [&, new_sets](auto v) -> bool { return find_set(v) == new_sets.second; };

  auto same_set = [&](auto v1, auto v2) -> bool {
    I(is_valid_set(v1));
    I(is_valid_set(v2));
    return find_set(v1) == find_set(v2);
  };

  // assign vertices to one of the two new sets we made
  unsigned int which_set  = 1;
  unsigned int back_index = sets.size() - 3;
  for (auto v : cut_verts) {
    sets[back_index + which_set].insert(v);
    sets[cut_set].erase(v);

    if (which_set == 1) {
      which_set = 2;
    } else {
      which_set = 1;
    }
  }

  // find the total area of both the a and b sets.  If the area is > max_imb, correct it.

  double init_a_area = 0.0;
  double init_b_area = 0.0;

  for (auto v : vert_set) {
    if (is_in_a(v)) {
      init_a_area += info.areas(v);
    } else {
      init_b_area += info.areas(v);
    }
  }

  constexpr double max_imb  = 2.0 / 3.0;
  auto             area_imb = [](double a1, double a2) -> double { return std::max(a1, a2) / (a1 + a2); };

  if (hier_verbose) {
    fmt::print("\nincoming imb: {:.3f}\n", area_imb(init_a_area, init_b_area));
  }

  if (area_imb(init_a_area, init_b_area) > max_imb) {
    // if the area combo is illegal, add some area to a random node to make it legal.  Not super smart, but it works for now
    size_t add_area_set = (init_a_area > init_b_area) ? new_sets.second : new_sets.first;
    double darea        = (1.0 / max_imb) * std::max(init_a_area, init_b_area) - init_a_area - init_b_area + 0.01;

    // TODO: this is a one-shot for loop - fix it
    for (auto v : sets[add_area_set]) {
      if (hier_verbose) {
        fmt::print("adding area {} to node {:<30}\n", darea, info.debug_names[v]);
      }
      info.areas[v] += darea;
      break;
    }
  }

  if (hier_verbose) {
    std::cout << "incoming partition:" << std::endl;
    for (auto v : vert_set) {
      fmt::print("{:<30}{} (aka {}), area {:.2f}\n",
                 info.debug_names(v),
                 (is_in_a(v) ? "a" : "b"),
                 (is_in_a(v) ? new_sets.first : new_sets.second),
                 info.areas(v));
    }
  }

  auto cmap = g.vert_map<Min_cut_data>();

  // track the best possible decrease in cost between the two sets, so that we can return when there is no more work to do
  int best_decrease = 0;

  do {
    // (re)calculate delta costs for each node
    for (auto v : vert_set) {
      int exter = 0;
      int inter = 0;
      for (auto e : g.out_edges(v)) {
        vertex_t other_v = g.head(e);
        if (is_valid_set(other_v)) {
          if (!same_set(v, other_v)) {
            exter += info.weights(e);
          } else {
            inter += info.weights(e);
          }
        }
      }
      cmap[v].d_cost = exter - inter;
      cmap[v].active = true;
    }

    std::vector<vertex_t> av, bv;
    std::vector<int>      cv;
    std::vector<double>   aav;
    std::vector<double>   bav;

    double a_area = 0.0;
    double b_area = 0.0;

    for (auto v : vert_set) {
      if (is_in_a(v)) {
        a_area += info.areas(v);
      } else {
        b_area += info.areas(v);
      }
    }

    bool prev_imbalanced = area_imb(a_area, b_area) > max_imb;
    I(!prev_imbalanced);

    // remove the node pair with the highest global reduction in cost, and add it to cv.
    // also add the nodes used in the reduction to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      int cost = std::numeric_limits<int>::min();

      auto a_max = g.null_vert();
      auto b_max = g.null_vert();

      for (auto v : vert_set) {
        if (cmap(v).active) {
          // row is in the "a" set and hasn't been deleted
          for (auto e : g.out_edges(v)) {
            vertex_t other_v = g.head(e);
            if (is_in_a(v) && is_in_b(other_v) && cmap(other_v).active) {
              // only select nodes in the other set
              int new_cost = cmap(v).d_cost + cmap(other_v).d_cost - 2 * info.weights(e);

              if (new_cost > cost) {
                cost  = new_cost;
                a_max = v;
                b_max = other_v;
              }
            }
          }
        }
      }

      I(cost != std::numeric_limits<int>::min());  // there should always be some kind of min cost
      I(a_max != g.null_vert());                   // these should be written with legal indices
      I(b_max != g.null_vert());

      // save the best swap and mark the swapped nodes as unavailable for future potential swaps
      cv.push_back(cost);

      av.push_back(a_max);
      bv.push_back(b_max);

      double delta_a_area = -info.areas(a_max) + info.areas(b_max);
      double delta_b_area = -info.areas(b_max) + info.areas(a_max);

      aav.push_back(delta_a_area);
      bav.push_back(delta_b_area);

      cmap[a_max].active = false;
      cmap[b_max].active = false;

      auto find_edge_to_max = [&](auto v, auto v_max) -> edge_t {
        if (cmap[v].active) {
          for (auto e : g.out_edges(v)) {
            vertex_t other_v = g.head(e);
            if (other_v == v_max) {  // no checking if other_v is active since the maxes were just deactivated
              return e;
            }
          }
        }
        return g.null_edge();
      };

      // recalculate costs considering a_max and b_max swapped
      for (auto v : vert_set) {
        if (is_in_a(v)) {
          cmap[v].d_cost
              = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, a_max)) - 2 * info.weights(find_edge_to_max(v, b_max));
        } else {
          cmap[v].d_cost
              = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, b_max)) - 2 * info.weights(find_edge_to_max(v, a_max));
        }
      }
    }

    // there should be set_size swaps possible
    I(cv.size() == set_size);

    auto check_sum = [&]() -> bool {
      int total = 0;
      for (const unsigned int c : cv) {
        total += c;
      }
      return total == 0;
    };

    // applying all swaps (aka swapping the sets) should have no effect
    I(check_sum());

    // calculate the maximum benefit reduction out of all reductions possible in this iteration
    best_decrease         = 0;
    size_t decrease_index = 0;

    std::vector<bool> swap_vec(cv.size(), false);

    for (size_t k = 0; k < cv.size(); k++) {
      int    trial_decrease = 0;
      double new_a_area     = a_area;
      double new_b_area     = b_area;
      for (size_t i = 0; i < k; i++) {
        // make sure the area imbalance won't climb too high if the swap occurs
        if (area_imb(new_a_area + aav[i], new_b_area + bav[i]) <= max_imb) {
          trial_decrease += cv[i];
          new_a_area += aav[i];
          new_b_area += bav[i];
          swap_vec[i] = true;
        }
      }
      if (trial_decrease > best_decrease) {
        best_decrease  = trial_decrease;
        decrease_index = k;
      }
    }

    // if the maximum reduction has a higher external cost than internal cost, there's something we can do.
    if (best_decrease > 0) {
      for (size_t i = 0; i < decrease_index; i++) {
        if (hier_verbose) {
          fmt::print("  swapping {} with {}.\n", info.debug_names(av[i]), info.debug_names(bv[i]));
        }

        if (swap_vec[i]) {
          sets[new_sets.first].erase(av[i]);
          sets[new_sets.second].insert(av[i]);

          sets[new_sets.second].erase(bv[i]);
          sets[new_sets.first].insert(bv[i]);
        }
      }
    }

    for (auto v : vert_set) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);

  double final_a_area = 0.0;
  double final_b_area = 0.0;

  for (auto v : vert_set) {
    if (is_in_a(v)) {
      final_a_area += info.areas(v);
    } else {
      final_b_area += info.areas(v);
    }
  }

  double final_imb = std::max(final_a_area, final_b_area) / (final_a_area + final_b_area);
  I(final_imb > 0.0 && final_imb <= 2.0 / 3.0);

  if (hier_verbose) {
    std::cout << "\nbest partition:" << std::endl;
    for (auto v : vert_set) {
      fmt::print("{:<30}{} (aka {}), cost {}, area {:.2f}\n",
                 info.debug_names(v),
                 (is_in_a(v) ? "a" : "b"),
                 (is_in_a(v) ? new_sets.first : new_sets.second),
                 cmap(v).d_cost,
                 info.areas(v));
    }

    fmt::print("a area: {:.2f}, b area: {:.2f}, imb: {:.3f}.\n", final_a_area, final_b_area, final_imb);
  }

  // if we inserted a temporary vertex, remove it from the list of active sets and erase it from the graph
  if (temp_even_vertex != g.null_vert()) {
    int loc = find_set(temp_even_vertex);
    sets[loc].erase(temp_even_vertex);
    g.erase_vert(temp_even_vertex);
  }

  return new_sets;
}

phier Hier_tree::discover_hierarchy(Graph_info<g_type>& info, int start_set, unsigned int num_components) {
  if (info.sets[start_set].size() <= num_components) {
    // set contains less than the minimum number of components, so treat it as a leaf node
    return make_hier_node(start_set);
  }

  auto [a, b] = min_wire_cut(info, start_set);

  phier t1 = discover_hierarchy(info, a, num_components);
  phier t2 = discover_hierarchy(info, b, num_components);

  return make_hier_tree(t1, t2);
}

void Hier_tree::discover_hierarchy(unsigned int num_components) {
  I(num_components >= 1);

  // if the graph is not fully connected, ker-lin fails to work.
  for (const auto& v : ginfo.al.verts()) {
    for (const auto& ov : ginfo.al.verts()) {
      if (ginfo.find_edge(v, ov) == ginfo.al.null_edge()) {
        auto temp_e           = ginfo.al.insert_edge(v, ov);
        ginfo.weights[temp_e] = 0;
      }
    }
  }

  hiers.push_back(discover_hierarchy(ginfo, 0, num_components));

  // undo temp edge creation because it's really inconvienent elsewhere
  for (const auto& v : ginfo.al.verts()) {
    for (const auto& ov : ginfo.al.verts()) {
      auto e = ginfo.find_edge(v, ov);
      if (e != ginfo.al.null_edge() && ginfo.weights[e] == 0) {
        ginfo.al.erase_edge(e);
      }
    }
  }
}

phier Hier_tree::make_hier_tree(phier t1, phier t2) {
  auto pnode  = std::make_shared<Hier_node>();
  pnode->name = "node_" + std::to_string(node_number);

  pnode->children[0] = t1;
  t1->parent         = pnode;

  pnode->children[1] = t2;
  t2->parent         = pnode;

  node_number++;

  return pnode;
}

phier Hier_tree::make_hier_node(const int set) {
  I(set >= 0);

  phier pnode = std::make_shared<Hier_node>();
  pnode->name = "leaf_node_" + std::to_string(node_number);

  auto set_areas = ginfo.al.verts() | ranges::view::remove_if([this, set](auto v) { return !this->ginfo.sets[set].contains(v); })
                   | ranges::view::transform([this](auto v) { return this->ginfo.areas(v); });

  for (const double a : set_areas) {
    pnode->area += a;
  }

  pnode->graph_subset = set;

  node_number++;

  return pnode;
}