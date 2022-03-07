// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

#include "cell.hpp"
#include "pass.hpp"

//#define G_DEBUG 1  // toggle for gather_inou debug print
#define O_DEBUG 1  // toggle for oneparent merge debug print
//#define M_DEBUG 1  // toggle to get print when merge detected
//#define S_DEBUG 1  // toggle for after partition print
//#define F_DEBUG 1  // toggle for final partition coloring print

// Constructor for Label_acyclic
Label_acyclic::Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff, bool _merge_en) : verbose(_verbose), hier(_hier), merge_en(_merge_en), cutoff(_cutoff) {
  part_id = 0;  
}

// dump()
void Label_acyclic::dump(Lgraph *g) const {
  fmt::print("/---------------/\n");
  fmt::print("Label_acyclic dump:\n");
  
  // Internal Nodes printing 
  int node_tracker = 0;
  
  for (auto n : g->forward(hier)) {
    fmt::print("Node: {} ,", n.debug_name());
    if (n.has_color()) fmt::print("Node Color: {}\n", n.get_color());
    node_tracker++;
  } 
  fmt::print("Found {} nodes using g->forward(hier)\n", node_tracker);
  
  fmt::print("=== id2inc ===\n");
  for (auto &it : id2inc) {
    fmt::print("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      fmt::print("    {}\n", node.debug_name());
    }
  }
  
  fmt::print("=== id2out ===\n");
  for (auto &it : id2out) {
    fmt::print("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      fmt::print("    {}\n", node.debug_name());
    }
  }
  
  fmt::print("=== Roots ===\n");
  for (auto &it : roots) {
    Node n(g, it);
    fmt::print("    {}\n", n.debug_name());
  }

  fmt::print("=== node2id ===\n");
  for (auto &it : node2id) {
    Node n(g, it.first);
    fmt::print("    {}, ID: {}\n", n.debug_name(), it.second);
  }
  fmt::print("/---------------/\n");
}


/* * * * * * *  
 Compares absl::flat_hash_set<Node::Compact>'s
 * * * * * * */
bool Label_acyclic::node_set_cmp(NodeSet a, NodeSet b) const {
  if (a.size() != b.size()) return false;  
  for (auto &n : a) {
    if (!(b.contains(n))) return false;
  }
  return true;
}

/* * * * * * *  
 Compares absl::flat_hash_set<int>'s
 * * * * * * */
bool Label_acyclic::int_set_cmp(IntSet a, IntSet b) const {
  if (a.size() != b.size()) return false;
  for (auto &n : a) {
    if (!(b.contains(n))) return false;
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
    const auto nodec = (pin.get_node()).get_compact();  // Node compact flat
    roots.insert(nodec);             // Saving roots
    node2id[nodec] = part_id;        // Saving part ID of nodes
    id2nodes[part_id].insert(nodec); // Saving nodes under part IDs
    part_id+=1;                       
  });

  // Adding potential roots to the root list
  bool add_root = false;  
  for (const auto &n : g->forward(hier)) {
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
      add_root = false;
      const auto nodec = n.get_compact();
      roots.insert(nodec); 
      node2id[nodec] = part_id;
      id2nodes[part_id].insert(nodec);
      part_id+=1;    
    }
  } 
}


/* * * * * * *  
 Runs through all the potential roots 
 tries to grow each partition as much as possible
 * * * * * * */
void Label_acyclic::grow_partitions(Lgraph *g) {
  // Iterating through all the potential roots
  if (roots.empty()) return; 

  for (auto &n : roots) {
    if (!node_preds.empty()) node_preds.clear(); 

    auto curr_id = node2id[n];
    node_preds.push_back(n);              // Adding yourself as a predecessor
    while (node_preds.size() != 0) {
      auto curr_pred = node_preds.back(); // Getting a predecessor to explore
      node_preds.pop_back();              

      // Checking the predecessors of curr_pred to add more nodes to explore
      // Get driver of all inp_edges and add to pot list if not already in a Part
      Node temp_n(g, curr_pred);
      for (auto &ie : temp_n.inp_edges()) { 
        auto pot_pred = ie.driver.get_node();
        auto pot_predc = pot_pred.get_compact();
       
        // Three conditions for node to be addable
        bool is_not_root = !(roots.contains(pot_predc));
        bool is_not_labeled = !(node2id.contains(pot_predc));
        bool is_not_io = (pot_pred.get_type_op() != Ntype_op::IO);
        
        if (is_not_root && is_not_labeled && is_not_io) {
          node2id[pot_predc] = curr_id;
          node_preds.push_back(pot_predc);
        }
      } // END of inp_edge iteration for loop
    } // END of node_preds clearing while loop 
  } // END of root iteration for loop
}


