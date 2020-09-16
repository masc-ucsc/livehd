#include "dag.hpp"

#include <functional>
#include <string>  // for std::to_string
#include <unordered_set>

#include "fmt/core.h"

void Dag::init(std::vector<Pattern> hier_patterns, const Graph_info<g_type>& ginfo) {
  // need to keep track of all the verts we've added so we can add them to the dag as leaves if required
  auto subp_verts = std::unordered_multiset<Lg_type_id::type>();

  for (auto pat : hier_patterns) {
    pat_dag_map.emplace(pat, std::make_shared<Dag_node>());
  }

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
          pat_dag_map[hier_patterns[j]]->parent = pat_dag_p;
          pat_dag_p->children.push_back(pat_dag_map[hier_patterns[j]]);
          j++;  // run the same check over again to see if we can match another subpattern of the same type
        }
      }
    }

    // anything not matched by a subpattern is a leaf node of the pattern
    for (auto pair : pat) {
      if (pair.second > 0) {
        pdag pd = std::make_shared<Dag_node>();
        subp_verts.insert(pair.first);
        // pd->label = pair.first;
        // I(leaf_dims.count(pair.first) > 0);
        // pd->dims = leaf_dims[pair.first];

        for (size_t k = 0; k < pair.second; k++) {
          pd->parent = pat_dag_p;
          pat_dag_p->children.push_back(pd);
        }
      }
    }
  }

  // find subpatterns not owned by other subpatterns, as they should belong to root
  for (auto pd : pat_dag_map) {
    if (pd.second->parent == nullptr) {
      pd.second->parent = root;
      root->children.push_back(pd.second);
    }
  }

  // any vertices that aren't hit by subpatterns are also children of root
  for (auto v : ginfo.al.verts()) {
    if (subp_verts.find(ginfo.labels(v)) == subp_verts.end()) {
      auto pd = std::make_shared<Dag_node>();
      // pd->label = ginfo.labels(v);
      // I(leaf_dims.count(ginfo.labels(v)) > 0);
      // pd->dims = leaf_dims[ginfo.labels(v)];
      pd->parent = root;
      root->children.push_back(pd);
    }
  }
}

// TODO: this currently just chooses all leaf nodes and a single pattern
std::unordered_set<Dag::pdag> Dag::select_points() {
  std::unordered_set<pdag> nodes;
  bool                     found_pat = false;

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
  /*
  std::function<void(pdag)> dump_dag = [&](pdag pd) {
    if (pd == root) {
      fmt::print("root node\n");
    } else {
      fmt::print("label {}, parent {}\n", pd->label, (pd->parent == root) ? "root" : std::to_string(pd->parent->label));
    }

    if (pd->children.size() > 0) {
      fmt::print("  children: ");
      for (auto child : pd->children) {
        I(child != nullptr);
        fmt::print("{}, ", child->label);
      }
      fmt::print("\n");
    }

    for (auto lout : pd->dims) {
      fmt::print("  dim: ({:.2f}, {:.2f}), loc ({:.2f}, {:.2f})\n", lout.width, lout.height, lout.xpos, lout.ypos);
    }

    if (pd->is_leaf()) {
      return;
    }

    for (auto child : pd->children) {
      I(child != nullptr);
      dump_dag(child);
    }
  };

  dump_dag(root);
  */
}