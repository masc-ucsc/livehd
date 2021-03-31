
# LGraph Concepts, Traversals, and Annotations

A key API needed for most LGraph passes is the capacity to traverse the graph
and create/read annotations. This document explains the main concepts and
options.

## Concepts

* **LGraph**: A graph is equivalent to the simple concept of Verilog modules.
  For simple Verilog modules, a single graph is created. Complex modules with
  functions can have multiple graphs.  LGraphs can be inspected either forwards
  or backwards.

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

* **Single graph traversal**: This is the basic traversal, and the algorithm does
  not descent to sub-graphs. This traversal does not require a top graph like
  the hierarchical traversal.

* **Hierarchical traversal**: It requires "top" graph which can have several
  sub-graphs.  Different traversal algorithms have different starting/ending
  points, but in hierarchical, they can go up/down the hierarchy. The hierarchy
  boundaries are not visible. The traversals behave like a "virtual flat" graph
  traversal. The only type of sub-graph exposed like in single graph traversal
  are black-box sub-graphs because there is no implementation.

  Explicit hierarchy information can be obtained through the LGraph `ref_htree()` method.  The obtained hierarchy can be considered global by passing the top level module to the required pass in `lgshell`:  
  `livehd> lgraph.open name:<top level module> |> a.hier.pass`

* **Class Attribute**: A class attribute is a value associated with pin, node,
  or edge. The same class attribute value is shared across all the graph
  instances.

* **Hierarchical Attribute**: Given a hierarchy, the same graph can be
  instantiated many times. Each of those instances can have different attribute
  values. A hierarchical attribute allows to have a value per pin, node, or edge
  for each instance.

## Working with LGraphs

`Node`:
  The Node class is essentially a pointer to an actual node in the hierarchy.  Information can be stripped from a node by making it a compact node (which preserves the value of `hidx`) or a compact_class node (which does not).
  A node contains several values that can be used for identification:
   - `nid`: a value that is unique to the node *within an LGraph*.
   - `hidx`: a value that uniquely describes the node's position *within an LGraph*, if the node was passed a `hidx` value upon creation.  This value can be made unique across the entire hierarchy by only traversing the root hierarchy of the entire design.
   - `current_g`: a pointer to the parent LGraph of the node.
   - `top_g`: stores a pointer to the root LGraph of the entire hierarchy.
   - various fields containing the node type (lut, and, add, subgraph, etc.).  

  When the connections going to/from a node are queried, the connection information returned will be different depending on if the node contains hierarchy information or not.  If the node contains no hierarchy information, then the connection possibilities will be limited to either sibling nodes or the parent node.  Otherwise, the connections will be traced back through the hierarchy and the original driver node will be returned.  Note that this driver node may not be a subgraph node.  
  
  If the node is created with a node id using `get_nid()`, the node id will represent the node id of the actual node.  If the edges of this node are traversed, then the hidx of the other node will be the hidx *of that node*, not the parent of that node.  
  
  If the node was created with a nid of `Node::Hardcoded_input_nid` or `Node::Hardcoded_output_nid`, then the hidx returned when the edges are traversed is the hidx *of the parent*.

`Node_pin`:  
  Node pins contain both an index (representing the node that the pin is connected to) and a port id (which is used to uniquely identify the node *within an LGraph*).

`Sub_node`:  
  A Sub_node stores a complete list of all the inputs and outputs of an LGraph, as well as the physical locations of said inputs and outputs.

`LGraph`:
  The LGraph class stores graph information.  An LGraph does not contain information to uniquely identify it (TODO: does it?), but it does contain plenty of methods for querying various attributes of a node within the LGraph from a given `nid` value.  
  There are several ways to iterate over just the subgraph nodes of an LGraph:
   - `each_local_sub_fast`: iterates over all the local subgraph or children. It does not visit grandchildren.
   - `each_local_unique_sub_fast`: Similar to each_local_sub_fast but each subgraph type is visited only once.
   - `get_down_nodes_map`: identical to `each_local_sub_fast` but without the lambda.  
  
  These methods also return a (globally) unique id representing a specific LGraph in a variable of type `Lg_type_id`.
  
  There are even more ways to iterate over all the nodes in an LGraph:
   - `fast`: goes over all nodes in a random order without traversing subgraph nodes.  Use `fast(true)` to recursively traverse subgraph nodes.  `fast` is the fastest way to traverse an LGraph.
   - `forward`: goes over const nodes, then other types of nodes in a specific order.  Use `forward(true)` to recursively traverse subgraphs, as above.
   - `backward`: `forward`, but in reverse.
  
  Graph inputs and outputs are treated as special nodes with hardcoded nid values.

Various classes have dump() methods, which print information about the class for debugging purposes.

## Examples

 - Discover the edges in a hierarchy:
```
absl::flat_hash_set<std::pair<Hierarchy_index, Hierarchy_index>> edge_set;

for (auto hidx : root_tree->depth_preorder()) {
  LGraph* lg = root_tree->ref_lgraph(hidx);
  
  Node temp_hier_node(root_lgraph, hidx, Node::Hardcoded_input_nid);
  
  std::cout << "Visiting LGraph " << lg->get_name() << std::endl;
  
  for (XEdge e : temp_hier_node.inp_edges()) {
    auto new_e = std::pair(e.driver.get_hidx(), e.sink.get_hidx());
    if (new_e.first == new_e.second) { // if the node connects to itself, ignore it
      continue;
    }
    if (edge_set.contains(new_e) { // ignore duplicates
      continue;
    }
    edge_set.insert(new_e);
  }
}
```

 - Recursively iterate over all the nodes in an LGraph:
 ```
 for (lg : var.lgs) {
   for (auto n : lg->forward(true)) {
     std::cout << "Node: " << n.debug_name() << std::endl;
   }
 }
 ```
 
 - Iterate over all child nodes in an LGraph:
```
for (auto lg : var.lgs) {
  lg->each_local_sub_fast([&](Node& n, Lg_type_id id) -> bool {
    std::cout << "node: " << n.debug_name() << ", id: " << id << std::endl;
    return true; // return true to keep iterating over children, or false to end early
  });
}
```

HERE  
 - Memories
 - different outputs
