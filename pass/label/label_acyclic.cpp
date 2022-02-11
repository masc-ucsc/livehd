// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

#include "annotate.hpp"
#include "cell.hpp"
#include "pass.hpp"

//#define A_DEBUG 1  // toggle for prelim debug print
//#define M_DEBUG 1  // toggle to get print when merge detected
//#define S_DEBUG 1  // toggle for after partition print

#define MERGE 1

Label_acyclic::Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff, bool _merge_en) : verbose(_verbose), hier(_hier), merge_en(_merge_en), cutoff(_cutoff) {
  part_id = 0;  
}

void Label_acyclic::dump() const {
  fmt::print("Label_acyclic dump\n");
}

bool Label_acyclic::set_cmp(NodeSet a, NodeSet b) const {
  if (a.size() != b.size()) return false;
  
  for (auto &n : a) {
    if (!(b.contains(n))) return false;
  }

  return true;
}


void Label_acyclic::label(Lgraph *g) {
  fmt::print("Small Partition Cutoff: {}, Merge: {}\n", cutoff, merge_en);

  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { Ann_node_color::clear(lg); });
  }
  Ann_node_color::clear(g); 


#ifdef A_DEBUG
  // Internal Nodes printing 
  int my_color = 0;
  int node_tracker = 0;
  
  for (auto n : g->forward(hier)) {
    fmt::print("Node: {}\n", n.debug_name());
    my_color = (node_tracker < 8) ? (8) : (16);
    n.set_color(my_color);
    if (n.has_color()) fmt::print("Node Color: {}\n", n.get_color());
    //n.set_name(mmap_lib::str(n.debug_name()));
    n.set_name(mmap_lib::str(fmt::format("MFFC_{}", my_color)));
    node_tracker++;
  }
  fmt::print("Found {} nodes using g->forward(hier)\n", node_tracker);

#endif
 

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

      //The sink of these outedges can be outNeighs of the Part
      for (const auto &oe : n.out_edges()) {
        auto sink_nodec = oe.sink.get_node().get_compact();
        
        // Only add to outgoing neighbors if not _io_
        if (static_cast<int>(sink_nodec.get_node(g).debug_name().find("_io_")) == -1) {
          id2out[part_id].insert(sink_nodec);
        }
      }
      add_root = true;
    } else if (n.get_num_out_edges() == 0) {
      add_root = true;
    } else if (n.get_num_out_edges() == 1) {
      // Handle case with one out edge that leads to an output pin
      //   If the sink of the out edge IS an io, add_root
      for (const auto &oe : n.out_edges()) { 
        const auto sink_node_name = oe.sink.get_node().debug_name();
        if (static_cast<int>(sink_node_name.find("_io_")) != -1) {
          add_root = true;
        }
        //else we do nothing cause Not a Root
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


  // Iterating through all the potential roots
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
       
        // Three conditions that must be false for node to be addable
        bool is_root = roots.contains(pot_predc);
        bool is_labeled = node2id.contains(pot_predc);
        bool not_io = (static_cast<int>(pot_pred.debug_name().find("_io_")) == -1);
        
        if (!(is_root) && !(is_labeled) && not_io) {
          node2id[pot_predc] = curr_id;
          node_preds.push_back(pot_predc);
          
          //All the outNeighs of nodes being added are outNeighs of the Part
          for (auto &oe : pot_pred.out_edges()) {
            auto sink_nodec = oe.sink.get_node().get_compact();

            // Only add to outgoing neighbors if:
            //   Node is labeled & Node is not of current id
            //   Also make sure it does not exist to prevent empty vectors
            if (node2id.contains(sink_nodec)) {
              if (node2id[sink_nodec] != curr_id) {
                id2out[curr_id].insert(sink_nodec);
              }
            }
          }
        } else {
          
          // Nodes not added to the Part can be incoming neighbors of the Part
          //   Must NOT be in the incoming vector & NOT be an _io_          
          if (static_cast<int>(pot_predc.get_node(g).debug_name().find("_io_")) == -1) {
            id2inc[curr_id].insert(pot_predc);
          }
        }

      } // END of inp_edge iteration for loop
    } // END of node_preds clearing while loop 
  } // END of root iteration for loop

