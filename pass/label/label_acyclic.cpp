// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

// #include "pass.hpp"

#define G_DEBUG           0  // toggle for gather_inou debug print
#define O_DEBUG           0  // toggle for oneparent merge debug print
#define M_DEBUG           0  // toggle to get print when merge detected
#define S_DEBUG           0  // toggle for after partition print
#define F_DEBUG           0  // toggle for final partition coloring print
#define CHANGE_NODE_NAMES 0

// Constructor for Label_acyclic
Label_acyclic::Label_acyclic(bool _v, bool _h, uint8_t _c, bool _m) : verbose(_v), hier(_h), merge_en(_m), cutoff(_c) {
  part_id = 1;
}

/* * * * * * *
 Compares absl::flat_hash_set<Node::Compact>'s
 * * * * * * */
bool Label_acyclic::node_set_cmp(NodeSet a, NodeSet b) const {
  if (a.size() != b.size()) {
    return false;
  }
  for (auto &n : a) {
    if (!(b.contains(n))) {
      return false;
    }
  }
  return true;
}

/* * * * * * *
 Compares absl::flat_hash_set<int>'s
 * * * * * * */
bool Label_acyclic::int_set_cmp(IntSet a, IntSet b) const {
  if (a.size() != b.size()) {
    return false;
  }
  for (auto &n : a) {
    if (!(b.contains(n))) {
      return false;
    }
  }
  return true;
}

/* * * * * * *
 Re-writes tgt with contents of ref if they differ, for NodeSet
 * * * * * * */
void Label_acyclic::node_set_write(NodeSet &tgt, NodeSet &ref) {
  if (!(node_set_cmp(tgt, ref))) {
    tgt.clear();
    tgt = ref;
  }
}

/* * * * * * *
 Re-writes tgt with contents of ref if they differ, for IntSet
 * * * * * * */
void Label_acyclic::int_set_write(IntSet &tgt, IntSet &ref) {
  if (!(int_set_cmp(tgt, ref))) {
    tgt.clear();
    tgt = ref;
  }
}

/* * * * * * *
 Loops through an lgraph and grabs all nodes that are potential partition roots
 * * * * * * */
void Label_acyclic::gather_roots(Lgraph *g) {
  // Iterating through outputs of the graphs (0 out edges), all are potential roots
  g->each_graph_output([&](const Node_pin &pin) {
    if (pin.get_node().get_type_op() != Ntype_op::Const) {
      const auto nodec = (pin.get_node()).get_compact();  // Node compact flat
      roots.insert(nodec);                                // Saving roots
      node2id[nodec] = part_id;                           // Saving part ID of nodes
      id2nodes[part_id].insert(nodec);                    // Saving nodes under part IDs
      part_id += 1;
    }
  });

  // Adding potential roots to the root list
  bool add_root = false;
  for (const auto &n : g->forward(hier)) {
    if (n.get_type_op() == Ntype_op::Const) {
      continue;
    }
    if (n.get_num_out_edges() > 1) {
      add_root = true;
    } else if (n.get_num_out_edges() == 0) {
      add_root = true;
    } else if (n.get_num_out_edges() == 1) {
      // If the sink of the one out edge IS an io, add_root
      for (const auto &oe : n.out_edges()) {
        auto sink_node_op = oe.sink.get_node().get_type_op();
        if (sink_node_op == Ntype_op::IO) {  // IO check
          add_root = true;
        }
      }
    }

    if (add_root == true) {
      add_root         = false;
      const auto nodec = n.get_compact();
      roots.insert(nodec);
      node2id[nodec] = part_id;
      id2nodes[part_id].insert(nodec);
      part_id += 1;
    }
  }
}

/* * * * * * *
 Runs through all the potential roots
 tries to grow each partition as much as possible
 * * * * * * */
void Label_acyclic::grow_partitions(Lgraph *g) {
  // Iterating through all the potential roots
  if (roots.empty()) {
    return;
  }

  for (auto &n : roots) {
    if (!node_preds.empty()) {
      node_preds.clear();
    }

    auto curr_id = node2id[n];
    node_preds.push_back(n);  // Adding yourself as a predecessor
    while (node_preds.size() != 0) {
      auto curr_pred = node_preds.back();  // Getting a predecessor to explore
      node_preds.pop_back();

      // Checking the predecessors of curr_pred to add more nodes to explore
      // Get driver of all inp_edges and add to pot list if not already in a Part
      Node temp_n(g, curr_pred);

      if (temp_n.get_type_op() == Ntype_op::Const) {
        continue;
      }

      for (auto &ie : temp_n.inp_edges()) {
        auto pot_pred = ie.driver.get_node();

        if (pot_pred.get_type_op() == Ntype_op::Const) {
          continue;
        }

        auto pot_predc = pot_pred.get_compact();
        // Three conditions for node to be addable
        bool is_not_root    = !(roots.contains(pot_predc));
        bool is_not_labeled = !(node2id.contains(pot_predc));
        bool is_not_io      = (pot_pred.get_type_op() != Ntype_op::IO);

        if (is_not_root && is_not_labeled && is_not_io) {
          node2id[pot_predc] = curr_id;
          node_preds.push_back(pot_predc);
        }
      }  // END of inp_edge iteration for loop
    }    // END of node_preds clearing while loop
  }      // END of root iteration for loop
}