/* * * * * * *  
 Run through the nodes in curr_id_nodes
 Gather all the ins and outs of those nodes
 Then, use this info to populate/overwrite id2inc, id2out, id2incparts, id2outparts
 * * * * * * */
void Label_acyclic::gather_inou(Lgraph *g) {
  for (auto &it : id2nodes) {
    auto curr_id = it.first;
    auto curr_id_nodes = it.second;

#ifdef G_DEBUG    
    fmt::print("curr_id: {}\n", curr_id);
#endif   

    common_node1.clear(); // for id2out
    common_node2.clear(); // for id2inc
    common_int1.clear();  // for id2outparts
    common_int2.clear();  // for id2incparts
    
    for (auto &n : curr_id_nodes) {
      Node tmp_n(g, n);
      
      // gather the sinks for id2out and id2outparts
      for (auto &e : tmp_n.out_edges()) {
        auto spin = e.sink;
        auto dpin = e.driver; 
        auto snode = spin.get_node();
        auto dnode = dpin.get_node();
        auto outgoing_id = node2id[snode.get_compact()];
        auto this_id = node2id[dnode.get_compact()];
        
        if (snode.get_type_op() != Ntype_op::IO) { 
          if ((curr_id != outgoing_id) && (curr_id == this_id)) {
            common_node1.insert(snode.get_compact());
            common_int1.insert(outgoing_id);
          }
        }
      }
       
      // gather the drivers, put in id2inc and id2incparts
      for (auto &e : tmp_n.inp_edges()) {
        auto spin = e.sink;
        auto dpin = e.driver; 
        auto snode = spin.get_node();
        auto dnode = dpin.get_node();
        auto incoming_id = node2id[dnode.get_compact()]; 
        auto this_id = node2id[snode.get_compact()];
        
        if (dnode.get_type_op() != Ntype_op::IO) {
          if ((curr_id != incoming_id) && (curr_id == this_id)) {
            common_node2.insert(dnode.get_compact());
            common_int2.insert(incoming_id); 
          }
        }
      }     
    } // END of for loop through all nodes in a part_id

#ifdef G_DEBUG
    fmt::print("common_node1:\n");
    for (auto &n : common_node1) {
      Node some_n(g, n);
      fmt::print("  {}\n", some_n.debug_name());
    }

    fmt::print("common_int1:\n");
    for (auto &n : common_int1) {
      fmt::print("  {}\n", n);
    }
    fmt::print("common_node2:\n");
    for (auto &n : common_node2) {
      Node some_n(g, n);
      fmt::print("  {}\n", some_n.debug_name());
    }

    fmt::print("common_int2:\n");
    for (auto &n : common_int2) {
      fmt::print("  {}\n", n);
    }
#endif
    
    node_set_write(id2out[curr_id], common_node1); 
    node_set_write(id2inc[curr_id], common_node2); 
    int_set_write(id2outparts[curr_id], common_int1); 
    int_set_write(id2incparts[curr_id], common_int2); 
  } // END of for loop going through all part_ids
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
  std::vector<int> pwi;     // Partitions with incoming
  std::vector<int> pwo;     // partitions with outgoing
  std::vector<int> parts;   // Partitions
  
  // Populate vectors with Part ids
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) pwi.push_back(i);
    if (id2out.contains(i)) pwo.push_back(i);
    parts.push_back(i);
  }

  auto pivot = pwi.begin();  // increment till pwi.end()
  bool merge_flag = true;
  bool keep_going = true;
  int merge_into = -1;      // The partition that will grow
  int merge_from = -1;      // The partition that will be eaten

  while (keep_going) {
    // reset the flags and ids
#ifdef M_DEBUG
    fmt::print("Keep Going, current pivot:{}\n", *pivot);
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
        auto i_part_size = (id2nodes[*i]).size();
        // only merge if one of the parts has incoming nodes
        if ((id2inc[*i].size() != 0) || (id2inc[*pivot].size() != 0)) {
          // At least one of the partitions has to be a small partition
          if (pivot_part_size <= cutoff || i_part_size <= cutoff) { 
            merge_flag = true;
            keep_going = true;
            merge_into = *pivot;
            merge_from = *i;
            break;    // Do merge outside the loop
          }
        }
      }
    }

    if (merge_flag) {     
#ifdef M_DEBUG
      fmt::print("Merge Detected, {} -> {}\n", merge_from, merge_into); 
#endif
      merge_op(merge_from, merge_into);
      
      // Removing merge_from from pwi, pwo, and parts for merge re-scan
      auto parts_rm_iter = std::find(parts.begin(), parts.end(), merge_from);
      auto pwi_rm_iter = std::find(pwi.begin(), pwi.end(), merge_from);
      auto pwo_rm_iter = std::find(pwo.begin(), pwo.end(), merge_from);
      
      if (parts_rm_iter != parts.end()) parts.erase(parts_rm_iter);
      if (pwi_rm_iter != pwi.end()) pwi.erase(pwi_rm_iter);
      if (pwo_rm_iter != pwo.end()) pwo.erase(pwo_rm_iter);

      pivot = pwi.begin(); // reset pivot to begin() for merge re-scan
    } else {
#ifdef M_DEBUG
      fmt::print("Before checking pivot\n");
#endif
      // No merge possible, check if pivot can be changed
      if ((pivot+1) != pwi.end()) {
#ifdef M_DEBUG
        fmt::print("moving pivot old:{}, new:{}\n", *pivot, *(pivot+1));
#endif
        ++pivot;            // change pivot
        keep_going = true;  // keep scanning for merges
      }
    }
  }
}


