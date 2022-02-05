// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

#include "annotate.hpp"
#include "cell.hpp"
#include "pass.hpp"

#define DEBUG 0

Label_acyclic::Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff) : verbose(_verbose), hier(_hier), cutoff(_cutoff) { 
  part_id = 0; 
}

void Label_acyclic::dump() const {
  fmt::print("Label_acyclic dump\n");
}

void Label_acyclic::label(Lgraph *g) {
  if (cutoff) fmt::print("small partition cutoff: {}\n", cutoff);

  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { Ann_node_color::clear(lg); });
  }
  Ann_node_color::clear(g); 


#if DEBUG
  // Internal Nodes printing 
  int my_color = 0;
  int input_color = 11;
  int output_color = 22;
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

  // Inputs
  g->each_graph_input([&](const Node_pin &pin) {
    auto n = pin.get_node();
    //fmt::print("Inputs: {}\n", n.debug_name()); 
    n.set_color(input_color);
    n.set_name(mmap_lib::str(fmt::format("in_{}", input_color)));
  });


  // Outputs
  g->each_graph_output([&](const Node_pin &pin) {
    auto n = pin.get_node();
    //fmt::print("Outputs: {}\n", n.debug_name()); 
    n.set_color(output_color);
    n.set_name(mmap_lib::str(fmt::format("out_{}", output_color)));
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
#endif
  
  // Iterating through outputs of the graphs (0 out edges), all are potential roots
  g->each_graph_output([&](const Node_pin &pin) {
    const auto nodec = (pin.get_node()).get_compact();  // Node compact flat
    roots.insert(nodec);             // Saving roots
    node2id[nodec] = part_id;        // Saving part ID of nodes
    id2nodes[part_id].push_back(nodec); // Saving nodes under part IDs
    part_id++;                       
  });

  // Also adding all nodes with more than 1 out_edge to potential roots
  bool add_root = false;
  for (auto n : g->forward(hier)) {
    if (n.get_num_out_edges() > 1 || n.get_num_out_edges() == 0) {
      add_root = true;
    } else if (n.get_num_out_edges() == 1) {
      // TODO
      // Get the node on the head of this edge
      // access the out edge  
      for (auto &oe : n.out_edges()) { 
        auto sink_node_name = oe.sink.get_node().debug_name();
        if (static_cast<int>(sink_node_name.find("_io_")) != -1) {
          add_root = true;
        }
      }

#if DEBUG
      fmt::print("** One Out Edge Case:\n");
      for (auto &oe : n.out_edges()) {
        auto sink_pin = oe.sink;
        //auto sink_pin_id = sink_pin.get_pid();
        auto sink_node = oe.sink.get_node();
        auto sink_pin_name = sink_pin.has_name() ? sink_pin.get_name() : "NoName";
        auto sink_node_name = sink_node.debug_name();
        fmt::print("** Node:{}, Sink Pin:{}, Sink Node:{}\n", n.debug_name(), sink_pin_name, sink_node_name);
      }
#endif
    }

    if (add_root == true) {
      add_root = false;
      const auto nodec = n.get_compact();
      roots.insert(nodec); 
      node2id[nodec] = part_id;
      id2nodes[part_id].push_back(nodec);
      //fmt::print("Root: {}, ID: {}\n", n.debug_name(), part_id);
      part_id+=5;    
    }
  } 

  // Iterating through all the potential roots
  for (auto n : roots) {
    if (!node_preds.empty()) node_preds.clear();
    auto curr_id = node2id[n];
    node_preds.push_back(n);              // Adding yourself as a predecessor
    while (node_preds.size() != 0) {
      auto curr_pred = node_preds.back(); // Getting a predecessor to explore
      node_preds.pop_back();              
      
      // Checking the predecessors of curr_pred to add more nodes to explore
      Node temp_n(g, curr_pred);
      for (auto ie : temp_n.inp_edges()) {
        
        auto pot_pred = ie.driver.get_node().get_compact();
        // Only add node if node is not a root or node is not already assigned
        if (!roots.contains(pot_pred)) {
          if (!node2id.contains(pot_pred)) {
            node2id[pot_pred] = curr_id;
            node_preds.push_back(pot_pred);
          }
        }
      }
    }
  }


  for (auto n : g->forward(hier)) {
    if (node2id.find(n.get_compact()) != node2id.end()) {
      fmt::print("Found {} in node2id\n", n.debug_name());
      n.set_color(node2id[n.get_compact()]);
      n.set_name(mmap_lib::str(fmt::format("ACYCPART{}", node2id[n.get_compact()])));
    } else {
      fmt::print("Not found {} in node2id\n", n.debug_name());
      n.set_color(8);
      n.set_name(mmap_lib::str("MISSING"));
    }
  }

#if DEBUG
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

  /*
  for (auto node : g->fast(hier)) {
    fmt::print("Node: {}, Part ID: {}\n", node.debug_name(), node2id[node]);
    n.set_color(it.second);
  }
  */

  if (verbose) {
    dump();
  } 
}