/* * * * * * *
 Run through the nodes in curr_id_nodes
 Gather all the ins and outs of those nodes
 Then, use this info to populate/overwrite id2inc, id2out, id2incparts, id2outparts
 * * * * * * */
void Label_acyclic::gather_inou(Lgraph *g) {
  IntSet  common_int1;   // use wherever an IntSet is needed
  IntSet  common_int2;   // use wherever an IntSet is needed
  NodeSet common_node1;  // use wherever an NodeSet is needed
  NodeSet common_node2;  // use wherever an NodeSet is needed

  for (auto &it : id2nodes) {
    auto curr_id       = it.first;
    auto curr_id_nodes = it.second;

#if G_DEBUG
    std::cout << std::format("curr_id: {}\n", curr_id);
#endif

    common_node1.clear();  // for id2out
    common_node2.clear();  // for id2inc
    common_int1.clear();   // for id2outparts
    common_int2.clear();   // for id2incparts

    for (auto &n : curr_id_nodes) {
      Node tmp_n(g, n);

      if (tmp_n.get_type_op() == Ntype_op::Const) {
        continue;
      }
      // gather the sinks for id2out and id2outparts
      for (auto &e : tmp_n.out_edges()) {
        auto spin        = e.sink;
        auto dpin        = e.driver;
        auto snode       = spin.get_node();
        auto dnode       = dpin.get_node();
        auto outgoing_id = node2id[snode.get_compact()];
        auto this_id     = node2id[dnode.get_compact()];

        if (snode.get_type_op() == Ntype_op::Const) {
          continue;
        }
        if (snode.get_type_op() != Ntype_op::IO) {
          if ((curr_id != outgoing_id) && (curr_id == this_id)) {
            common_node1.insert(snode.get_compact());
            common_int1.insert(outgoing_id);
          }
        }
      }

      // gather the drivers, put in id2inc and id2incparts
      for (auto &e : tmp_n.inp_edges()) {
        auto spin        = e.sink;
        auto dpin        = e.driver;
        auto snode       = spin.get_node();
        auto dnode       = dpin.get_node();
        auto incoming_id = node2id[dnode.get_compact()];
        auto this_id     = node2id[snode.get_compact()];

        if (dnode.get_type_op() == Ntype_op::Const) {
          continue;
        }
        if (dnode.get_type_op() != Ntype_op::IO) {
          if ((curr_id != incoming_id) && (curr_id == this_id)) {
            common_node2.insert(dnode.get_compact());
            common_int2.insert(incoming_id);
          }
        }
      }
    }  // END of for loop through all nodes in a part_id

    node_set_write(id2out[curr_id], common_node1);
    node_set_write(id2inc[curr_id], common_node2);
    int_set_write(id2outparts[curr_id], common_int1);
    int_set_write(id2incparts[curr_id], common_int2);

#if G_DEBUG
    std::cout << "common_node1:\n";
    for (auto &n : common_node1) {
      Node some_n(g, n);
      std::cout << std::format("  {}\n", some_n.debug_name());
    }

    std::cout << "common_int1:\n";
    for (auto &n : common_int1) {
      std::cout << std::format("  {}\n", n);
    }
    std::cout << "common_node2:\n";
    for (auto &n : common_node2) {
      Node some_n(g, n);
      std::cout << std::format("  {}\n", some_n.debug_name());
    }

    std::cout << "common_int2:\n";
    for (auto &n : common_int2) {
      std::cout << std::format("  {}\n", n);
    }
#endif

  }  // END of for loop going through all part_ids
}

/* * * * * * *
 The actual merging of two partitions
 * * * * * * */