void Label_acyclic::merge_partitions_one_parent() { 
  
  fmt::print("oneparent\n");

  // Use part_id to generate Partition lists
  std::vector<int> pwi;     // Partitions with incoming
  std::vector<int> pwo;     // partitions with outgoing
  std::vector<int> parts;   // Partitions
  
  // Populate vectors with Part ids
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) pwi.push_back(i);
    if (id2out.contains(i)) pwo.push_back(i);
    parts.push_back(i);
  }

  auto pivot = pwi.begin();  // increment till pwi.end()
  bool merge_flag = true;
  bool keep_going = true;
  int merge_into = -1;      // The partition that will grow
  int merge_from = -1;      // The partition that will be eaten

  while (keep_going) {
#ifdef O_DEBUG
    fmt::print("Keep Going, current pivot:{}\n", *pivot);
#endif

    // reset the flags and ids
    merge_flag = false;
    keep_going = false;
    merge_into = -1;
    merge_from = -1;

    // Iterate through pwi to check which are the same
    for (auto i = pwi.begin(); i != pwi.end(); ++i) { 
#ifdef O_DEBUG
      fmt::print("id2incparts[{}]\n", *pivot);
      for (auto &o : id2incparts[*pivot]) {
        fmt::print("{}\n", o);
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
        break;    // Do merge outside the loop 
      }
    }

    if (merge_flag) {     
#ifdef O_DEBUG
      fmt::print("Merge Detected, {} -> {}\n", merge_from, merge_into); 
#endif
      merge_op(merge_from, merge_into);

      // Removing merge_from from pwi, pwo, and parts for merge re-scan
      auto parts_rm_iter = std::find(parts.begin(), parts.end(), merge_from);
      auto pwi_rm_iter = std::find(pwi.begin(), pwi.end(), merge_from);
      auto pwo_rm_iter = std::find(pwo.begin(), pwo.end(), merge_from);
      
      if (parts_rm_iter != parts.end()) parts.erase(parts_rm_iter);
      if (pwi_rm_iter != pwi.end()) pwi.erase(pwi_rm_iter);
      if (pwo_rm_iter != pwo.end()) pwo.erase(pwo_rm_iter);

      /*
      // If pwi is not empty, keep going
      if (pwi.size() != 0) {
        pivot = pwi.begin(); // reset pivot to begin() for merge re-scan
      } else {
        keep_going = false;
      }
      */
      pivot = pwi.begin();
    } else {
#ifdef O_DEBUG
      fmt::print("Before checking pivot\n");
#endif
      // No merge possible, check if pivot can be changed
      if ((pivot + 1) != pwi.end()) {
#ifdef O_DEBUG
        fmt::print("moving pivot old:{}, new:{}\n", *pivot, *(pivot+1));
#endif
        ++pivot;            // change pivot
        keep_going = true;  // keep scanning for merges
      }
    }
  }
}


void Label_acyclic::label(Lgraph *g) {
  fmt::print("Cutoff is {}\n", cutoff);

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

  dump(g);

#ifdef F_DEBUG
  for (auto n : g->forward(hier)) {
    if (node2id.find(n.get_compact()) != node2id.end()) {
      fmt::print("Found {} in node2id\n", n.debug_name());
    } else {
      fmt::print("Not found {} in node2id\n", n.debug_name());
    }
  }
#endif

  // Actual Labeling happens here:
  for (auto n : g->fast(hier)) { 
    n.set_color(node2id[n.get_compact()]);
    n.set_name(std::string(fmt::format("ACYCPART{}", node2id[n.get_compact()])));
  }

  if (verbose) dump(g);
}
