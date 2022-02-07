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

  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { Ann_node_color::clear(lg); });
  }
  Ann_node_color::clear(g); 


#ifdef S_DEBUG
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

#endif
 

  // Iterating through outputs of the graphs (0 out edges), all are potential roots
  g->each_graph_output([&](const Node_pin &pin) {
    const auto nodec = (pin.get_node()).get_compact();  // Node compact flat
    roots.insert(nodec);             // Saving roots
    node2id[nodec] = part_id;        // Saving part ID of nodes
    id2nodes[part_id].push_back(nodec); // Saving nodes under part IDs
    part_id+=5;                       
  });

  // Adding potential roots to the root list
  bool add_root = false;
  
  for (const auto &n : g->forward(hier)) {
    if (n.get_num_out_edges() > 1) {
      //TODO 
      //The sink of these outedges can be outNeighs of the Part

      for (const auto &oe : n.out_edges()) {
        auto sink_nodec = oe.sink.get_node().get_compact();
        auto curr_outg = id2out[part_id];
        // Only add if not there
        if (std::find(curr_outg.begin(), curr_outg.end(), sink_nodec) == curr_outg.end()) {

#ifdef S_DEBUG      
          fmt::print("Adding {} to outNeigh of {}\n", sink_nodec.get_node(g).debug_name(), n.debug_name());
#endif
          id2out[part_id].push_back(sink_nodec);
        }
      }

      add_root = true;
    } else if (n.get_num_out_edges() == 0) {
      add_root = true;
    } else if (n.get_num_out_edges() == 1) {
      // Handle case with one out edge that leads to an output pin
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
      id2nodes[part_id].push_back(nodec);
      part_id+=5;    
    }
  } 


  // Iterating through all the potential roots
  for (auto &n : roots) {
    if (!node_preds.empty()) { node_preds.clear(); }
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
        //bool is_io = (static_cast<int>(pot_pred.debug_name().find("_io_")) == -1);

        if (!(is_root) && !(is_labeled)) {

#ifdef S_DEBUG
          fmt::print("Not a Root and Not labeled: {}\n", pot_pred.debug_name());
#endif

          node2id[pot_predc] = curr_id;
          node_preds.push_back(pot_predc);
          //TODO
          //All the outNeighs of nodes being added are outNeighs of the Part

          /*
          for (auto &oe : pot_pred.out_edges()) {
            auto sink_nodec = oe.sink.get_node().get_compact();
            auto curr_outg = id2out[curr_id];
            // Only add node if:
            //   Not in the outNeigh list for this part ID
            //   Not the same part ID as curr_id but has an ID
            if (std::find(curr_outg.begin(), curr_outg.end(), sink_nodec) == curr_outg.end()) {
              if ((node2id[sink_nodec] != curr_id) && (node2id.contains(sink_nodec))) {

#ifdef S_DEBUG                
                fmt::print("Adding {} to outNeigh of {}\n", sink_nodec.get_node(g).debug_name(), n.get_node(g).debug_name());
#endif

                id2out[curr_id].push_back(sink_nodec);
              }
            }
          }
          */

        } else {
          //TODO
          //Nodes that can't be added, can be inNeighs of the Part
          id2inc[curr_id].push_back(pot_predc);
        }
      }
    }
  }



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

  // Actual Labeling happens here:
  for (auto n : g->forward(hier)) {
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
