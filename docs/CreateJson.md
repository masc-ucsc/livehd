This document describes the design, implementation, and reasoning behind the latest changes to the inou.json.fromlg command. This command can be invoked to generate a JSON representation file of a given LGraph netlist that Yosys can parse.

This pass is intended to create a JSON file that can nextpnr can use to Place and Route a netlist onto an ice40 FPGA (from Lattice). Currently, as described under section "Future Work", we cannot fully achieve this goal, so this inou pass remains a Work in Progress.

# fromlg Translation Methodology

## Main Challenge 1: Translating from a Bus-based to a Net-based Circuit Architecture

The main difference between LGraph and the output JSON file that Yosys expects is that LGraph relies on bus edges between Node_pins, but Yosys expects individual nets that connect independent circuit nodes.

To demonstrate the significance of the above, observe the following circuit:

![alt text](https://users.soe.ucsc.edu/~crhilber/net_v_edges.png)

In the above circuit, we have input ports "Axx" and "Bxx", both which output 8-bit digital signals. These two signals feed two separate adders.

In a net-based architecture, we have 16 nets: Axx[7:0] and Bxx[7:0].

In a bus-based architecture, we have 4 edges:
1. An edge connecting Axx to the top adder.
2. Axx to the bottom adder.
3. Bxx to the top adder.
4. Bxx to the bottom adder.

1 and 2 will have the same driver pin (same for 3 and 4), but 1 and 3 will have the same sink pin (same for 2 and 4).

In addition to the above, if we either truncate or concatenate nets in Verilog, we must instantiate a Pick_Op or a Join_Op, respectively. Observe the following Verilog snippet:

```v
input [7:0] inpA;
input [7:0] inpB;
output reg [13:0] outnet;

always @* begin
  outnet = {2'b10, inpA[6:3], inpB};
end
```

The above circuit in LGraph gets translated to the below diagram:

![alt text](https://users.soe.ucsc.edu/~crhilber/lgraph_pick_join.png)

Note that we truncate bits 3 thru 6 for inpA via the "Pick_Op". This is a "Node" within a particular LGraph that has the following properties:
1. The number of bits we are truncating is given by the edge bit_width() of the output Node_pin of the Node.
2. The bit position we are selecting is given by the OFFSET input, where we must connect a U32Const_Op whose value equals the selected position. Note that the edge bit_width() that connects the output of the constant and the offset pin is equal to floor(log_2(value)) + 1.

For combining multiple edges into one edge, LGraph instantiates a "Join_Op" node that takes multiple edges and combines them into one. Thus, if we wanted to figure out where each index of "outnet" originated from (e.g. outnet[8] = inpA[3]), we would need to backtrace the "outnet" edge and smartly iterate over the input pins of the Join_Op.

Furthermore, if we had an even more complicated truncation, such as "some_wire = {inpA[6:3], inpA[7]}", we would need to have two Pick_Ops and one Join_Op to instantiate the aforementioned Verilog line.

Nevertheless, the complexities of LGraph's bus-based architecture are justified because we save on memory space and CPU operations. This property comes about since logical circuits follow a bus-based format as well; were we to operate on a per-net basis, we would spend much more memory and CPU cycles to describe connections that could simply be bundled instead.

However, when synthesizing a circuit to be physically instantiated, we must describe each independent net separately so that the Placer and Router know which pins to connect to which other pins. To translate LGraph to this scheme, we use a C++ class named "Inou_Tojson" that handles the conversion from LGraph to Yosys-standard JSON.

# class Inou_Tojson

## Public Methods

```cpp
Inou_Tojson(LGraph *toplg_, PrettySbuffWriter &writer_)
```

Creates the Inou_Tojson object. We use this object to store metadata for each LGraph that aids in tracking all the individual nets.

## Private Fields

TODO

# LGraph API Functions Used

Note that the following descriptions assumes that the reader of this page is already familiar with C++ lambda functions.

## LGraph

The "LGraph" class directly represents the modules written in Verilog. We use "LGraph" to represent the inputs, outputs, and internal "nodes" of each module (more on nodes in the next subsection); all of these can be iterated with the following methods.

```
LGraph *lg;
```

We operate on LGraphs by maintaining a pointer to the object which lists the metadata associated with the module. For the inou.json.* functions, we receive a pointer to the top-level LGraph from the "Eprp_var &var" variable.

```cpp
lg->get_name();
```

Obtains the name of the name of the current LGraph pointed to by lg. Note that names can refer to a "real" module instantiated in the user-written Verilog files, or it can refer to a "black-box" module created by either Yosys or LGraph. In the latter case, we would want to set the "hide_name" attribute to true.

```cpp
lg->each_sub_unique_fast([&](Node&, Lg_type_id lgid) { /**/ });
```

A loop that calls a given lambda function on each of the subgraphs, and their subgraphs recursively; these subgraphs are represented via a unique index (LG_type_id lgid). In order to retrieve the corresponding LGraph pointer for each subgraph, we must call the following:

```cpp
LGraph *lg = LGraph::open(toplg->get_path(), lgid);
```

This function returns the LGraph pointer that corresponds to lgid, but we must know that LGraph's path. To find out the path, we maintain a pointer to the top-level LGraph, "toplg", within Inou_Tojson, then we call "lg->get_path()" to retrieve the path.

Within the tojson pass, we create a vector that contains the top-level graph and all of the subgraphs, then we iteratively call Inou_Tojson::dump_graph(LGraph *lg) on each element of that vector.

```cpp
lg->each_graph_input([&](const Node_pin &input_pin) { /**/ });
```

A loop which calls a lambda function on each of the input Node_pins (more on these later in the document). These pins are named driver pins that correspond to the Verilog module's input ports. We can thus retrieve the names of a module's inputs by using this function on the corresponding LGraph and calling "input_pin->get_name()".

Note that using the following method will not retrieve the same Node_pins, so beware:

```cpp
ginp_node = lg->get_graph_input_node();
for (const auto &out_edge : ginp_node.out_edges()) {
  Node_pin driver_pin = out_edge.driver;
  // ...
}
```

This typically occurs when we perform a split on an input edge.

```cpp
lg->each_graph_output([&](const Node_pin &output_pin) { /**/ });
```

Same as above, except with output ports of a module.

```cpp
for (const auto &node: lg->forward()) { /**/ }
```

This function iterates on each of the "Node"s within a given subgraph (more on Nodes in the subsection below). Note that subgraphs are instantiated as Nodes with a Node_type set to "SubGraph_Op". Furthermore, tojson performs no flattening.

## Node



## Node_type



## Node_pin



## Edge



# Future Work

## Operation Translations

This pass currently fails to create a JSON file that can be correctly parsed by Yosys; we still need to figure out certain characteristics and translation methods for our internal Op nodes.

For instance, if we have an arbitrarily-sized 3 input adder in LGraph, we would need to devise a strategy of translating that 3 input adder to be parsable by Yosys (where the latter expects only 2 input adders with a certain maximum length).

## fromjson tolg

As part of a stretch goal for this quarter, we did not prioritize rewriting inou.json.tolg, since our goal is to use LiveHD to synthesize netlists and output a file that can be inserted into a Place and Route tool (such as nextpnr). For now, inou.json.tolg will immediately exit with a Pass::error, but we will rewrite this command over Summer 2020.

## TMapping

In the Yosys tool, you can specify an FPGA chip to synthesize one's netlist for, then you can output a file that can be parsed by a PnR tool. Currently, we are roughly 90% complete in creating a JSON file that can be parsed by Yosys, and we have a pass named "Mockturtle" that translates most operations into sets of LUTs, but we need to further develop LiveHD to be able to synthesize arbitrary netlists into a form that can commence Place and Route for FPGAs.

