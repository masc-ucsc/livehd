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

If and all the inputs but one ADD ($ADD_{0}$) are known:

* $ADD_{0}.max = Y.max - \sum{i=1}^{\infty} ADD_{i}.max + \sum_{i=0}^{\infty} SUB_{i}.min$
* $ADD_{0}.min = Y.min - \sum{i=1}^{\infty} ADD_{i}.min + \sum_{i=0}^{\infty} SUB_{i}.max$
* $ADD_{0}.sign = $ADD_{0}.min<0$

If and all the inputs but one SUB ($SUB_{0}$) are known:

* $SUB_{0}.max = \sum{i=0}^{\infty} ADD_{i}.max - \sum_{i=1}^{\infty} SUB_{i}.min - Y.max$
* $SUB_{0}.min = \sum{i=0}^{\infty} ADD_{i}.min - \sum_{i=1}^{\infty} SUB_{i}.max - Y.min$
* $SUB_{0}.sign = $SUB_{0}.min<0$

#### Verilog Considerations

In Verilog, the addition is unsigned if any of the inputs is unsigned. If any
input is unsigned. all the inputs will be "unsigned extended" to match the
largest value. This is different from Sum_Op semantics were each input is
signed or unsigned extended independent of the other inputs. To match the
semantics, when mixing signed and unsigned, the signed inputs connected to
Sum_Op with less than the maximum number of bits should be 1 bit zero extended.
An easy way to zero extend a number if with the Join_Op (Join_Op has an
unsigned output if the last input is unsigned).


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

### Sext_Op

This operatator sign extends the input picking the POS bit as most significant or sign bit.

```{.graph .center caption="Sext LGraph Node."}
digraph Sext {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; cell;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> cell [ label ="VAL" ];
    q1 -> cell [ label ="SZ" ];
    cell  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \left\{\begin{matrix} VAL\&((1\ll SZ)-1) & \text{if}\ VAL[SZ]==0 \\ -(((\neg VAL)\& ((1\ll SZ)-1))+1) & \text{otherwise} \end{matrix}\right.$
* $Y.max = (P_{n}.max<<(P_{0}.bits+..+P_{n-1}.bits))-1$
* $Y.min = (P_{n}.min<<(P_{0}.bits+..+P_{n-1}.bits))$
* $Y.sign = 1$

#### Backward Propagation

If the driver to Sext_Op only drives this node, the same Y.max and Y.min can be
propagated to the VAL.max and VAL.min.  The reason is that the upper dropped
bits were not used anyway, so no need to have all those bits computed.

 VAL.min = 0

#### Other Considerations

The Sext_Op is similar to the Pick_Op when OFF is zero and SZ is the same. The
difference is the sign extension. For Pick_Op, the result is always unsigned,
for Sext_Op, the result is always signed.

### Join_Op

Join or concatenate operator. The output keeps the sign of the most last or most significant input.

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

#### Forward Propagation

* $Y = ... P_{2}<<(P_{1}.bits+P_{0}._bits) | P_{1}<<(P_{0}.bits) | P_{0}$
* $Y.max = (P_{n}.max<<(P_{0}.bits+..+P_{n-1}.bits))-1$
* $Y.min = (P_{n}.min<<(P_{0}.bits+..+P_{n-1}.bits))$
* $Y.sign = P_{n}.sign$

#### Backward Propagation

* Back propagation is possible when only one input is unknown (and it is not the last one $P_{n}$). Only
the bitwidth can be back propagated. Not the sign.

#### Other Considerations

Join_Op has two uses besides concatenating values. It is the way to implement a left shift
and it is the way to create an unsigned value out of a signed value.

Join_Op(xx,0u3bits) is a left shift by adding as many zeros as the $P_{0}$ has.

Join_Op(0,xx) will result in the same number as xx, but unsigned. Join_Op can
be used to unsigned extend by adding a zero to the last input. The new $Y.max =
P_{0}.max - P_{0}.min$ if min was negative and max was positive.


Pick_Op: Pick some bits from the VAL input pin
Y = VAL[[OFF..(OFF+Y.__bits)]]}

And_Op: bitwise AND with 2 outputs single bit reduction (RED) or bitwise
Y = VAL&..&VAL ; RED= &Y

### ArithShiftRight_Op

The right shift is the equivalent of arithmetic right shift if the input is signed. If the input is unsigned, RightShift_Op behaves like an simple right shift.


#### Other Considerations

The Pick_Op operation performs unsigned right shift even for signed inputs $Y = Pick_Op(VAL, OFF, 0)$ behaves like a $Y=VAL>>OFF$.


### Pick_Op

Pick selects some bits from the source (VAL). It can be used as an unsigned right shift. The Pick_Op result is always unsigned.


#### Forward Propagation

* $Y = \left\{\begin{matrix} VAL>>OFF & SZ==0 \\ (VAL>>OFF) \& (1<<SZ)-1) & otherwise \end{matrix}\right.$
* $Y.max = \left\{\begin{matrix} VAL.max>>OFF & SZ==0 \\ (VAL.max>>OFF) \& (1<<SZ)-1) & otherwise \end{matrix}\right.$
* $Y.min = 0$
* $Y.sign = 0$

#### Backward Propagation

The sign can not be backward propagated because Pick_Op removes the sign no matter the input sign.


# Generate PDF

pandoc --pdf-engine=xelatex --toc -N GitHub-use.md Basic_APIs.md --mathjax --filter pandoc-graphviz.py -o ~/tmp/pp.pdf
https://github.com/Wandmalfarbe/pandoc-latex-template
https://pianomanfrazier.com/post/write-a-book-with-markdown/

#### To be continued ...
