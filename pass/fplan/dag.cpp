#include "dag.hpp"

#include <functional>
#include <string>  // for std::to_string
#include <unordered_set>

#include "fmt/core.h"

void Dag::init(std::vector<Pattern> hier_patterns, const Graph_info<g_type>& ginfo) {
  // need to keep track of all the verts we've come across so we can add them to the dag as leaves if required
  std::unordered_map<Lg_type_id::type, unsigned int> subp_verts;

  // add an edge from a parent to a child, making sure to keep track of repeating edges
  auto add_edge = [&](pdag parent, pdag child, unsigned int count) {
    child->parent = parent;

    // technically an illegal value
    size_t child_index = parent->children.size();

    for (size_t i = 0; i < parent->children.size(); i++) {
      if (parent->children[i] == child) {
        child_index = i;
      }
    }

    if (child_index == parent->children.size()) {
      parent->children.push_back(child);
      parent->child_edge_count.emplace_back(count);
    } else {
      parent->child_edge_count[child_index] += count;
    }
  };

  auto add_vert = [&]() -> pdag {
    auto pd    = std::make_shared<Dag_node>();
    pd->dag_id = ++dag_id_counter;
    return pd;
  };

  for (auto pat : hier_patterns) {
    auto pd = add_vert();
    pat_dag_map.emplace(pat, pd);
  }

  // check for edges between patterns
  for (size_t i = 0; i < hier_patterns.size(); i++) {
    auto  pat       = hier_patterns[i].verts;
    auto& pat_dag_p = pat_dag_map[hier_patterns[i]];

    // going in reverse because we want the largest subset, not the smallest
    for (int j = i - 1; j >= 0; j--) {
      auto subpat = hier_patterns[j].verts;

      bool subset = true;

      for (auto pair : pat) {
        for (auto spair : subpat) {
          if (spair.first != pair.first || spair.second > pair.second) {
            subset = false;
            break;
          }
        }

        if (!subset) {
          break;
        }
      }

      if (subset) {
        for (auto spair : subpat) {
          pat[spair.first] -= spair.second;

          add_edge(pat_dag_p, pat_dag_map[hier_patterns[j]], spair.second);
          j++;  // run the same check over again to see if we can match another subpattern of the same type
        }
      }
    }

    // anything not matched by a subpattern is a leaf node of the pattern
    for (auto pair : pat) {
      if (pair.second > 0) {
        auto pd = add_vert();
        subp_verts.emplace(pair);
        // pd->label = pair.first;
        // I(leaf_dims.count(pair.first) > 0);
        // pd->dims = leaf_dims[pair.first];

        add_edge(pat_dag_p, pd, pair.second);
      }
    }
  }

  // find subpatterns not owned by other subpatterns, as they should belong to root
  for (auto pd : pat_dag_map) {
    if (pd.second->parent == nullptr) {
      add_edge(root, pd.second, 1);
    }
  }

  std::unordered_map<Lg_type_id::type, pdag> label_pat_map;

  // TODO: broken - doesn't take into account collapsed verts, just throws in the entire hierarchy.


  // NOTE: not all nodes in the dag represent a single vertex.
  // if the hierarchy is partially collapsed, then leaf nodes in the hierarchy can represent multiple verts.



  for (auto v : ginfo.al.verts()) {
    pdag pd;
    auto label = ginfo.labels(v);
    if (subp_verts.count(label) == 0) {
      // if vertex hasn't been picked up by a subpattern, it's a child of root
      if (label_pat_map.count(label) == 0) {
        pd = add_vert();
        label_pat_map.emplace(label, pd);
      } else {
        pd = label_pat_map[label];
      }
      add_edge(root, pd, 1);
    }
  }

  // pd->label = ginfo.labels(v);
  // I(leaf_dims.count(ginfo.labels(v)) > 0);
  // pd->dims = leaf_dims[ginfo.labels(v)];

  // add_edge(root, pd)
}

// TODO: this currently just chooses all leaf nodes and a single pattern
std::unordered_set<Dag::pdag> Dag::select_points() {
  std::unordered_set<pdag> nodes;
  // bool                     found_pat = false;

  /*
  std::function<void(pdag)> select_nodes = [&](pdag pd) {
    if (pd->label == 0 && !found_pat && pd != root) {
      nodes.insert(pd);
      found_pat = true;
    } else if (pd->label > 0) {
      nodes.insert(pd);
    }

    if (pd->is_leaf()) {
      return;
    }

    for (auto child : pd->children) {
      I(child != nullptr);
      select_nodes(child);
    }
  };

  select_nodes(root);
  */

  return nodes;
}

void Dag::dump() {
  std::function<void(pdag)> dump_dag = [&](pdag pd) {
    if (pd == root) {
      fmt::print("root node\n");
    } else {
      fmt::print("dag id {}, parent {}\n", pd->dag_id, (pd->parent == root) ? "root" : std::to_string(pd->parent->dag_id));
    }

    if (pd->children.size() > 0) {
      fmt::print("  children: ");
      for (auto child : pd->children) {
        I(child != nullptr);
        fmt::print("{}, ", child->dag_id);
      }
      fmt::print("\n");
    }

    // for (auto lout : pd->dims) {
    // fmt::print("  dim: ({:.2f}, {:.2f}), loc ({:.2f}, {:.2f})\n", lout.width, lout.height, lout.xpos, lout.ypos);
    //}

    if (pd->is_leaf()) {
      return;
    }

    for (auto child : pd->children) {
      I(child != nullptr);
      dump_dag(child);
    }
  };

  dump_dag(root);
}