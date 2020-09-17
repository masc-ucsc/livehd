#include "dag.hpp"

#include <functional>
#include <string>  // for std::to_string
#include <unordered_set>

#include "fmt/core.h"

// add an edge from a parent to a child, making sure to keep track of repeating edges
void Dag::add_edge(pdag parent, pdag child, unsigned int count) {
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
}

Dag::pdag Dag::add_pat_vert() {
  auto pd = std::make_shared<Dag_node>(++dag_id_counter);
  return pd;
}

Dag::pdag Dag::add_leaf_vert(const Lg_type_id::type label, const double area) {
  auto pd = std::make_shared<Dag_node>(++dag_id_counter, label, area);
  return pd;
}

void Dag::init(std::vector<Pattern> pattern_sets, const Graph_info<g_type>& gi) {
  // need to keep track of all the verts we've come across so we can add them to the dag as leaves if required
  std::unordered_map<Lg_type_id::type, unsigned int> subp_verts;

  for (auto pat : pattern_sets) {
    auto pd = add_pat_vert();
    pat_dag_map.emplace(pat, pd);
  }

  // check for edges between patterns
  for (size_t i = 0; i < pattern_sets.size(); i++) {
    auto  pat   = pattern_sets[i].verts;
    auto& pat_p = pat_dag_map[pattern_sets[i]];

    // going in reverse because we want the largest subset, not the smallest
    for (int j = i - 1; j >= 0; j--) {
      auto subpat = pattern_sets[j].verts;

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

          add_edge(pat_p, pat_dag_map[pattern_sets[j]], spair.second);
          j++;  // run the same check over again to see if we can match another subpattern of the same type
        }
      }
    }

    // anything not matched by a subpattern is a leaf node of the pattern
    for (auto pair : pat) {
      if (pair.second > 0) {
        Lg_type_id::type label = pair.first;

        pdag pat_leaf;

        // find a node in the graph with the same label and get the area from it
        double area = 0.0;
        for (auto v : gi.al.verts()) {
          if (gi.labels(v) == label) {
            area = gi.areas(v);
          }
        }
        I(area != 0.0);
        
        pat_leaf = add_leaf_vert(label, area);

        subp_verts.emplace(pair);
        add_edge(pat_p, pat_leaf, pair.second);
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

  for (auto v : gi.al.verts()) {
    pdag pd;
    auto label = gi.labels(v);
    if (subp_verts.count(label) == 0) {
      // if vertex hasn't been picked up by a subpattern, it's a child of root
      if (label_pat_map.count(label) == 0) {
        // find the area
        double area = 0.0;
        for (auto av : gi.al.verts()) {
          if (gi.labels(av) == label) {
            area = gi.areas(av);
          }
        }
        I(area != 0.0);

        pd = add_leaf_vert(label, area);

        label_pat_map.emplace(label, pd);
      } else {
        pd = label_pat_map[label];
      }
      add_edge(root, pd, 1);
    }
  }
}

void Dag::dump() {
  std::function<void(pdag)> dump_dag = [&](pdag pd) {
    if (pd == root) {
      fmt::print("root node\n");
    } else {
      fmt::print("dag id {}, parent {}", pd->dag_id, pd->is_root() ? "root" : std::to_string(pd->parent->dag_id));
      if (pd->is_leaf()) {
        fmt::print(", label {}, area {}", pd->dag_label, pd->area);
      }
      fmt::print("\n");
    }

    if (pd->children.size() > 0) {
      fmt::print("children:\n");
      for (size_t i = 0; i < pd->children.size(); i++) {
        I(pd->children[i] != nullptr);
        fmt::print("  id {}, count {}\n", pd->children[i]->dag_id, pd->child_edge_count[i]);
      }
    }

    // for (auto lout : pd->dims) {
    // fmt::print("  dim: ({:.2f}, {:.2f}), loc ({:.2f}, {:.2f})\n", lout.width, lout.height, lout.xpos, lout.ypos);
    //}

    if (pd->is_leaf()) {
      // fmt::print("")
      return;
    }

    for (auto child : pd->children) {
      I(child != nullptr);
      dump_dag(child);
    }
  };

  dump_dag(root);
}