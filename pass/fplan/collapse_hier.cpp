#include <functional>

#include "hier_tree.hpp"
#include "profile_time.hpp"

Hier_tree::phier Hier_tree::make_hier_tree(Graph_info<g_type>& gi, phier t1, phier t2) {
  auto pnode  = std::make_shared<Hier_node>(gi);
  pnode->name = "hnode_" + std::to_string(unique_node_counter);

  pnode->children[0] = t1;
  t1->parent         = pnode;

  pnode->children[1] = t2;
  t2->parent         = pnode;

  unique_node_counter++;

  return pnode;
}

Hier_tree::phier Hier_tree::make_hier_node(Graph_info<g_type>& gi, const set_t& s) {
  I(s.size() > 0);

  phier pnode = std::make_shared<Hier_node>(gi);
  pnode->name = "hleaf_" + std::to_string(++unique_node_counter);

  for (auto v : s) {
    pnode->area += gi.areas(v);
  }

  for (auto v : s) {
    pnode->graph_set.insert(v);
  }

  return pnode;
}

// create a new hierarchy tree from the unmodified hierarchy tree
// invariant: the old and new graphs have to be exactly the same (same verts, vert maps, and edges)
Hier_tree::phier Hier_tree::dup_tree(phier oldn, Graph_info<g_type>& new_gi) {
  // quick (non-exhaustive) checks that the graphs are the same
  I(collapsed_gis[0].al.order() == new_gi.al.order());
  I(collapsed_gis[0].al.size() == new_gi.al.size());

  if (oldn->is_leaf()) {
    // record ids of old verts and use ids to get an equivalent set to new verts
    std::unordered_set<unsigned long> old_ids;
    for (auto v : oldn->graph_set) {
      old_ids.insert(collapsed_gis[0].ids(v));
    }

    set_t new_vert_set = new_gi.al.vert_set();
    for (auto v : new_gi.al.verts()) {
      if (old_ids.count(new_gi.ids(v)) > 0) {
        new_vert_set.insert(v);
      }
    }

    return make_hier_node(new_gi, new_vert_set);
  }

  I(oldn->children[0] != nullptr);
  I(oldn->children[1] != nullptr);

  // I believe the HiReg paper states that child nodes can have layouts less than the thresold,
  // as long as the total area between the child nodes is greater than the threshold
  auto n1 = dup_tree(oldn->children[0], new_gi);
  auto n2 = dup_tree(oldn->children[1], new_gi);

  return make_hier_tree(new_gi, n1, n2);
}

Hier_tree::phier Hier_tree::collapse(phier node, Graph_info<g_type>& gi, double threshold_area) {
  if (find_area(node) > threshold_area) {
    if (node->is_leaf()) {
      return make_hier_node(gi, node->graph_set);
    }

    auto n1 = collapse(node->children[0], gi, threshold_area);
    auto n2 = collapse(node->children[1], gi, threshold_area);

    I(node->children[0] != nullptr);
    I(node->children[1] != nullptr);

    return make_hier_tree(gi, n1, n2);
  }

  auto collapse_set = gi.al.vert_set();

  std::function<void(phier)> get_subtree_nodes = [&](phier rnode) {
    if (rnode->is_leaf()) {
      for (auto v : rnode->graph_set) {
        collapse_set.insert(v);
      }
    } else {
      I(rnode->children[0] != nullptr);
      I(rnode->children[1] != nullptr);

      get_subtree_nodes(rnode->children[0]);
      get_subtree_nodes(rnode->children[1]);
    }
  };

  get_subtree_nodes(node);

  vertex_t collapsed_v = gi.make_set_vertex("cp_vert_", collapse_set);
  if (collapse_set.size() > 1) {
    gi.erase_set_verts(collapse_set);
  }

  node->name = std::string("hleaf_").append(gi.debug_names(collapsed_v));
  node->area = gi.areas(collapsed_v);
  node->graph_set.insert(collapsed_v);

  // delete child nodes once everything is moved over
  // TODO: should this be a recursive resetting?
  node->children[0].reset();
  node->children[1].reset();

  return node;
}

void Hier_tree::collapse(const size_t num_chiers, const double threshold_area) {
  I(threshold_area >= 0.0);
  I(hiers.size() == 1);
  I(collapsed_gis.size() == 1);

  profile_time::timer t;

  fmt::print("    allocating collapsed hierarchies...");
  t.start();
  hiers.resize(num_chiers + 1);
  for (size_t i = 1; i <= num_chiers; i++) {
    collapsed_gis.emplace_back(collapsed_gis[0]);
    hiers[i] = dup_tree(hiers[0], collapsed_gis[i]);
  }
  fmt::print("done ({} ms).\n", t.time());

  for (size_t i = 0; i <= num_chiers; i++) {
    fmt::print("    collapsing hierarchy {} (min area: {})...", i, i * threshold_area);
    t.start();
    hiers[i] = collapse(hiers[i], collapsed_gis[i], i * threshold_area);
    fmt::print("done ({} ms).\n", t.time());
  }

  // clean up edges in the collapsed graph - if there's >1 edge between two nodes,
  // add up the weights of the edges and remove all but one of em
  fmt::print("    cleaning up unused edges...");
  t.start();
  for (size_t i = 0; i <= num_chiers; i++) {
    auto& gi     = collapsed_gis[i];
    auto  marked = gi.al.edge_map<bool>();

    for (auto v : gi.al.verts()) {
      // map of connected verts -> edge connecting v and other vert
      auto visited_verts = gi.al.vert_map<edge_t>(gi.al.null_edge());

      for (auto it = gi.al.out_edges(v).begin(); it != gi.al.out_edges(v).end(); it++) {
        auto ov = gi.al.head(*it);
        if (visited_verts[ov] != gi.al.null_edge()) {
          gi.weights[visited_verts[ov]] += gi.weights(*it);
          marked[*it] = true;
        } else {
          visited_verts[ov] = *it;
        }
      }
    }

    for (auto e : gi.al.edges()) {
      if (marked[e]) {
        gi.al.erase_edge(e);
      }
    }
  }

  fmt::print("done ({} ms).\n", t.time());
}