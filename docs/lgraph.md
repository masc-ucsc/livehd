# LGraph Internals

LGraph is the graph-based data structure used inside LiveHD. Together with
LNAST, it is one of the key data structures.

The LGraph can be built directly with passes like Yosys, or through LNAST to
LGraph translations. The LNAST builds a gated-SSA which is translated to
LGraph. Understanding the LGraph is needed if you want to build a LiveHD pass.

## LGraph API

A single LGraph represents a single netlist module. LGraph is composed of nodes,
node pins, edges and tables of attributes. An LGraph node is affiliated with a
node type and each type defines different amounts of input and output node pins.
For example, a node can have 3 input ports and 2 output pins. Each of the
input/output pins can have many edges to other graph nodes. Every node pin has
an affiliated node pid. In the code, every node_pin has a `Port_ID`. 

A pair of driver pin and sink pin constitutes an edge. In the
following API example, an edge is connected from a driver pin (pid1) to a sink
pin (pid3). The bitwidth of the driver pin determines the edge bitwidth.


### Node, Node_pin, and Edge Construction 

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
new_node = lg->create_node_const(value)
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
driver_pin.connect(sink_pin);
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

### Non-Hierarchical Traversal Iterators

LGraph allows forward and backward traversals in the nodes (bidirectional
graph). The reason is that some algorithms need a forward and some a backward
traversal, being bidirectional would help. Whenever possible, the fast iterator
should be used.

```cpp
for (const auto &node:lg->fast())     {...} // unordered but very fast traversal

for (const auto &node:lg->forward())  {...} // propagates forward from each input/constant

for (const auto &node:lg->backward()) {...} // propagates backward from each output
```

The LGraph iterator such as `for(auto node: g->forward())` do not visit graph
input and outputs.

```
// simple way using lambda
lg->each_graph_input([&](const Node_pin &pin){

  //your operation with graph_input node_pin;

});
```

### Hierarchical Traversal Iterators

LGraph supports hierarchical traversal. Each sub-module of a hierarchical
design will be transformed into a new LGraph and represented as a sub-graph node
in the parent module. If the hierarchical traversal is used, every time the
iterator encounters a sub-graph node, it will load the sub-graph persistent
tables to the memory and traverse the subgraph recursively, ignoring the
sub-graph input/outputs.  This cross-module traversal treats the hierarchical
netlist just like a flattened design. In this way, all integrated third-party
tools could automatically achieve global design optimization or analysis by
leveraging the LGraph hierarchical traversal feature.

```cpp
for (const auto &node:lg->forward_hier()) {...}
```


### Edge Iterators

To iterate over the input edges of node, simply call:

```cpp
for (const auto &inp_edge : node.inp_edges()) {...}
```

And for output edges:

```cpp
for (const auto &out_edge : node.out_edges()) {...}
```


## LGraph Attribute Design
Design attribute stands for the characteristic given to a LGraph node or node
pin. For instance, the characteristic of a node name and node physical
placement. Despite a single LGraph stands for a particular module, it could be
instantiated multiple times. In this case, same module could have different
attribute at different hierarchy of the netlist. A good design of attribute
structure should be able to represent both non-hierarchical and hierarchical
characteristic.


### Non-Hierarchical Attribute
Non-hierarchical LGraph attributes include pin name, node name and line of
source code. Such properties should be the same across different LGraph
instantia- tions. Two instantiations of the same LGraph module will have the
exact same user-defined node name on every node. For example, instantiations of
a subgraph-2 in both top and subgraph-1 would maintain the same non-hierarchical
attribute table.  

```cpp
node.set_name(std::string_view name);
```


### Hierarchical Attribute
LGraph also support hierarchical attribute. It is achieved by using a tree data
structure to record the design hierarchy. In LGraph, every graph has a unique
id (lg_id), every instantiation of a graph would form some nodes in the tree and
every tree node is indexed by a unique hierarchical id (hid). We are able to
identify a unique instantiation of a graph and generate its own hierarchical
attribute table. An example of hierarchical attribute is wire-delay.

```cpp
node_pin.set_delay(float delay);
```


## LGraph Node Type Semantics


For each LGraph node, there is a specific semantic. This section explains the
operation to perform for each node. It includes a precise way to compute the
maximum and minimum value for the output. In most cases, if the minimum value
can not be negative, the result is unsigned.


The document also explains corner cases in relationship to Verilog and how to
convert to/from Verilog semantics. In general the nodes have a single output
with the exception of complex nodes like subgraphs or memories.


