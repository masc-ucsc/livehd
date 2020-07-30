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

std::pair<int, int> Hier_tree::min_wire_cut(Graph_info& info, Set_map& smap, int cut_set) {
  using namespace ranges;

  auto& g = info.al;
  
  // TODO: why does smap need to be captured by value?
  auto not_in_cut_set = [&smap, cut_set](vertex v) -> bool {
    return smap(v) != cut_set;
  };
  
  auto vert_set = g.verts() | view::remove_if(not_in_cut_set);

  // TODO: graph lib needs a specific version of range-v3, and that version doesn't have size or conversion utilities yet.
  // as far as I know, there isn't really a better way to do this.
  unsigned int graph_size = 0;
  for ([[maybe_unused]] const auto& v : vert_set) {
    graph_size++;
  }
  
  I(graph_size >= 2);
  
  static int set_counter = 1;
  auto new_sets = std::pair(set_counter, set_counter + 1);
  
  // if there are only two elements in the graph, we can exit early.
  if (graph_size == 2) {
    int which_vert = 0;
    for (const auto& v : vert_set) {
      if (which_vert == 0) {
        smap[v] = new_sets.first;
        which_vert++;
      } else {
        smap[v] = new_sets.second;
      }
    }
    return new_sets;
  }
  
  // if there are an odd number of elements, we need to insert one to make the graph size even.
  vertex temp_vertex = g.null_vert();
  if (graph_size % 2 == 1) {
    temp_vertex = g.insert_vert();
    info.names[temp_vertex] = "temp_node";
    info.areas[temp_vertex] = 0.0f;
    
    for (const auto& other_v : g.verts()) {
      edge temp_edge = g.insert_edge(temp_vertex, other_v);
      info.weights[temp_edge] = 0;

      temp_edge = g.insert_edge(other_v, temp_vertex);
      info.weights[temp_edge] = 0;
    }
    
    smap[temp_vertex] = cut_set;
    
    // re-generate vert_set since we changed the underlying graph
    vert_set = g.verts() | view::remove_if(not_in_cut_set);

    for (const auto& v : vert_set) {
      std::cout << info.names(v) << std::endl;
    }

    graph_size++;
  }

  // make new set numbers
  unsigned int set_inc = 0;
  
  for (auto v : vert_set) {
    std::cout << "vertex " << info.names(v) << ": " << smap(v) << std::endl;
  }

  for (auto v : vert_set) {
    smap[v] = set_counter + set_inc;
    
    if (set_inc == 1) {
      set_inc = 0;
    } else {
      set_inc = 1;
    }
  }

  for (auto v : vert_set) {
    std::cout << "vertex " << info.names(v) << ": " << smap(v) << std::endl;
  }

  set_counter += 2;

  const unsigned int set_size = graph_size / 2;

  auto is_valid_set = [&smap, new_sets](vertex v) -> bool {
    return smap(v) == new_sets.first || smap(v) == new_sets.second;
  };

  auto is_in_a = [&smap, new_sets](vertex v) -> bool {
    return smap(v) == new_sets.first;
  };

  auto is_in_b = [&smap, new_sets](vertex v) -> bool {
    return smap(v) == new_sets.second;
  };

  auto same_set = [&smap, &is_valid_set](vertex v1, vertex v2) -> bool {
    I(is_valid_set(v1));
    I(is_valid_set(v2));
    return smap(v1) == smap(v2);
  };

  auto cmap = g.vert_map<Min_cut_data>();
  
  int best_decrease = 0;

  do {
    // (re)calculate delta costs for each node
    for (const vertex& v : vert_set) {
      int exter = 0;
      int inter = 0;
      for (const edge& e : g.out_edges(v)) {
        auto other_v = g.head(e);
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
    
    std::vector<vertex> av, bv;
    std::vector<int> cv;
    
    // remove the node pair with the highest global reduction in cost, and add it to cv.
    // also add the nodes used in the reduction to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      
      int cost = std::numeric_limits<int>::min();

      auto a_max = g.null_vert();
      auto b_max = g.null_vert();
      
      for (const vertex& v : vert_set) {
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
      
      // TODO: cost never gets written if the graph contains disconnected subgraphs with odd numbers of elements, fix.
      I(cost != std::numeric_limits<int>::min()); // there should always be some kind of min cost
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
      for (const vertex& v : vert_set) {
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
    
    I(cv.size() == set_size);
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

    for (const vertex& v : vert_set) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);

#ifndef NDEBUG
  std::cout << std::endl;
  std::cout << "best partition:" << std::endl;
  for (const vertex& v : vert_set) {
    std::cout << info.names(v) << ":\t";
    std::cout << (is_in_a(v) ? "a" : "b") << " (aka " << (is_in_a(v) ? new_sets.first : new_sets.second);
    std::cout << "), cost " << cmap(v).d_cost << std::endl;
  }
#endif
  
  if (temp_vertex != g.null_vert()) {
    g.erase_vert(temp_vertex);
  }
  
  return new_sets;
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
  
  std::cout << "splitting set " << start_set << std::endl;

  // figure out the number of verts in the set
  unsigned int set_size = 0;
  for (const auto& v : info.al.verts()) {
    if (smap(v) == start_set) {
      set_size++;
    }
  }
  
  if (set_size <= num_components) {
    // set contains less than the minimum number of components, so treat it as a leaf node
    phier p = std::make_shared<Hier_node>();
    p->name = "stuff";
    p->area = -1;
    p->graph_subset = -1; // TODO: grab this from a graph element or something
    return p;
  }
  
  auto [a, b] = min_wire_cut(info, smap, start_set);

  phier t1 = discover_hierarchy(info, smap, a);
  phier t2 = discover_hierarchy(info, smap, b);
  
  phier p = std::make_shared<Hier_node>();
  return p;
  //return make_hier_tree(t1, t2);
}

/*
void Hier_tree::collapse() {

}
*/

Hier_tree::Hier_tree(Graph_info& info, unsigned int min_num_components, double min_area)
  : area(min_area), num_components(min_num_components) {
  
  I(num_components >= 1);
  I(area >= 0.0);

  Set_map smap = info.al.vert_map<int>();
  for (const auto& v : info.al.verts()) {
    smap[v] = 0; // everything starts in set 0
  }
  
  root = discover_hierarchy(info, smap, 0);
  // TODO: collapse the tree from root
}

