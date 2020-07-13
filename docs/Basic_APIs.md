# LGraph Internals

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
```cpp
for (auto &out : node.out_edges()) {
  auto  dpin       = out.driver;
  auto  dpin_pid   = dpin.get_pid();
  auto  dnode_name = dpin.get_node().debug_name();
  auto  snode_name = out.sink.get_node().debug_name();
  auto  spin_pid   = out.sink.get_pid();
  auto  dpin_name  = dpin.has_name() ? dpin.get_name() : "";
  auto  dbits      = dpin.get_bits();

  fmt::print(" {}->{}[label=\"{}b :{} :{} :{}\"];\n"
      , dnode_name, snode_name, dbits, dpin_pid, spin_pid, dpin_name);
}
```
## LGraph Node Type Semantics

### Sum_Op

Add/Substraction node

```{.graph .center caption="Sum LGraph Node."}
digraph Sum {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Sum;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> Sum [ label ="ADD" ];
    q1 -> Sum [ label ="SUB" ];
    Sum  -> q [ label = "Y" ];
}
```

If the inputs do not have the same size, they are extended (sign or unsigned)
to all have the same length.

#### Forward Propagation

* $Y = \sum_{i=0}^{\infty} ADD_{i} - \sum_{i=0}^{\infty} SUB_{i}$
* $Y.max = \sum_{i=0}^{\infty} ADD_{i}.max - \sum_{i=0}^{\infty} SUB_{i}.min$
* $Y.min = \sum_{i=0}^{\infty} ADD_{i}.min - \sum_{i=0}^{\infty} SUB_{i}.max$
* $Y.sign = Y.min<0$

#### Backward Propagation

The Sum node allows a conservative back propagation ($ADD.bits = Y.bits$) only when:

* $ADD.bits = Y.bits$, only if ADD pin is used and all the ADD pins are
  unsigned (sign false).
* Neither ADD.sign now SUB.sign can be backward propagated

#### Verilog Considerations

The Verilog to LiveHD translator MUST create Sum_Op operations where all the
inputs have the same number of bits. In Verilog:

```verilog
logic signed [3:0] a = -1
logic signed [4:0] c;

assign c = a + 1'b1;
```

The previous Verilog example extends everything to 5 bits (c), but unsigned
extended because one of the inputs is unsigned (1b1 is unsigned in verilog, and
2sb1 is signed +1). LGraph semantics are different.

```verilog
c = 5b01111 + 5b0001 // this is the Verilog semantics by matching size
c == -16 (!!)
```

Once the inputs are zero/sign extended to match size. The Sum operator always
generates the same result independent of the sign of the inputs.

### Mult_Op

Multiply operator

