#include "dag.hpp"

#include <functional>
#include <unordered_set>

Dag::Dag() : g(d_type()), dims(g.vert_map<Dim>()), root(g.insert_vert()), labels(g.vert_map<Lg_type_id::type>()) {}

void Dag::init(std::vector<Pattern> hier_patterns, std::unordered_map<Lg_type_id::type, Dim> leaf_dims,
               const Graph_info<g_type>& ginfo) {
  // Pattern -> vert map since we can't hash verts directly
  std::vector<d_type::Vert> pat_v_map(hier_patterns.size());

  // need a set that is valid across graph types
  auto subp_verts = std::unordered_multiset<Lg_type_id::type>();

  for (size_t i = 0; i < hier_patterns.size(); i++) {
    auto new_v   = g.insert_vert();
    pat_v_map[i] = new_v;
    dims[new_v]  = hier_patterns[i].d;
    // Pattern verts have a label of 0
  }

  for (size_t i = 0; i < hier_patterns.size(); i++) {
    auto pat      = hier_patterns[i].verts;
    auto parent_v = pat_v_map[i];

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
          g.insert_edge(parent_v, pat_v_map[j]);
          j++;  // run the same check over again to see if we can match another subpattern of the same type
        }
        pat_v_map[i] = parent_v;
      }
    }

    // anything not matched by a subpattern is a leaf node of the pattern
    for (auto pair : pat) {
      d_type::Vert leaf_v;
      if (pair.second > 0) {
        leaf_v = g.insert_vert();
        subp_verts.insert(pair.first);
        labels[leaf_v] = pair.first;
        I(leaf_dims.count(pair.first) > 0);
        dims[leaf_v] = leaf_dims[pair.first];
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
      I(leaf_dims.count(ginfo.labels(v)) > 0);
      dims[new_v] = leaf_dims[ginfo.labels(v)];
      g.insert_edge(new_v, root);
    }
  }
}