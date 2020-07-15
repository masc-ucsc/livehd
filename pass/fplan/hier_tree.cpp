#include "tree.hpp"

unsigned int Hier_tree::size(const std::shared_ptr<Hier_node> top) {
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

// fill out the connections in the matrix by examining every connection on every node... :/
void Hier_tree::wire_matrix(cost_matrix& m) {
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

// creates a cost matrix for doing a min-cut on the root node
cost_matrix Hier_tree::make_matrix(pnode root) {
  unsigned int root_size = size(root);
  I(root_size % 2 == 0); // TODO: handle odd numbers of nodes
  
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

  wire_matrix(matrix);

  return matrix;
}

// splits a cost matrix into two sub-matrices for a recursive min-cut call
std::pair<cost_matrix, cost_matrix> Hier_tree::halve_matrix(const cost_matrix& old_matrix) {
  cost_matrix ma, mb;
  
  unsigned int which_set_a = 0;
  unsigned int which_set_b = 0;

  const unsigned int new_size = old_matrix.size() / 2;
  
  ma.reserve(new_size);
  mb.reserve(new_size);
  
  for (size_t i = 0; i < old_matrix.size(); i++) {
    cost_matrix_row new_row = { old_matrix[i].node, std::vector<int>(new_size, 0), 0xCAFE, true };
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

  wire_matrix(ma);
  wire_matrix(mb);
  
  return std::pair(ma, mb);
}

std::pair<std::vector<pnode>, std::vector<pnode>> Hier_tree::min_wire_cut(cost_matrix& m) {

  const unsigned int graph_size = m.size();
  const unsigned int set_size = m.size() / 2;
  
  int best_decrease = 0;

  do {
    // (re)calculate delta costs for each node
    for (cost_matrix_row& r : m) {
      int exter = 0;
      int inter = 0;
      for (size_t i = 0; i < graph_size; i++) {
        int cost = r.connect_cost[i];
        if (m[i].set != r.set) {
          exter += cost;
        } else {
          inter += cost;
        }
      }
      r.d_cost = exter - inter;
    }
    
#ifndef NDEBUG
    std::cout << std::endl;
    std::cout << "connection matrix:" << std::endl;
    
    for (const cost_matrix_row& r : m) {
      std::cout << r.node->name << "\t" << ((r.set == 0) ? " (a): [ " : " (b): [ ");
      for (const auto& conn_cost : r.connect_cost) {
        std::cout << std::setfill(' ') << std::setw(5) << conn_cost << std::setfill(' ') << std::setw(5);
      }
      std::cout << " ]" << std::endl;
    }
#endif

    std::vector<size_t> av, bv;
    std::vector<int> gv;
    
    // preload vectors with known size to avoid mem alloc
    av.reserve(set_size);
    bv.reserve(set_size);
    gv.reserve(set_size);

    // for each node in the set of nodes in a and b:
    // remove the node pair with the highest global reduction in cost, and add it to gv.
    // also add the nodes to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      
      int g = std::numeric_limits<int>::min();

      int a_max_i = -1;
      int b_max_i = -1;
      
      for (size_t i = 0; i < graph_size; i++) {
        if (m[i].set == 0 && m[i].active) {
          // row is in the "a" set and hasn't been deleted
          for (size_t j = 0; j < graph_size; j++) {
            if (m[j].set != m[i].set && m[j].active) {
              // only select nodes in the other set
              int new_g = m[i].d_cost + m[j].d_cost - 2 * m[i].connect_cost[j];
              if (new_g > g) {
                g = new_g;
                a_max_i = i;
                b_max_i = j;
              }
            }
          }
        }
      }
      
      I(a_max_i != -1); // these should be written with legal indices
      I(b_max_i != -1);
      I(g != std::numeric_limits<int>::min());
      
      // at this point, g should contain the highest reduction cost
      gv.push_back(g);
      
      av.push_back(a_max_i);
      bv.push_back(b_max_i);
      
      m[a_max_i].active = false;
      m[b_max_i].active = false;

      for (size_t i = 0; i < graph_size; i++) {
        if (m[i].set == 0 && m[i].active) {
          m[i].d_cost = m[i].d_cost + 2 * m[i].connect_cost[a_max_i] - 2 * m[i].connect_cost[b_max_i];
        } else {
          m[i].d_cost = m[i].d_cost + 2 * m[i].connect_cost[b_max_i] - 2 * m[i].connect_cost[a_max_i];
        }
      }
    }

    auto check_sum = [&gv]() -> bool {
      int total = 0;
      for (const auto& g : gv) {
        total += g;
      }
      return total == 0;
    };
    
    I(check_sum()); // moving all elements from one set to the other should have no effect

    // calculate the maximum benefit reduction out of all reductions possible in this iteration
    best_decrease = 0;

    size_t decrease_index = 0;
    for (size_t k = 0; k < gv.size(); k++) {
      int trial_decrease = 0;
      for (size_t i = 0; i < k; i++) {
        trial_decrease += gv[i];
      }
      if (trial_decrease > best_decrease) {
        best_decrease = trial_decrease;
        decrease_index = k;
      }
    }

    // if the maximum reduction has a higher external cost than internal cost, there's something we can do.
    if (best_decrease > 0) {
      for (size_t i = 0; i <= decrease_index; i++) {
        std::cout << "swapping " << m[av[i]].node->name << " with " << m[bv[i]].node->name << std::endl;
        std::swap(m[av[i]].set, m[bv[i]].set);
      }
    }

    for (size_t i = 0; i < graph_size; i++) {
      m[i].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
    
  } while (best_decrease > 0);
  
  // strip weight information and return vectors
  std::vector<pnode> a_res, b_res;
  
  a_res.reserve(set_size);
  b_res.reserve(set_size);

  for (size_t i = 0; i < graph_size; i++) {
    if (m[i].set == 0) {
      a_res.push_back(m[i].node);
    } else {
      b_res.push_back(m[i].node);
    }
  }

  return std::pair(a_res, b_res);
}

pnode Hier_tree::make_tree(pnode t1, pnode t2) {
  // I think I could just make a new Hier_node containing the t1 and t2 as children and return that...?
  // TODO: get this working
  pnode p;
  return p;
}

pnode Hier_tree::discover_hierarchy() {
  cost_matrix m = make_matrix(root);
  return discover_hierarchy(m);
}

pnode Hier_tree::discover_hierarchy(cost_matrix& m) {
  if (m.size() <= num_components) {
    return root;
  }
  
  std::pair<std::vector<pnode>, std::vector<pnode>> cut_pair = min_wire_cut(m);
  auto m_pair = halve_matrix(m);
  
#ifndef NDEBUG
  std::cout << "A set contains:" << std::endl;
  for (const auto& elem : cut_pair.first) {
    std::cout << "\t" << elem->name << std::endl;
  }

  std::cout << std::endl << "B set contains:" << std::endl;
  for (const auto& elem : cut_pair.second) {
    std::cout << "\t" << elem->name << std::endl;
  }
#endif
  
  pnode t1 = discover_hierarchy(m_pair.first);
  pnode t2 = discover_hierarchy(m_pair.second);
  
  return make_tree(t1, t2);
}

void Hier_tree::collapse(const double area) {
  
}

