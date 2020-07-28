#include "hier_tree.hpp"
/*
unsigned int Hier_tree::size(const phier top) {
  if (top->size != 0) {
    return top->size;
  }

  if (top->children.size() == 0) {
    top->size = 1;
    return 1;
  }

  unsigned int total_size = 1;
  for (const auto& child : top->children) {
    total_size += size(child);
  }

  top->size = total_size;
  return total_size;
}
*/

// since the graph can't actually be split, nodes get assigned to sets.
// this function populates a Min_cut_map with the proper data, assuming the data in start_set is to be split.
// it returns a pair of values indicating the sets it created.
std::pair<int, int> Hier_tree::populate_set_map(const graph::Bi_adjacency_list& g, Set_map& smap) {
  static int set_counter = 1;
  unsigned int set_inc = 0;
  
  // insert vert data, with both new sets getting unique set values to refer to them by
  for (auto vert : g.verts()) {
    smap[vert] = set_counter + set_inc;
    
    if (set_inc == 1) {
      set_inc = 0;
    } else {
      set_inc = 1;
    }
  }

  auto ret_pair = std::pair(set_counter, set_counter + 1);
  set_counter += 2;
  return ret_pair;
}


void Hier_tree::populate_cost_map(const graph::Bi_adjacency_list& g, Min_cut_map& cmap) {
  for (const auto& vert : g.verts()) {
    cmap[vert].d_cost = 0xCAFE;
    cmap[vert].active = true;
  }
}

void Hier_tree::min_wire_cut(Graph_info& info, Set_map& smap) {
  
  auto& g = info.al;
  
  const unsigned int graph_size = g.order();
  const unsigned int set_size = graph_size / 2;

  I(graph_size % 2 == 0); // cost matrix should be symmetric!

  auto cmap = info.al.vert_map<Min_cut_data>();
  populate_cost_map(info.al, cmap);
  auto valid_sets = populate_set_map(info.al, smap);
  
  int best_decrease = 0;
  
  // shouldn't need to ret early if the size is <= 2...?
/*
  if (m.size() <= 2) {
    std::vector<pnetl> a, b;
    a.push_back(m[0].node);
    b.push_back(m[1].node);
    
    return std::pair(a, b);
  }
*/

  auto is_valid_set = [&smap, valid_sets](vertex v) -> bool {
    int set = smap(v);
    return set == valid_sets.first || set == valid_sets.second;
  };

  auto is_in_a = [&smap, valid_sets, &is_valid_set](vertex v) -> bool {
    I(is_valid_set(v));
    return smap(v) % 2 == valid_sets.first;
  };

  auto is_in_b = [&smap, valid_sets, &is_valid_set](vertex v) -> bool {
    I(is_valid_set(v));
    return smap(v) % 2 == valid_sets.second;
  };

  auto same_set = [&smap, &is_valid_set](vertex v1, vertex v2) -> bool {
    I(is_valid_set(v1));
    I(is_valid_set(v2));
    return smap(v1) == smap(v2);
  };

  do {
    // (re)calculate delta costs for each node
    for (const vertex& v : g.verts()) {
      int exter = 0;
      int inter = 0;
      for (const edge& e : g.out_edges(v)) {
        auto other_v = g.head(e);
        if (!same_set(v, other_v)) {
          exter += info.weights[e];
        } else {
          inter += info.weights[e];
        }
      }
      cmap[v].d_cost = exter - inter;
    }
    
    std::vector<vertex> av, bv;
    std::vector<int> cv;
    
    // remove the node pair with the highest global reduction in cost, and add it to cv.
    // also add the nodes used in the reduction to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      
      int cost = std::numeric_limits<int>::min();

      auto a_max = g.null_vert();
      auto b_max = g.null_vert();
      
      for (const vertex& v : g.verts()) {
        if (cmap(v).active) {
          // row is in the "a" set and hasn't been deleted
          for (const edge& e : g.out_edges(v)) {
            auto other_v = g.head(e);
            if (is_in_a(v) && is_in_b(other_v) && cmap(other_v).active) {
              // only select nodes in the other set
              int new_cost = cmap(v).d_cost + cmap(other_v).d_cost - 2 * info.weights(e);
              if (new_cost > cost) {
                cost = new_cost;
                a_max = v;
                b_max = other_v;
              }
            }
          }
        }
      }
      
      if (cost == std::numeric_limits<int>::min()) {
        // if nothing in our adjacency list is connected to anything else anymore, no point in going on.
        break;
      }

      I(a_max != g.null_vert()); // these should be written with legal indices
      I(b_max != g.null_vert());
      
      // at this point, cost should contain the highest reduction cost
      cv.push_back(cost);
      
      av.push_back(a_max);
      bv.push_back(b_max);

      cmap[a_max].active = false;
      cmap[b_max].active = false;
      
      auto find_edge_to_max = [&g, &cmap](vertex v, vertex max) -> edge {
        if (cmap[v].active) {
          for (const edge& e : g.out_edges(v)) {
            vertex other_v = g.head(e);
            if (other_v == max) { // no checking if other_v is active since the maxes were just deactivated
              return e;
            }
          }
        }
        return g.null_edge();
      };

      // recalculate costs considering a_max and b_max swapped
      for (const vertex& v : g.verts()) {
        if (is_in_a(v)) {
          cmap[v].d_cost = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, a_max)) - 2 * info.weights(find_edge_to_max(v, b_max));
        } else {
          cmap[v].d_cost = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, b_max)) - 2 * info.weights(find_edge_to_max(v, a_max));
        }
      }
    }
    
    auto check_sum = [&cv]() -> bool {
      int total = 0;
      for (const unsigned int c : cv) {
        total += c;
      }
      return total == 0;
    };
    
    I(check_sum()); // applying all swaps (aka swapping the sets) should have no effect
    
    // calculate the maximum benefit reduction out of all reductions possible in this iteration
    best_decrease = 0;
    size_t decrease_index = 0;
    
    for (size_t k = 0; k < cv.size(); k++) {
      int trial_decrease = 0;
      for (size_t i = 0; i < k; i++) {
        trial_decrease += cv[i];
      }
      if (trial_decrease > best_decrease) {
        best_decrease = trial_decrease;
        decrease_index = k;
      }
    }
    
    // if the maximum reduction has a higher external cost than internal cost, there's something we can do.
    if (best_decrease > 0) {
      for (size_t i = 0; i < decrease_index; i++) {
        std::cout << "swapping " << info.names(av[i]) << " with " << info.names(bv[i]) << std::endl;
        std::swap(smap[av[i]], smap[bv[i]]);
      }
    }

    for (const vertex& v : g.verts()) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);

