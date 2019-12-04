## LGraph APIs needed to know

#### For LGraph developer(must read if you know the old LGraph):
* src_pin/dst_pin have been renamed to driver_pin/sink_pin

* the concept of node bitwidth has been removed, it’s wrong since  
  every output edge of a node could have different bitwidth. 
  The edge bitwidth is defined on the driver_pin.

* in the old LGraph, every graph input/output is represented as different nodes.
  However, in the new lgraph, all graph inputs are represented as a single graph
  input node, each graph input is a pin of that node. Same as graph outputs.  

* The LGraph iterator such as for(auto node: g->forward()) no longer visit graph ios, the graph io should be handled separately, for example,

```
// simple way using lambda 
lg->each_graph_input([&](const Node_pin &pin){

  //your operation with graph_input node_pin;

});
```
```
// hard approach
ginp_node = lg->get_graph_input_node();

for (const auto &out_edge : ginp_node.out_edges()) {

  Node_pin driver_pin = out_edge.driver;

  Node_pin sink_pin = out_edge.sink;

  Node sink_node = out_edge.sink.get_node();

}
```
#### APIs
* create node
```
new_node = lg->create_node()
//note: type and/or bits still need to be assigned later
```
* create node with node type assigned
```
new_node = lg->create_node(Node_type_Op)
//note: recommended way if you know the target node type
```
* create a constant node
```
new_node = lg->create_node_const(uint32_t)
//note: recommended way to create a const node
```

* setup default driver pin for pin_0 of a node
```
driver_pin = new_node.setup_driver_pin();
//note: when you know the node type only has one output pin

```

* setup default sink pin for pin_0 of a node
```
sink_pin = new_node.setup_sink_pin()
//note: when you know the node type only has one input pin
```

* setup driver pin for pin_x of a node
```
driver_pin = new_node.setup_driver_pin(pid)
//note: when you know the pid, same as sink_pin
```

* get the pid value of a node_pin object
```
node_pin.get_pid()
```

* add an edge between driver_pin and sink_pin
```
lg->add_edge(driver_pin, sink_pin)
```

* get the driver node of an edge
```
driver_node = edge.driver.get_node()

```

* use node as the index/key for a container
```
absl::flat_hash_map<Node::Compact, int> my_map;
my_map[node1.get_compact()] = 77; 
my_map[node2.get_compact()] = 42;
...
```

* use node_pin as the index/key for a container
```
absl::flat_hash_map<Node_pin::Compact, int> my_map;
my_map[node_pin1.get_compact()] = 14; 
my_map[node_pin2.get_compact()] = 58;
...
```

* create a LGraph input(output) with the name
```
new_node_pin = lg->add_graph_input(std::string_view)
```

* debug information of a node
```
node.debug_name()
```

* debug information of a node_pin
```
node_pin.debug_name()
```
* iterate output edges and get node/pin information from it
```
for (auto &out : node.out_edges()) {
  auto  dpin       = out.driver;
  auto  dpin_pid   = dpin.get_pid();  
  auto  dnode_name = dpin.get_node().debug_name();
  auto  snode_name = out.sink.get_node().debug_name();
  auto  spin_pid   =  out.sink.get_pid();  
  auto  dpin_name  = dpin.has_name() ? dpin.get_name() : "";
  auto  dbits      = dpin.get_bits();

  fmt::print(" {}->{}[label=\"{}b :{} :{} :{}\"];\n"
      , dnode_name, snode_name, dbits, dpin_pid, spin_pid, dpin_name);
}
```
#### To be continued ...