Cells with single output, have 'Y' as output. The inputs are single characters
'A', 'B'... For most inputs, there can be many drivers. E.g: a single Sum cell
can do `Y=3+20+a0+a3` where `A_{0} = 3`, `A_{1} = 20`, `A_{2} = a0`, and `A_{3}
= a3`.

If an input can not have multiple drivers, a lower case name is used ('a',
'b'...). E.g: the right shift cell is `Y=a>>b` because only one driver can
connect to 'a' and 'b'.


The section includes description on how to compute the maximum (`max`) and
minimum (`min`) allowed result range. This is used by the bitwidth inference
pass. To easy the explanation, a `sign` value means that the result may be
negative (`sign = result.min<0`). `known` is true if the result sign is known
(`known = result.max<0 or result.min>=0`), either positive or negative (`neg`).

For any value (`val`), the number of bits required (`bits`) is `val.bits =
log2(absmax(val.max,val.min))+val.sign?1:0`.

### Sum_op

Addition and substraction node is a single node that performs 2-complement
additions and substractions with unlimited precision.

```{.graph .center caption="Sum LGraph Node."}
digraph Sum {
    rankdir=LR;
    size="1,0.5"

    node [shape = circle]; Sum;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> Sum [ label ="A" ];
    q1 -> Sum [ label ="B" ];
    Sum  -> q [ label = "Y" ];
}
```

If the inputs do not have the same size, they are extended (sign or unsigned)
to all have the same length.

#### Forward Propagation

* $Y = \sum_{i=0}^{\infty} A_{i} - \sum_{i=0}^{\infty} B_{i}$
* $Y.max = \sum_{i=0}^{\infty} A_{i}.max - \sum_{i=0}^{\infty} B_{i}.min$
* $Y.min = \sum_{i=0}^{\infty} A_{i}.min - \sum_{i=0}^{\infty} B_{i}.max$

#### Backward Propagation

Backward propagation is possible when all the inputs but one are known. If all
the inputs have known size. The algorithm can check and look for the inputs
that have more precision than needed and reduce the max/min backwards.

For example, if and all the inputs but one A ($A_{0}$) are known:

* $A_{0}.max = Y.max - \sum{i=1}^{\infty} A_{i}.min + \sum_{i=0}^{\infty} B_{i}.max$
* $A_{0}.min = Y.min - \sum{i=1}^{\infty} A_{i}.max + \sum_{i=0}^{\infty} B_{i}.min$

If and all the inputs but one B ($B_{0}$) are known:

* $B_{0}.max = \sum{i=0}^{\infty} A_{i}.max - \sum_{i=1}^{\infty} B_{i}.min - Y.min$
* $B_{0}.min = \sum{i=0}^{\infty} A_{i}.min - \sum_{i=1}^{\infty} B_{i}.max - Y.max$

#### Verilog Considerations

In Verilog, the addition is unsigned if any of the inputs is unsigned. If any
input is unsigned. all the inputs will be "unsigned extended" to match the
largest value. This is different from Sum_Op semantics were each input is
signed or unsigned extended independent of the other inputs. To match the
semantics, when mixing signed and unsigned, the signed inputs connected to
Sum_Op with less than the maximum number of bits should be 1 bit zero extended.
An easy way to zero extend a number if with the Join_Op (Join_Op has an
unsigned output if the last input is unsigned or zero).


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

#### Peephole Optimizations

* `Y = x-0+0+...` becomes `Y = x+...`
* `Y = x-x+...` becomes `Y = ...`
* `Y = x+x+...` becomes `Y = (x<<1)+...`
* `Y = (x<<n)+(y<<m)` where m>n becomes `Y = (x+y<<(m-n)<<n`
* `Y = (~x)+1+...` becomes `Y = ...-x`
* `Y = a + (b<<n)` becomes `Y = {(a>>n)+b, a&((1<<n)-1)}`
* `Y = a - (b<<n)` becomes `Y = {(a>>n)-b, a&((1<<n)-1)}`
* If every x,y... lower bit is zero `Y=x+y+...` becomes Y=((x>>1)+(y>>1)+..)<<1

### Mult_op

Multiply operator. There is no Prod_Op that combines multiplication and
division because unlike in Sum_Op, in integer operations the order matters
(`a*(b/c) != (a*b)/c`).