#ifndef NDEBUG
  std::cout << std::endl;
  std::cout << "best partition:" << std::endl;
  for (const vertex& v : info.al.verts()) {
    std::cout << info.names(v) << ":\t";
    std::cout << (is_in_a(v) ? "a" : "b") << " (aka " << (is_in_a(v) ? valid_sets.first : valid_sets.second);
    std::cout << "), cost " << cmap(v).d_cost << std::endl;
  }
#endif
}

/*
phier Hier_tree::make_hier_tree(phier t1, phier t2) {
  static unsigned int temp_node_number = 0; // exact value doesn't really matter, as long as it's unique
  std::string name = "_hier_node_" + std::to_string(temp_node_number);
  temp_node_number++;

  Hier_node hier_temp;
  hier_temp.name = name;

  hier_temp.children.push_back(t1);
  hier_temp.children.push_back(t2);

  return std::make_shared<Hier_node>(hier_temp);
}
*/

phier Hier_tree::discover_hierarchy(Graph_info& info, Set_map& smap, int start_set) {
  
  // figure out the number of verts in the set
  unsigned int set_size = 0;
  for (const auto& v : info.al.verts()) {
    if (smap(v) == start_set) {
      set_size++;
    }
  }
  
  if (set_size <= num_components) {
    phier p = std::make_shared<Hier_node>();
    p->name = "stuff";
    p->area = -1;
    p->graph_subset = -1; // TODO: grab this from a graph element or something
    return p;
  }
  
  min_wire_cut(info, smap);

  // TODO: check this
  //phier t1 = discover_hierarchy(info, smap, start_set + 1);
  //phier t2 = discover_hierarchy(info, smap, start_set + 2);
  
  phier p;
  return p;
  //return make_hier_tree(t1, t2);
}

/*
void Hier_tree::collapse() {

}
*/

Hier_tree::Hier_tree(Graph_info& info, unsigned int min_num_components, double min_area)
  : area(min_area), num_components(min_num_components) {
  Set_map smap = info.al.vert_map<int>();
  root = discover_hierarchy(info, smap, 0);
  // TODO: collapse the tree from root
}

