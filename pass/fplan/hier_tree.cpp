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

/*
// fill out the connections in the matrix by examining every connection on every node... :/
void Hier_tree::wire_matrix(Cost_matrix& m) {
  for (size_t i = 0; i < m.size(); i++) {
    for (const auto& conn : m[i].node->connect_list) {
      for (size_t j = 0; j < m.size(); j++) {
        if (conn.first == m[j].node) {
          m[i].connect_cost[j] = conn.second;
        }
      }
    }
  }
}
*/

/*
// creates a cost matrix for doing a min-cut on the root node
Cost_matrix Hier_tree::make_matrix(pnode root) {
  unsigned int root_size = size(root);
  
  cost_matrix matrix;
  matrix.reserve(root_size);

  unsigned int which_set = 0;

  // create a cost matrix from the hierarchy
  std::function<void(pnode)> alloc_matrix;
  
  alloc_matrix = [&](pnode node) {
    cost_matrix_row new_row = { node, std::vector<int>(root_size, 0), 0xCAFE, true, which_set };
    matrix.push_back(new_row);
    
    which_set ^= 1; // toggle between 1 and 0
    
    for (pnode child : node->children) {
      alloc_matrix(child);
    }
  };
  
  alloc_matrix(root);

  if (matrix.size() % 2) {
    // insert a temp node with no connections to make the matrix even
    // (we know that there are no temp nodes that need to be deleted since the matrix is new)

    for (cost_matrix_row& r : matrix) {
      r.connect_cost.push_back(0);
    }

    pnode temp = std::make_shared<Netl_node>();
    temp->name = "_temp"; // give the temp node a unique name
    matrix.push_back({ temp, std::vector<int>(matrix.size() + 1, 0), 0xCAFE, true, which_set });
    
    std::cout << "inserting temp node into set " << (which_set ? "b" : "a") << "." << std::endl;
  }

  wire_matrix(matrix);

  return matrix;
}
*/

/*
// splits a cost matrix into two sub-matrices for a recursive min-cut call
// if the split is not even, a temporary node is added at the end to make it even.
std::pair<Cost_matrix, Cost_matrix> Hier_tree::halve_matrix(const Cost_matrix& old_matrix) {
  Cost_matrix ma, mb;
  
  unsigned int which_set_a = 0;
  unsigned int which_set_b = 0;

  unsigned int old_size = old_matrix.size();
  unsigned int new_size = old_size / 2;

  bool need_extra_row = false;
  
  if (new_size % 2) {
    // matrix is odd, so do something to make it even
    if (old_matrix[old_matrix.size() - 1].node->name == "_temp") {
      // matrix has a temp node, so remove it
      old_size--;
      new_size--;
    } else {
      // matrix doesn't have a temp node, so add one
      new_size++;
      need_extra_row = true;
    }
  }

  ma.reserve(new_size);
  mb.reserve(new_size);
  
  for (size_t i = 0; i < old_matrix.size(); i++) {
    Cost_matrix_row new_row = { old_matrix[i].node, std::vector<int>(new_size, 0), 0xCAFE, true };
    if (old_matrix[i].set == 0) {
      new_row.set = which_set_a;
      which_set_a ^= 1;
      ma.push_back(new_row);
    } else {
      new_row.set = which_set_b;
      which_set_b ^= 1;
      mb.push_back(new_row);
    }
  }

  if (need_extra_row) {
    pnetl n = std::make_shared<Netl_node>();
    n->name = "_temp";
    Cost_matrix_row temp_row = { n, std::vector<int>(new_size, 0), 0xCAFE, true };
    
    temp_row.set = which_set_a;
    ma.push_back(temp_row);
    
    temp_row.set = which_set_b;
    mb.push_back(temp_row);
  }

  wire_matrix(ma);
  wire_matrix(mb);
  
  return std::pair(ma, mb);
}
*/

/*
void Hier_tree::prune_matrix(Cost_matrix& m) {
  if (m[m.size() - 1].node->name == "_temp") {
    m.erase(m.cend() - 1);
    for (Cost_matrix_row& r : m) {
      r.connect_cost.erase(r.connect_cost.cend() - 1);
    }
  }
}
*/

void Hier_tree::min_wire_cut(Graph_info& info) {
  
  auto& g = info.al;
  
  const unsigned int graph_size = g.order();
  const unsigned int set_size = graph_size / 2;

  I(graph_size % 2 == 0); // cost matrix should be symmetric!

  auto cmap = info.al.vert_map<Min_cut_data>();
  populate_cost_map(info.al, cmap);
  
  int best_decrease = 0;
/*
  if (m.size() <= 2) {
    std::vector<pnetl> a, b;
    a.push_back(m[0].node);
    b.push_back(m[1].node);
    
    return std::pair(a, b);
  }
*/

  do {
    // (re)calculate delta costs for each node
    for (const vertex& v : g.verts()) {
      int exter = 0;
      int inter = 0;
      for (const edge& e : g.out_edges(v)) {
        auto other_v = g.head(e);
        if (cmap(other_v).set != cmap(v).set) {
          exter += info.weights[e];
        } else {
          inter += info.weights[e];
        }
      }
      cmap[v].d_cost = exter - inter;
    }
    
#ifndef NDEBUG
    std::cout << std::endl;
    std::cout << "connection list:" << std::endl;
    for (const vertex& v : g.verts()) {
      std::cout << info.names(v) << "\t" << ((cmap(v).set == 0) ? "(set a" : "(set b") << ", cost: " << cmap(v).d_cost << ") [";
      for (const edge& e : g.out_edges(v)) {
        if (info.weights(e) != 0) {
          // don't print connections with no weight, as they don't matter.
          std::cout << info.names(g.head(e)) << ", ";
        }
      }
      std::cout << "]" << std::endl;
    }
#endif
    
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
            if (cmap(v).set == 0 && cmap(other_v).set == 1 && cmap(other_v).active) {
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
        if (cmap(v).set == 0) {
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
        std::swap(cmap[av[i]].set, cmap[bv[i]].set);
      }
    }

    for (const vertex& v : g.verts()) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);
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

void Hier_tree::populate_cost_map(const graph::Bi_adjacency_list& g, Min_cut_map& m) {
  unsigned int which_set = 0;

  // insert vert data
  for (auto vert : g.verts()) {
    m[vert].d_cost = 0xCAFE; // illegal value
    m[vert].active = true;
    m[vert].set = which_set;

    which_set ^= 1; // toggle between 0 and 1
  }
}

phier Hier_tree::discover_hierarchy(Graph_info && info) {
  if (info.al.order() <= num_components) {
    phier p;
    return p; // TODO: ??
  }
  
  min_wire_cut(info);
  
#ifndef NDEBUG
  std::cout << "a set:" << std::endl;
  std::cout << std::endl << "b set:" << std::endl;
#endif
  
  // TODO: not sure how to run discovery on incomplete graphs...?

  //phier t1 = discover_hierarchy(m_pair.first);
  //phier t2 = discover_hierarchy(m_pair.second);

  // TODO: prune the hierarchy trees here

  phier p;
  return p;
  //return make_hier_tree(t1, t2);
}

Hier_tree::Hier_tree(Graph_info && info) {
  root = discover_hierarchy(std::move(info));
  // TODO: collapse the tree from root
}

/*
void Hier_tree::collapse() {

}
*/

