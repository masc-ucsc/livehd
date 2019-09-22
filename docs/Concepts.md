
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
  constants (strings, numeric,...), memories, or sub-graphs.

* **Pin**: Each node has a set of driver or sink pins. In LGraph a driver pin
  is a pin in a node that has drive strength. Driver pins are outputs from the
  node. The single driver pin can connect to a given sink pin, but multiple sink
  pins can connect to the same driver pin.  A pair of node and pid makes a Pin.
  Edges in the graph are connected to pins.

* **Edge**: An edge is the pair of a driver and a sink pin that are directly connected.
  All the graphs use bi-direct

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

## Traversal Algorithms

There are 3 main traversals algorithms fast, forward, and backward. 

### Fast Traversal

Fast will iterate over all nodes in the graph. The nodes will be accessed in
some random order.  This is the fastest traversal algorithm with high cache
locality, it is significantly faster than the other traversals.

### Forward Traversal

HERE

-Memories different outputs