void Label_acyclic::merge_op(int merge_from, int merge_into) {
  // Alter node2id->Replace all merge_from ids with merge_into
  for (auto &it : node2id) {
    if (it.second == merge_from) {
      node2id[it.first] = merge_into;
    }
  }

  // Alter id2nodes->merge id2nodes and erase merge_from
  id2nodes[merge_into].merge(id2nodes[merge_from]);
  id2nodes.erase(merge_from);

  // Alter id2inc->merge id2inc and id2incparts and erase merge_from
  bool merge_from_has_inc = id2inc.contains(merge_from);
  bool merge_into_has_inc = id2inc.contains(merge_into);
  if (merge_from_has_inc || merge_into_has_inc) {
    id2inc[merge_into].merge(id2inc[merge_from]);
    id2inc.erase(merge_from);
    id2incparts[merge_into].erase(merge_from);
    id2incparts[merge_into].merge(id2incparts[merge_from]);
    id2incparts.erase(merge_from);
  }

  // Alter id2out->merge_from -> merge_into, erase merge_from
  bool merge_from_has_out = id2out.contains(merge_from);
  bool merge_into_has_out = id2out.contains(merge_into);
  if (merge_from_has_out || merge_into_has_out) {
    id2out[merge_into].merge(id2out[merge_from]);
    id2out.erase(merge_from);
    id2outparts[merge_into].erase(merge_from);
    id2outparts[merge_into].merge(id2outparts[merge_from]);
    id2outparts.erase(merge_from);
  }
}

/* * * * * * *
 Goes through all the current partitions
 Merge if parents are same and at least one is within cutoff
 * * * * * * */
void Label_acyclic::merge_partitions_same_parents() {
  // Use part_id to generate Partition lists
  std::vector<int> pwi;    // Partitions with incoming
  std::vector<int> pwo;    // partitions with outgoing
  std::vector<int> parts;  // Partitions

  // Populate vectors with Part ids
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) {
      pwi.push_back(i);
    }
    if (id2out.contains(i)) {
      pwo.push_back(i);
    }
    parts.push_back(i);
  }

  auto pivot      = pwi.begin();  // increment till pwi.end()
  bool merge_flag = true;
  bool keep_going = true;
  int  merge_into = -1;  // The partition that will grow
  int  merge_from = -1;  // The partition that will be eaten

  while (keep_going) {
    // reset the flags and ids
#if M_DEBUG
    std::cout << std::format("Keep Going, current pivot:{}\n", *pivot);
#endif

    merge_flag = false;
    keep_going = false;
    merge_into = -1;
    merge_from = -1;

    // Iterate through pwi to check which are the same
    for (auto i = pwi.begin(); i != pwi.end(); ++i) {
      // Merge condition: same parents and small partition cutoff matches
      if ((i != pivot) && (node_set_cmp(id2inc[*i], id2inc[*pivot]))) {
        auto pivot_part_size = (id2nodes[*pivot]).size();
        auto i_part_size     = (id2nodes[*i]).size();
        // only merge if one of the parts has incoming nodes
        if ((id2inc[*i].size() != 0) || (id2inc[*pivot].size() != 0)) {
          // At least one of the partitions has to be a small partition
          if (pivot_part_size <= cutoff || i_part_size <= cutoff) {
            merge_flag = true;
            keep_going = true;
            merge_into = *pivot;
            merge_from = *i;
            break;  // Do merge outside the loop
          }
        }
      }
    }

    if (merge_flag) {
#if M_DEBUG
      std::cout << std::format("Merge Detected, {} -> {}\n", merge_from, merge_into);
#endif
      merge_op(merge_from, merge_into);

      // Removing merge_from from pwi, pwo, and parts for merge re-scan
      auto parts_rm_iter = std::find(parts.begin(), parts.end(), merge_from);
      auto pwi_rm_iter   = std::find(pwi.begin(), pwi.end(), merge_from);
      auto pwo_rm_iter   = std::find(pwo.begin(), pwo.end(), merge_from);

      if (parts_rm_iter != parts.end()) {
        parts.erase(parts_rm_iter);
      }
      if (pwi_rm_iter != pwi.end()) {
        pwi.erase(pwi_rm_iter);
      }
      if (pwo_rm_iter != pwo.end()) {
        pwo.erase(pwo_rm_iter);
      }

      pivot = pwi.begin();  // reset pivot to begin() for merge re-scan
    } else {
#if M_DEBUG
      std::cout << "Before checking pivot\n";
#endif
      // No merge possible, check if pivot can be changed
      if ((pivot + 1) != pwi.end()) {
#if M_DEBUG
        std::cout << std::format("moving pivot old:{}, new:{}\n", *pivot, *(pivot + 1));
#endif
        ++pivot;            // change pivot
        keep_going = true;  // keep scanning for merges
      }
    }
  }
}

