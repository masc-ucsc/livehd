
# LGraph Concepts, Traversals, and Annotations

A key API needed for most LGraph passes is the capacity to traverse the graph
and create/read annotations. This document explains the main concepts and
options.

## Concepts

* **Graph**: A graph is equivalent to the simple concept of Verilog modules.
  For simple Verilog modules, a single graph is created. Complex modules with
  functions can have multiple graphs. 

* **Black-box graph**: A graph is black-box when there is no known
  implementation. Nevertheless, even for black-box graphs, it is necessary to
  know the inputs and outputs. Once the implementation is known, LGraph will
  match inputs/outputs for the code generation. It is not necessary to
  re-elaborate the graphs instantiating the black-box subgraph.

* **Node**: Each vertex in the graph is a node. The nodes are either logic
  operations (and, or, xor, LUT,...), arithmetic operations (add, mult,...),
  muxes, wire selection (pick, join,...) , registers (flop,latches,...),
  constants (strings, numeric,...), memories, or sub-graphs.  One way to think of a node is
  part of an instantiation of an LGraph.

* **Pin**: Each node has a set of driver or sink pins. In LGraph a driver pin
  is a pin in a node that has drive strength. Driver pins are outputs from the
  node. The single driver pin can connect to a given sink pin, but multiple sink
  pins can connect to the same driver pin.  A pair of node and pid makes a Pin.
  Edges in the graph are connected to pins.

* **Edge**: An edge is the pair of a driver and a sink pin that are directly connected.
  All the graphs use bi-directional edges.

HERE
LGraphs are bi-directional graphs that allow
  algorithms to inspect forward/backward.


* **Single graph traversal**: This is the basic traversal, and the algorithm does
  not descent to sub-graphs. This traversal does not require a top graph like
  the hierarchical traversal.

* **Hierarchical traversal**: It requires "top" graph which can have several
  sub-graphs.  Different traversal algorithms have different starting/ending
  points, but in hierarchical, they can go up/down the hierarchy. The hierarchy
  boundaries are not visible. The traversals behave like a "virtual flat" graph
  traversal. The only type of sub-graph exposed like in single graph traversal
  are black-box sub-graphs because there is no implementation.

* **Class Attribute**: A class attribute is a value associated with pin, node,
  or edge. The same class attribute value is shared across all the graph
  instances.

* **Hierarchical Attribute**: Given a hierarchy, the same graph can be
  instantiated many times. Each of those instances can have different attribute
  values. A hierarchical attribute allows to have a value per pin, node, or edge
  for each instance.

## Working with LGraphs

`Node`:  
  The Node class is essentially a fancy pointer to an actual node in the hierarchy.  Information can be stripped from a node by making it a compact node (which preserves the value of `hidx`) or a compact_class node (which does not).
  A node contains several values that can be used for identification:
   - `nid`, which returns a type that is unique to the node *within an LGraph*.
   - `hidx`, which returns a type that uniquely describes the node *within an LGraph*, if the node was passed a `hidx` value upon creation.  This value can be made unique across the entire hierarchy by ?? (TODO: find a way to do this that doesn't depend on internal features of LGraph...)
   - `current_g`, which stores a pointer to the parent LGraph of the node.
   - `top_g`, which stores a pointer to the root LGraph of the entire hierarchy. (TODO: not sure if this is accurate!)
   - fields containing the node type (lut, and, add, subgraph, etc.)
  When the connections going to/from a node are queried, the connection information returned will be different depending on if the node contains hierarchy information or not.  If the node contains no hierarchy information, then the connection possibilities will be limited to nodes within the current LGraph.  Otherwise, the connections will be traced back through the hierarchy and the original driver will be returned.
  
  If the node is created with a node id like n.get_compact().get_nid(), the node id will represent the node id of the actual node.  If the edges of this node are traversed, then the hidx of the other node will be the hidx *of that node*, not the parent of that node.
  
  If the node was created with a nid of Node::Hardcoded_input_nid or Node::Hardcoded_output_nid, then the hidx returned when the edges are traversed is the hidx *of the parent*.
  
  The best way to traverse across a hierarchy to get globally unique values:
  1. find the root hierarchy (lgid of 1?)
  2. get the reftree (could replace this with an LGraph function)
  3. do a depth_preorder traversal of the graph
  4. create a node using the lgraph, the hidx, and some kind of node id
  5. do stuff with that node - all edges and stuff will be automatically filled since the node you created is a pointer.
  
  TODO: when you call get_down_nodes_map, are all the nodes subgraph nodes? yes!
  

`Node_pin`:
  Node pins contain both an index (representing the node that the pin is connected to) and a port id (which is used to uniquely identify the node *within an LGraph*).

`Sub_node`:
  A Sub_node stores a complete list of all the inputs and outputs of an LGraph, as well as the physical locations of said inputs and outputs.

`LGraph`:  
  The LGraph class stores graph information.  An LGraph does not contain information to uniquely identify it (TODO: does it?), but it does contain plenty of methods for querying various attributes of a node within the LGraph from a given `nid` value.
  Iterators:
   - each_sub_fast_direct (all children, goes over every node)
   - each_sub_fast_unique (all children, skips repeated instantiations of the same lgraph)
  
  Graph inputs and outputs are treated as special nodes with hardcoded id values.

Various classes have dump() methods, which print information about the class for debugging purposes.

## Traversal Algorithms

There are 3 main traversals algorithms fast, forward, and backward. 

### Fast Traversal

Fast will iterate over all nodes in the graph. The nodes will be accessed in
some random order.  This is the fastest traversal algorithm with high cache
locality, it is significantly faster than the other traversals.

### Forward Traversal

## Common LGraph Operations

TODO: either write out examples for basic operations or explain what things do

 - Enumerate everything connected to a certain pin (lgraph -> each_pin)
 - Enumerate the inputs/outputs of an LGraph (lgraph -> get_self_sub_node -> ?)
 - show hidx vs idx vs lgid vs nid vs ...
 - explain lg_type_id
 - explain get_down_nodes_map and why it's compact
 - explain get_top_lgraph vs get_...
 
 - Iterate over all drivers of a node:  
```
for (auto pin : node.inp_connected_pins()) {
  for (auto driver_pin : pin.inp_driver()) {
    std::cout << driver_pin.get_node().debug_name() << std::endl;
  }
}
```

### Iterating over the connections on a node


 - Iterate over all child nodes in an LGraph:
```
for (auto lg : var.lgs) {
  lg->each_sub_fast([&](Node& n, Lg_type_id id) -> bool {
    std::cout << "node: " << n.debug_name() << ", id: " << id << std::endl;
    return true; // return true to keep iterating over children, or false to end early
  });
}
```

HERE

-Memories different outputs

