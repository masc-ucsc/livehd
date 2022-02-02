// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

#include "annotate.hpp"
#include "cell.hpp"
#include "pass.hpp"

Label_acyclic::Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff) : verbose(_verbose), hier(_hier), cutoff(_cutoff) { 
  part_id = 0; 
}

void Label_acyclic::dump() const {
  fmt::print("Label_acyclic dump\n");
}

void Label_acyclic::label(Lgraph *g) {
  if (cutoff) fmt::print("small partition cutoff: {}\n", cutoff);
 
  // Internal Nodes printing 
  for (auto n : g->forward(hier)) {
    //fmt::print("Node: {}\n", n.debug_name());
    n.set_color(8);
  }

  // Inputs
  g->each_graph_input([&](const Node_pin &pin) {
    auto n = pin.get_node();
    //fmt::print("Inputs: {}\n", n.debug_name()); 
  });


  // Outputs
  g->each_graph_output([&](const Node_pin &pin) {
    auto n = pin.get_node();
    //fmt::print("Outputs: {}\n", n.debug_name()); 
  });


  /*
  // Iterating through graph inputs 
  g->each_graph_input([&](const Node_pin &pin) {
    for (const auto &e : pin.out_edges()) {
      auto sink_node = e.sink.get_node();
      auto driver_node = e.driver.get_node();
      fmt::print("sink_node: {}, driver_node: {}\n", sink_node.debug_name(), driver_node.debug_name());
    }
  });
  */

  // Iterating through outputs of the graphs (0 out edges), all are potential roots
  g->each_graph_output([&](const Node_pin &pin) {
    const auto nodec = (pin.get_node()).get_compact();  // Node compact flat
    part_roots.insert(nodec);      // Inserting node compact
    part_nodes[nodec] = part_id;   // Adding node to map and assigning id 
    part_id++;                       
  });

  // Also adding all nodes with more than 1 out_edge to potential roots
  for (auto n : g->forward(hier)) {
    if (n.get_num_out_edges() > 1 || n.get_num_out_edges() == 0) {
      const auto nodec = n.get_compact();
      part_roots.insert(nodec); 
      part_nodes[nodec] = part_id;
      //fmt::print("Root: {}, ID: {}\n", n.debug_name(), part_id);
      part_id++;
    } else {
      //fmt::print("Not a Root: {}\n", n.debug_name());
    }
  } 

  // Iterating through all the potential roots
  for (auto n : part_roots) {
    if (!node_preds.empty()) node_preds.clear();
    
    node_preds.push_back(n);              // Adding yourself as a predecessor
    while (node_preds.size() != 0) {
      auto curr_pred = node_preds.back(); // Getting a predecessor to explore
      node_preds.pop_back();              
      
      // Checking the predecessors of curr_pred to add more nodes to explore
      Node temp_n(g, curr_pred);
      for (auto ie : temp_n.inp_edges()) {
        
        auto pot_pred = ie.driver.get_node().get_compact();
        // Only add node if node is not a root or node is not already assigned
        if (!part_roots.contains(pot_pred)) {
          if (!part_nodes.contains(pot_pred)) {
            part_nodes[pot_pred] = part_nodes[curr_pred];
            node_preds.push_back(pot_pred);
          }
        }
      }
    }
    // while there are still predecessors 
    //   pop one out, and explore its predecessors
    //     explore a node's input edges
    //       then get the driver of each input edge --> preds
    //       for all preds found that are valid, add to node_preds
    //         valid preds == (NOT in part_roots && NOT in part_nodes)
    //    
  }

  
  for (auto &it : part_roots) {
    Node n(g, it);
    //fmt::print("Root Node: {}\n", n.debug_name());
  }

  for (auto &it : part_nodes) {
    Node n(g, it.first);
    //fmt::print("Node: {}, Part ID: {}\n", n.debug_name(), it.second);
  }


  for (auto node : g->fast(hier)) {
    (void)node;  // to avoid warning
    // FIXME: do pass here
  }

  if (verbose) {
    dump();
  } 
}