```{.graph .center caption="Multiply LGraph Node."}
digraph Mult {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Mult;
    node [shape = point ]; q0
    node [shape = point ]; q

    q0 -> Mult [ label ="VAL" ];
    Mult  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \prod_{i=0}^{\infty} VAL_{i}$
* $Y.max = \prod_{i=0}^{\infty} \text{maxabs}(\text{VAL}_{i}.max, \text{VAL}_{i}.min)$
* $Y.min = \begin{cases} -Y.max & Y.sign \ne 0 \\                                                                                                                                  \prod_{i=0}^{\infty} \text{minabs}(\text{VAL}_{i}.max, \text{VAL}_{i}.min) & \text{otherwise} \end{cases}$
* $Y.sign = !\forall_{i=0}^{\infty} (VAL_{i}.min<0 and \text{VAL}_{i}.max<0) and !\forall_{i=0}^{\infty} (VAL_{i}.min>=0 and \text{VAL}_{i}.max>=0)$

#### Backward Propagation

* Conservative $VAL.bits = Y.bits$ is possible.
* $VAL.sign$ can be set unsigned only when $Y.sign$ is known to be unsigned

### Verilog Considerations

Unlike the Sum_Op, the Verilog 2 LiveHD translation does not need to extend the
inputs to have matching sizes.  Multiplying/dividing signed and unsigned
numbers has the same result. The bit representation is the same if the result
was signed or unsigned.

LiveHD mult node result (Y) number of bits can be more efficient than in
Verilog.  E.g: if the max value of VAL is 3 (2 bits) and 5 (3bits). If the
result is unsigned, the maximum result is 15 (4 bits). In Verilog, the result
will always be 5 bits. If the Verilog result was to an unsigned variable.
Either all the inputs were unsigned, or there should be a Join_Op with 1bit
zero to force the MSB as positive. This extra bit will be simplified but it
will notify LGraph that the output is to be treated as unsigned.

### Div_Op

Division operator

```{.graph .center caption="Division LGraph Node."}
digraph Div {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Div;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> Div [ label ="NUM" ];
    q1 -> Div [ label ="DEN" ];
    Div  -> q [ label = "Y" ];
}
```

* $Y = \frac{\text{NUM}}{\text{DEN}}$
* $Y.max = \frac{\text{maxabs}(\text{NUM}.max,\text{NUM}.min)}{\text{minabs}(\text{DEN}.max,\text{DEN}.min)}$
* $Y.min = \frac{\text{minabs}(\text{NUM}.max,\text{NUM}.min)}{\text{maxabs}(\text{DEN}.max,\text{DEN}.min)}$
* $Y.sign = !(NUM.min<0 and NUM.max<0 and DEN.min<0 and DEN.max<0) and !(NUM.min>=0 and NUM.max>=0 and DEN.min>=0 and DEN.max>=0) $
* No back propagation is possible.

The result is unsigned if all the allowed values (min..max) are positive, or
all the allowed values are negative. Otherwise, the result is signed.

Verilog NOTE: The same considerations as in the multiplication should be applied.

### Modulo_Op

Modulo operator

```{.graph .center caption="Modulo LGraph Node."}
digraph Mod {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Mod;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> Mod [ label ="NUM" ];
    q1 -> Mod [ label ="DEN" ];
    Mod  -> q [ label = "Y" ];
}
```

* $Y = \text{NUM} % \text{DEN}$
* $Y.max = DEN.max-1$
* $Y.min = 0$
* $Y.sign = 0$
* Back propagation to the DEN input is possible $DEN.max = Y.max+1$.

The result is unsigned if all the allowed values (min..max) are positive, or
all the allowed values are negative. Otherwise, the result is signed.

Verilog NOTE: The result is always unsigned like in Verilog. The inputs do not
need to be sign extended.

### Not_Op

Not operator

```{.graph .center caption="Node LGraph Node."}
digraph Mod {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Not;
    node [shape = point ]; q0
    node [shape = point ]; q

    q0 -> Not [ label ="VAL" ];
    Not  -> q [ label = "Y" ];
}
```

* $Y = \text{bitwise-not}(\text{VAL})$
* $Y.max = (1<<VAL.bits)-1$
* $Y.min = 0$
* $Y.sign = 0$
* Back propagation is possible by knowing the 2 of the 3 inputs bit sizes.

Each bit in the input VAL is toggled. Y and VAL should have the same number of
bits. The result is unsigned.


### Join_Op

Join or concatenate operator

```{.graph .center caption="Join LGraph Node."}
digraph Join {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Join;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q2
    node [shape = point ]; q

    q0 -> Join [ label ="P0" ];
    q1 -> Join [ label ="P1" ];
    q2 -> Join [ label ="P2" ];
    Join  -> q [ label = "Y" ];
}
```

* $Y = ... P_{2}<<(P_{1}.bits+P_{0}._bits) | P_{1}<<(P_{0}.bits) | P_{0}
* $Y.max = (\text{absmax}(P_{n}.max, P_{n}.min)<<(P_{0}.bits+..+P_{n-1}.bits))-1$
* $Y.min = 0$
* $Y.sign = 0$
* Back propagation is possible when only one input is unknown (and it is not the last one $P_{n}$). Only
the bitwidth can be back propagated. Not the sign.


Pick_Op: Pick some bits from the VAL input pin
Y = VAL[[OFF..(OFF+Y.__bits)]]}

And_Op: bitwise AND with 2 outputs single bit reduction (RED) or bitwise
Y = VAL&..&VAL ; RED= &Y

pandoc --pdf-engine=xelatex --toc -N GitHub-use.md Basic_APIs.md --mathjax --filter pandoc-graphviz.py -o ~/tmp/pp.pdf
https://github.com/Wandmalfarbe/pandoc-latex-template
https://pianomanfrazier.com/post/write-a-book-with-markdown/

#### To be continued ...