```{.graph .center caption="Multiply LGraph Node."}
digraph Mult {
    rankdir=LR;
    size="1,0.5"

    node [shape = circle]; Mult;
    node [shape = point ]; q0
    node [shape = point ]; q

    q0 -> Mult [ label ="A" ];
    Mult  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \prod_{i=0}^{\infty} A_{i}$
* $Tmax = \prod_{i=0}^{\infty} \text{maxabs}(A_{i}.max, A_{i}.min)$
* $Tmin = \prod_{i=0}^{\infty} \text{minabs}(A_{i}.max, A_{i}.min)$
* $neg  = \prod_{i=0}^{\infty} A_{i}.sign$
* $known = \forall_{i=0}^{\infty} A_{i}.known$

* $Y.max = \begin{cases} -Tmin &      neg  \land known \\
                          Tmax & \text{otherwise} \end{cases}$

* $Y.min = \begin{cases}  Tmin & \overline{neg}  \land known \\
                         -Tmax & \text{otherwise} \end{cases}$

When the result sign is not known, the max/min is conservatively computed.

#### Backward Propagation

If only one input is missing, it is possible to infer the max/min from the
output and the other inputs. As usual, if all the inputs and outputs are known,
it is possible to backward propagate to further constraint the inputs.


* $Tmax = \frac{\prod_{i=1}^{\infty} \text{maxabs}(A_{i}.max, A_{i}.min)}{Y.min}$
* $Tmin = \frac{\prod_{i=1}^{\infty} \text{minabs}(A_{i}.max, A_{i}.min)}{Y.max}$
* $neg  = Y.sign \times \prod_{i=1}^{\infty} A_{i}.sign$
* $known = Y.known \land \forall_{i=1}^{\infty} A_{i}.known$
* $A_{0}.max = \begin{cases} -Tmin &      neg  \land known \\
                              Tmax & \text{otherwise} \end{cases}$
* $A_{0}.min = \begin{cases}  Tmin & \overline{neg}  \land known \\
                             -Tmax & \text{otherwise} \end{cases}$


### Verilog Considerations

Unlike the Sum_Op, the Verilog 2 LiveHD translation does not need to extend the
inputs to have matching sizes.  Multiplying/dividing signed and unsigned
numbers has the same result. The bit representation is the same if the result
was signed or unsigned.

LiveHD mult node result (Y) number of bits can be more efficient than in
Verilog.  E.g: if the max value of A0 is 3 (2 bits) and A1 is 5 (3bits). If the
result is unsigned, the maximum result is 15 (4 bits). In Verilog, the result
will always be 5 bits. If the Verilog result was to an unsigned variable.
Either all the inputs were unsigned, or there should be a Join_Op with 1bit
zero to force the MSB as positive. This extra bit will be simplified but it
will notify LGraph that the output is to be treated as unsigned.

#### Peephole Optimizations

* `Y = a*1*...` becomes `Y=a*...`
* `Y = a*0*...` becomes `Y=0`
* `Y = power2a*...` becomes `Y=(...)<<log2(power2a)`
* `Y = (power2a+power2b)*...` becomes `tmp=... ; Y = (tmp+tmp<<power2b)<<(power2a-power2b)` when power2a>power2b
* `Y = (power2a-power2b)*...` becomes `tmp=... ; Y = (tmp-tmp<<power2b)<<(power2a-power2b)` when power2a>power2b

### Div_op

Division operator. The division operation is quite similar to the inverse of the multiplication, but a key difference is that only one driver is allowed for each input ('a' vs 'A').

```{.graph .center caption="Division LGraph Node."}
digraph Div {
    rankdir=LR;
    size="1,0.5"

    node [shape = circle]; Div;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> Div [ label ="a" ];
    q1 -> Div [ label ="b" ];
    Div  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \frac{a}{b}$
* $Tmax = \frac{\text{maxabs}(a.max,a.min)}{\text{minabs}(b.max,b.min)}$
* $Tmin = \frac{\text{minabs}(a.max,a.min)}{\text{maxabs}(b.max,b.min)}$
* $known = a.known \land \b.known$
* $neg   = a.sign \times b.sign$
* $Y.max = \begin{cases} -Tmin &      neg  \land known \\
                          Tmax & \text{otherwise} \end{cases}$
* $Y.min = \begin{cases}  Tmin & \overline{neg}  \land known \\
                         -Tmax & \text{otherwise} \end{cases}$

#### Backward Propagation

The backward propagation from the division can extracted from the forward
propagation. It is a simpler case of multiplication backward propagation.

### Verilog Considerations

The same considerations as in the multiplication should be applied.

#### Peephole Optimizations

* `Y = a/1` becomes `Y=a`
* `Y = 0/b` becomes `Y=0`
* `Y = a/power2b` becomes `Y=a>>log2(power2b)` if `Y.known and !Y.neg`
* `Y = a/power2b` becomes `Y=1+~(a>>log2(power2b))` if `Y.known and Y.neg`
* `Y = (x*c)/a` if c.bits>a.bits becomes `Y = x * (c/a)` which should be a smaller division.
* If b is a constant and `Y.known and !Y.neg`. From the hackers delight, we know that the division can be changed for a multiplication `Y=(a*(((1<<(a.__bits+2)))/b+1))>>(a.__bits+2)`
* If a sign is not `known`. Then `Y = Y.neg? (~Y_unsigned+1):Y_unsigned`


#### Modulo

There is no module cell in LGraph. The reason is that a modulo different from a
power of 2 is very rare in hardware. If the language supports modulo
operations, they must be translated to division/multiplication.


```
 y = a mod b
```

It is the same as:

```
 y = a-b*(a/b)
```

If b is a power of 2, the division optimization will transform the modulo operation to:

```
y = a - (a>>n)<<n
```

The add optimization should reduce it to:

```
y = a & ((1<<n)-1)
```

### Not_op

Bitwise Not operator

```{.graph .center caption="Node LGraph Node."}
digraph Mod {
    rankdir=LR;
    size="1,0.5"

    node [shape = circle]; Not;
    node [shape = point ]; q0
    node [shape = point ]; q

    q0 -> Not [ label ="a" ];
    Not  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \text{bitwise-not}(\text{a})$
* $Y.max = \text{max}(~a.max,~a.min)$
* $Y.min = \text{min}(~a.max,~a.min)$

#### Backward Propagation

* $a.max = \text{max}(~Y.max,~Y.min)$
* $a.min = \text{min}(~Y.max,~Y.min)$

### Verilog Considerations

Same semantics as verilog


#### Peephole Optimizations

No optimizations by itself, it has a single input. Other operations like Sum_Op can optimize when combined with Not_Op.


### Sext_op

This operatator sign extends the input picking the 'b' bit as most significant
or sign bit. If the input has more bits, all the upper bits are cleared or set
depending on the sign extension.

```{.graph .center caption="Sext LGraph Node."}
digraph Sext {
    rankdir=LR;
    size="1,0.5"

    node [shape = circle]; cell;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q

    q0 -> cell [ label ="a" ];
    q1 -> cell [ label ="b" ];
    cell  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = \begin{cases} a\&((1\ll b)-1)  & \text{if}\ a[b]==0 \\ 
                           -(a\&((1\ll b)-1)) & \text{otherwise} \end{cases}$
* $Y.max = \text{min}(a.max,(1<<b)-1)$
* $Y.min = \text{max}(a.min,-(1<<b)$

#### Backward Propagation

If the driver to Sext_Op only drives this node, the same Y.max and Y.min can be
propagated to the VAL.max and VAL.min.  The reason is that the upper dropped
bits were not used anyway, so no need to have all those bits computed.

* $a.max = Y.max$
* $a.min = Y.min$

#### Other Considerations

The Sext_Op is similar to the Pick_Op when `b` is zero and `b` is the same. The
difference is the sign extension. For Pick_Op, the result is always unsigned,
for Sext_Op, the result can be signed.


#### Peephole Optimizations

* `Y = sext(a,b)` if `maxabs(a.max,a.min) < (1<<b)` becomes `Y=a`
* `Y = sext(a,b)` if `a.min>=0` becomes `Y=a&(1<<b)-1`


### Join_op

Join or concatenate operator. The output keeps the sign of the most last or
most significant input.  This means that if the upper value is negative, the
result is negative.

```{.graph .center caption="Join LGraph Node."}
digraph Join {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Join;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q2
    node [shape = point ]; q

    q0 -> Join [ label ="a" ];
    q1 -> Join [ label ="b" ];
    q2 -> Join [ label ="c" ];
    Join  -> q [ label = "Y" ];
}
```

#### Forward Propagation

* $Y = ... c<<(b.bits+a._bits) | b<<(a.bits) | a$
* $Y.max = \begin{cases} c.max<<(b.bits+a.bits) & c.max \ne 0 \\
                         b.max<<(a.bits)        & b.max \be 0 \\
                         a.max                  & otherwise \end{cases}$
* $Y.min = \begin{cases} c.min<<(b.bits+a.bits) & c.min \ne 0 \\
                         b.min<<(a.bits)        & b.min \be 0 \\
                         a.min                  & otherwise \end{cases}$

#### Backward Propagation

Back propagation is possible when only one input is unknown. It is conservative
because the max/min for the intermediate values are gone.

* $b.max =  \frac{Y.max}{c.max<<(a.bits)}$ if b is not the highest value
* $b.min = -\frac{Y.min}{c.min<<(a.bits)}$ if b is not the highest value

* $c.max =  \frac{Y.max}{1<<(a.bits+b.bits)}$ if c is the highest value
* $c.min =  \frac{Y.min}{1<<(a.bits+b.bits)}$ if c is the highest value


#### Other Considerations

Join_Op has two uses besides concatenating values. It is the way to implement a left shift
and it is the way to create an unsigned value out of a signed value.

Join_Op(xx,0u3bits) is a left shift by adding as many zeros as the `a` has.

Join_Op can not be used to created an unsigned value. Join_Op(0,x) is the same as x.


#### Peephole Optimizations

* `Y = Join(0,a,b)` becomes `Y= Join(a,b)`
* `Y = Join(a,0)` is the implementation for `a<<n` where `n=0.bits`

### Pick_op

Pick some bits from the VAL input pin
Y = VAL[[OFF..(OFF+Y.__bits)]]}

#### Other Considerations

The Pick_Op operation performs unsigned right shift even for signed inputs $Y = Pick_Op(VAL, OFF, 0)$ behaves like a $Y=VAL>>OFF$.

Pick selects some bits from the source (VAL). It can be used as an unsigned right shift. The Pick_Op result is always unsigned.

### ShiftRigt_op

Logical or sign extension shift right.

### Mux_op

### LUT_op

### And_op

reduce AND `a =u= -1` // unsigned equal

### Or_op

reduce OR `a != 0` 

### Xor_op

reduce xor is a chain of XORs.

### Const_op

### SFlop_op

### AFlop_op

### FFlop_op

### Latch_op

### Memory_op

Memory is the basic block to represent SRAM-like structures. Any large storage will benefit of using memory arrays instead of slower to simulate set of flops. The memories are highly configurable.


```{.graph .center caption="Memory LGraph Node."}
digraph Join {
    rankdir=LR;
    size="2,1"

    node [shape = circle]; Memory;
    node [shape = point ]; q0
    node [shape = point ]; q1
    node [shape = point ]; q2
    node [shape = point ]; q

    q0 -> Join [ label ="s" ];
    q1 -> Join [ label ="b" ];
    q2 -> Join [ label ="p" ];
    Join  -> q [ label = "Y" ];
}
```

* `s` is for the array size in number of entries
* `b` is the number of bits per entry
* `p` is the read, write, or read/write ports connecting to the memory
* `q` is the read data out of the memory for the read ports

Both `p` and `q` are arrays to support multiported memories. The order of the
ports do not change semantics.

Each port `p` has the following entries (all entries must be populated):

* `clk_pin` points to the clock driver pin
* `posedge` points to a 1/0 constant driver pin
* `enable`  points to the driver pin for read/write enable.
* `fwd`   points to a 0/1 constant driver pin to indicate if writes forward
  value (`0b0` for write-only ports). Effectively, it means zero cycles read
latency when enabled. `fwd` is more than just setting `latency=0`. Even with
latency zero, the write delay affects until the result is visible. With `fwd`
enabled, the write latency does not matter to observe the results. This
requires a costly forwarding logic.
* `latency` points to an integer constant driver pin (2 bits). For writes `latency from 1 to 3`, for reads `latency from 0 to 3`
* `nwmask`  Points to the write mask (0 == write, 1==no write). The mask bust be a big as the number of bits per entry (`b`)
* `wmode`   points to the driver pin or switching between read and write mode (single bit)
* `addr`    points to the driver pin for the address. The address bits should match the array size (`ceil(log2(s))`)
* `data`    points to the write data driver pin (read result is in `q` port). Connected to `0b0` for read-only ports

All the ports must be populated with the correct size. This is important
because some modules access the field by bit position.  It not used point to a
zero constant with the correct number of bits. E.g: a 8bit per entry (`b`)
array needs a 8 bit zero `nwmask` (`nwmask = 0u8bits`). Setting wmask to `0b0`
will mean a 1 bit zero, and the memory will be incorrectly operated. `clk_pin`
is the least significant bit of the `p` configuration.

The memory usually has power of two sizes. If the size is not a power of 2, the
address is rounded up. Writes to the invalid addresses will generated random
memory updates. Reads should read random data.

#### Forward Propagation

#### Backward Propagation

#### Other Considerations

#### Peephole Optimizations

### SubGraph_op


And_Op: bitwise AND with 2 outputs single bit reduction (RED) or bitwise
Y = VAL&..&VAL ; RED= &Y


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
