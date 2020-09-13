#include "dag.hpp"

#include <unordered_set>

Dag::Dag() : g(dag_t()), labels(g.vert_map<Lg_type_id::type>()), root(g.insert_vert()) {}

void Dag::make(pattern_vec_t pv, const Graph_info<g_type>& ginfo) {
  // hacky pattern_t -> Vert map since we can't hash verts directly
  // map is pv[i] (pattern_t) -> pat_v_map[i] (vert)
  std::vector<dag_t::Vert> pat_v_map(pv.size());

  // need a set that is valid across graph types
  auto subp_verts = std::unordered_multiset<Lg_type_id::type>();

  for (size_t i = 0; i < pv.size(); i++) {
    pat_v_map[i] = g.insert_vert();
    // pattern verts have a label of 0
  }

  for (size_t i = 0; i < pv.size(); i++) {
    auto pat      = pv[i];
    auto parent_v = pat_v_map[i];

    // going in reverse because we want the largest subset, not the smallest
    for (int j = i - 1; j >= 0; j--) {
      auto subpat = pv[j];

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
          g.insert_edge(parent_v, pat_v_map[j]);
          j++;  // run the same check over again to see if we can match another subpattern of the same type
        }
        pat_v_map[i] = parent_v;
      }
    }

    // anything not matched by a subpattern is a leaf node of the pattern
    for (auto pair : pat) {
      dag_t::Vert leaf_v;
      if (pair.second > 0) {
        leaf_v = g.insert_vert();
        subp_verts.insert(pair.first);
        labels[leaf_v] = pair.first;
      }

      for (size_t k = 0; k < pair.second; k++) {
        g.insert_edge(leaf_v, parent_v);
      }
    }
  }

  // any subpatterns that aren't hit by other subpatterns are children of root
  for (auto v : g.verts() | ranges::view::drop(1)) {
    if (g.out_degree(v) == 0) {
      g.insert_edge(v, root);
    }
  }

  // any vertices that aren't hit by subpatterns are also children of root
  for (auto v : ginfo.al.verts()) {
    if (subp_verts.find(ginfo.labels(v)) == subp_verts.end()) {
      auto new_v    = g.insert_vert();
      labels[new_v] = ginfo.labels(v);
      g.insert_edge(new_v, root);
    }
  }
}

void Dag::dump() {
  using namespace graph::attributes;
  std::cout << g.dot_format("label"_of_vert = labels) << std::endl;
}