#ifdef MERGE
  // Use part_id to generate Partition lists
  //    we can use lists to directly access the map
  if (merge_en) {
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

    // Declare outside of loop for better access
    bool merge_flag = true;
    bool keep_going = true;
    int merge_into = -1;      // The partition that will grow
    int merge_from = -1;      // The partition that will be eaten

    while (keep_going) {
      // reset the flags and ids
      merge_flag = false;
      keep_going = false;
      merge_into = -1;
      merge_from = -1;

      // Iterate through pwi to check which are the same
      for (auto i = pwi.begin(); i != pwi.end(); ++i) { 
        if (i == pivot) continue;  // Same partition, no need to compare
       
        // Merge condition 
        if (set_cmp(id2inc[*i], id2inc[*pivot])) {
          // At least one of the partitions has to be a small partition
          auto pivot_part_size = (id2nodes[*pivot]).size();
          auto i_part_size = (id2nodes[*i]).size();
          if (pivot_part_size >= cutoff || i_part_size >= cutoff) { 
            // two flags are always toggled together in the loop
            merge_flag = true;
            keep_going = true;
            merge_into = *pivot;
            merge_from = *i;
            break;    // Do merge outside the loop
          }
        }
      }

      // Here means for loop ran to completion
      if (merge_flag) {

#ifdef M_DEBUG
        fmt::print("Merge Detected, {} -> {}\n", merge_from, merge_into); 
#endif
        // merge id2inc and erase merge_from
        id2inc[merge_into].merge(id2inc[merge_from]);
        id2inc.erase(merge_from);

        // merge_into & merge_from in id2out: merge and erase merge_from
        // only merge_from in id2out: merge_from -> merge_into, erase merge_from
        bool merge_into_has_out = id2out.contains(merge_into);
        bool merge_from_has_out = id2out.contains(merge_from);
        if (merge_into_has_out && merge_from_has_out) {
          id2out[merge_into].merge(id2out[merge_from]);
          id2out.erase(merge_from);
        } else if (!merge_into_has_out && merge_from_has_out) {
          id2out[merge_into] = id2out[merge_from];
          id2out.erase(merge_from);
        }
        // all other cases do nothing

        // Replace all merge_from ids with merge_into
        for (auto &it : node2id) {
          if (it.second == merge_from) node2id[it.first] = merge_into;
        }

        // merge id2nodes and erase merge_from
        id2nodes[merge_into].merge(id2nodes[merge_from]);
        id2nodes.erase(merge_from);

        // Removing merge_from from pwi, pwo, and parts for merge re-scan
        auto parts_rm_iter = std::find(parts.begin(), parts.end(), merge_from);
        auto pwi_rm_iter = std::find(pwi.begin(), pwi.end(), merge_from);
        auto pwo_rm_iter = std::find(pwo.begin(), pwo.end(), merge_from);
        
        if (parts_rm_iter != parts.end()) parts.erase(parts_rm_iter);
        if (pwi_rm_iter != pwi.end()) pwi.erase(pwi_rm_iter);
        if (pwo_rm_iter != pwo.end()) pwo.erase(pwo_rm_iter);
 
        pivot = pwi.begin(); // reset pivot to begin() for merge re-scan
      } else {
        // No merge possible, check if pivot can be changed
        if (pivot != pwi.end()) {
          ++pivot;            // change pivot
          keep_going = true;  // keep scanning for merges
        }
      }
    }
  }
#endif

  // Actual Labeling happens here:
  for (auto n : g->fast(hier)) {
    // Searching for nodes that did not get accessed when partitioning
    // Ones that are found will be labeled/colored
    if (node2id.find(n.get_compact()) != node2id.end()) {
      //fmt::print("Found {} in node2id\n", n.debug_name());
      n.set_color(node2id[n.get_compact()]);
      n.set_name(mmap_lib::str(fmt::format("ACYCPART{}", node2id[n.get_compact()])));
    } else {
      fmt::print("Not found {} in node2id\n", n.debug_name());
      n.set_color(8);
      n.set_name(mmap_lib::str("MISSING"));
    }
  }


#ifdef S_DEBUG
  fmt::print("node2incoming: \n");
  for (auto &it : id2inc) {
    fmt::print("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      fmt::print("    {}\n", node.debug_name());
    }
  }
  
  fmt::print("node2outgoing: \n");
  for (auto &it : id2out) {
    fmt::print("  Part ID: {}\n", it.first);
    for (auto &n : it.second) {
      Node node(g, n);
      fmt::print("    {}\n", node.debug_name());
    }
  }
  
  fmt::print("Roots: \n");
  for (auto &it : roots) {
    Node n(g, it);
    fmt::print("    {}\n", n.debug_name());
  }

  fmt::print("node2id: \n");
  for (auto &it : node2id) {
    Node n(g, it.first);
    fmt::print("    {}, ID: {}\n", n.debug_name(), it.second);
    //n.set_color(it.second);
  }

#endif

  if (verbose) { dump(); }

}