void Label_acyclic::merge_partitions_one_parent() {
  // Use part_id to generate Partition lists
  std::vector<int> pwi;    // Partitions with incoming
  std::vector<int> pwo;    // partitions with outgoing
  std::vector<int> parts;  // Partitions

  // Populate vectors with Part ids
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) {
      pwi.push_back(i);
    }
    if (id2out.contains(i)) {
      pwo.push_back(i);
    }
    parts.push_back(i);
  }

  auto pivot      = pwi.begin();  // increment till pwi.end()
  bool merge_flag = true;
  bool keep_going = true;
  int  merge_into = -1;  // The partition that will grow
  int  merge_from = -1;  // The partition that will be eaten

  while (keep_going) {
#if O_DEBUG
    std::cout << std::format("Keep Going, current pivot:{}\n", *pivot);
#endif

    // reset the flags and ids
    merge_flag = false;
    keep_going = false;
    merge_into = -1;
    merge_from = -1;

    // Iterate through pwi to check which are the same
    for (auto i = pwi.begin(); i != pwi.end(); ++i) {
#if O_DEBUG
      std::cout << std::format("id2incparts[{}]\n", *pivot);
      for (auto &o : id2incparts[*pivot]) {
        std::cout << std::format("{}\n", o);
      }
#endif

      if ((i == pivot) && (id2incparts[*pivot].size() == 1)) {
        merge_flag = true;
        keep_going = true;
        merge_into = *pivot;
        for (auto &o : id2incparts[*pivot]) {
          merge_from = o;
          break;
        }
        break;  // Do merge outside the loop
      }
    }

    if (merge_flag) {
#if O_DEBUG
      std::cout << std::format("Merge Detected, {} -> {}\n", merge_from, merge_into);
#endif
      merge_op(merge_from, merge_into);

      // Removing merge_from from pwi, pwo, and parts for merge re-scan
      auto parts_rm_iter = std::find(parts.begin(), parts.end(), merge_from);
      auto pwi_rm_iter   = std::find(pwi.begin(), pwi.end(), merge_from);
      auto pwo_rm_iter   = std::find(pwo.begin(), pwo.end(), merge_from);

      if (parts_rm_iter != parts.end()) {
        parts.erase(parts_rm_iter);
      }
      if (pwi_rm_iter != pwi.end()) {
        pwi.erase(pwi_rm_iter);
      }
      if (pwo_rm_iter != pwo.end()) {
        pwo.erase(pwo_rm_iter);
      }

      pivot = pwi.begin();
    } else {
#if O_DEBUG
      std::cout << "Before checking pivot\n";
#endif
      // No merge possible, check if pivot can be changed
      if ((pivot + 1) != pwi.end()) {
#if O_DEBUG
        std::cout << std::format("moving pivot old:{}, new:{}\n", *pivot, *(pivot + 1));
#endif
        ++pivot;            // change pivot
        keep_going = true;  // keep scanning for merges
      }
    }
  }
}

// dump()
void Label_acyclic::dump(Lgraph *g) const {
  std::cout << "---- Label_acyclic dump ----\n";

  // Internal Nodes printing
  int node_tracker = 0;

  for (auto n : g->forward(hier)) {
    std::cout << std::format("Node: {}", n.debug_name());
    if (n.has_color()) {
      std::cout << std::format(", Node Color: {}\n", n.get_color());
    } else {
      std::cout << "\n";
    }
    node_tracker++;
  }
  std::cout << std::format("Found {} nodes using g->forward(hier)\n", node_tracker);

  std::cout << "=== id2inc ===\n";
  for (auto &it : id2inc) {
    std::cout << std::format("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      std::cout << std::format("    {}\n", node.debug_name());
    }
  }

  std::cout << "=== id2out ===\n";
  for (auto &it : id2out) {
    std::cout << std::format("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      std::cout << std::format("    {}\n", node.debug_name());
    }
  }

  std::cout << "=== Roots ===\n";
  for (auto &it : roots) {
    Node n(g, it);
    std::cout << std::format("    {}\n", n.debug_name());
  }

  std::cout << "=== node2id ===\n";
  for (auto &it : node2id) {
    Node n(g, it.first);
    std::cout << std::format("    {}, ID: {}\n", n.debug_name(), it.second);
  }
  std::cout << "---- fin ----\n";
}

void Label_acyclic::label(Lgraph *g) {
  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { lg->ref_node_color_map()->clear(); });
  }
  g->ref_node_color_map()->clear();

  gather_roots(g);
  grow_partitions(g);
  gather_inou(g);

  if (merge_en) {
    merge_partitions_same_parents();
    gather_inou(g);
    merge_partitions_one_parent();
    gather_inou(g);
  }

#if F_DEBUG
  for (auto n : g->forward(hier)) {
    if (node2id.find(n.get_compact()) != node2id.end()) {
      std::cout << std::format("Found {} in node2id\n", n.debug_name());
    } else {
      std::cout << std::format("Not found {} in node2id\n", n.debug_name());
    }
  }
#endif

  // Actual Labeling happens here:
  for (auto n : g->fast(hier)) {
    auto nc = n.get_compact();
    if (node2id.contains(nc)) {
      n.set_color(node2id[nc]);
#if CHANGE_NODE_NAMES
      n.set_name(std::string(std::format("ACYCPART{}", node2id[n.get_compact()])));
#endif
    } else {
      n.set_color(NO_COLOR);
    }
  }

  if (verbose) {
    dump(g);
  }
}
