This document describes the design, implementation, and reasoning behind the latest changes to the inou.json.fromlg command. This command can be invoked to generate a JSON representation file of a given LGraph netlist that Yosys can parse.

This pass is intended to create a JSON file that can nextpnr can use to Place and Route a netlist onto an ice40 FPGA (from Lattice). Currently, as described under section "Future Work", we cannot fully achieve this goal, so this inou pass remains a Work in Progress.

# Invocation

Before executing the pass "inou.json.fromlg" on a given LGraph, you must first create the LGraph from the source Verilog file (if not done already):

```sh
livehd> inou.yosys.tolg files:./${VERILOG_PATH}/${TOP_MOD_NAME}.v top:${TOP_MOD_NAME}
```

Where you replace "${TOP_MOD_NAME}" with the name of the top graph of your design. As a reminder, by default, lgdb/ is the path that stores the LGraph.

Once the LGraph gets created, to invoke the pass "inou.json.fromlg", the following command is used:

```sh
livehd> lgraph.open name:${TOP_MOD_NAME} |> inou.json.fromlg odir:lgdb/
```

This command will dump the top-level graph and all its subgraphs into a file named "${TOP_MOD_NAME}.json". This JSON file can then be parsed by Yosys; it could even be parsed by nextpnr if Mockturtle was used before inou.json.fromlg.

# Expected Output

We are attempting to replicate the JSON output of Yosys; we can obtain a baseline reference by performing the below command in a Unix terminal:

```sh
bash> MOD=${TOP_MOD_NAME}; yosys -p 'write_json '${MOD}.json ${PATH}/${MOD}.v
```

The above command synthesizes ${TOP_MOD_NAME}.v and dumps the result into a JSON file. Of note, this is a generic JSON output; we cannot input it into nextpnr. If we wanted to create a JSON for nextpnr, we would instead call:

```sh
bash> MOD=${TOP_MOD_NAME}; yosys -p 'synth_ice40 -noflatten -top '${MOD}' -json ${TOP_MOD_NAME}.json' ${MOD}.v
```

For designing inou.json.fromlg, I would study the output JSON of the first command and attempt to replicate the output within LiveHD.

If one wants to compare the two flows, they may:
1. Write or obtain an example Verilog module (along with any needed submodule(s)).
2. Synthesize the design with Yosys to create one JSON file.
3. Use the LiveHD flow to create another JSON file
4. Compare output files.

# fromlg Translation Methodology

## Translating from a Bus-based to a Net-based Circuit Architecture

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

However, when synthesizing a circuit to be physically instantiated, we must describe each independent net separately so that the Placer and Router know which pins to connect to which other pins. To translate LGraph to this scheme, we use a C++ class named "Inou_Tojson" that handles the conversion from LGraph to Yosys-standard JSON (next section).

## Operation Translations

Unfortunately, inou.json.fromlg currently does not successfully translate most LGraph operations to their Yosys equivalents due to the following LGraph property:

"For any given LGraph Node that performs a commutative operation (e.g. ADD, AND, OR, etc.), there exists only one input Node_pin, and all input edges connect to that single pin."

More about this is described under "Future Work", and we hope to swiftly resolve this issue.

# class Inou_Tojson

In the entry function for the JSON dump pass, we instantiate an object from the "Inou_Tojson" class; we use this object to write the JSON representation of each graph for a given top-level module.

Before reading this section, one should first review the basic API calls for the "LGraph" class under "LGraph API Functions Used". We have to write subgraphs sequentially and atomically to JSON, so we must do the following:
1. We cannot recurse on subgraphs; if we encounter a Node that instantiates a module (where the Node_type == SubGraph_Op), we only write the instantiation information (e.g. the instantiation name, parameters, connectivity with the rest of the module, etc).
2. We have to write one graph at a time (as we cannot begin writing one subgraph to JSON until we've completed writing all previous subgraphs), so we must create a vector uniquely listing all graphs.

For example, let's say we have the following:
1. We have three Verilog modules: A, B, and C.
2. A is the top module and instantiates two instances of B and C and some miscellaneous boolean logic.
3. Inside of B, there are four instantiations of C and some miscellaneous logic.
4. Inside of C, there is only boolean logic.
In this example, we have three graphs: A, B, and C. A is the top-level graph passed into the flow, and we must create a vector listing A, B, and C before creating the Inou_Tojson object. This is achieved via "lg->each_sub_unique_fast()".

Note: were we not to initially track all the graphs at the start, we would have to use recursive data structures within the private fields. This would prove too complex and costly (memory/CPU wise).

## rapidjson::PrettyWriter

```cpp
using PrettySbuffWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;
```

inou.json.fromlg uses the [rapidjson library](https://rapidjson.org/) to write the JSON dictionaries. The API functions are very self-explanatory; thus, we will exclude describing how to use this library.

Nevertheless, to use this library, we maintain an object derived from the above type (which is typedef-ed via a "using" statement to ease development).

Since we write each component of the JSON file sequentially (i.e. we never rewind to previous points of the JSON dump), we keep a sole reference to a single PrettySbuffWriter object within the Inou_Tojson class.

## Private Fields

To map LGraph to JSON, we need to obtain the following metadata per LGraph:
1. A "set of set of nets". Recall the Verilog example posted under "fromlg Translation Methodology". In it, we have "inpA", "inpB", and "outnet". Each of these three names are a set of nets (with widths 8, 8, and 14 respectively). When dumping into JSON, we need to translate each individual net into an integer, then write those integers inside an array within the "connections" property of a given cell.
2. A list of cells for a given graph.

To track the integers required for (1), we use the following typedef:

```cpp
using IPair = std::pair<uint32_t, uint32_t>;
```

1. IPair.first states the starting index of a net set.
2. IPair.second states the width of that net.

```cpp
using Cells = std::vector<Node::Compact_class>;
```

The above typedef short-hands writing code for recording each node in order to process later on via "write_cells()".

TODO

## Public Methods

```cpp
Inou_Tojson(LGraph *toplg_, PrettySbuffWriter &writer_);
```

Creates the Inou_Tojson object; we keep a reference to the top-level graph and JSON writer object.

```cpp
int dump_graph(Lg_type_id lgid);
```

For each LGraph, we obtain their lgid via toplg->get_id() (for the top level graph) or toplg->each_sub_unique_fast() (for the subgraphs). We call dump_graph() N times, one per lgid. Each dump_graph() call will write the entire "module"

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

TODO

## Node_type

TODO

## Node_pin

TODO

## Edge

TODO

# Future Work

## Operation Translations

This pass currently fails to create a JSON file that can be correctly parsed by Yosys; we still need to figure out certain characteristics and translation methods for our internal Op nodes.

For instance, if we have an arbitrarily-sized 3 input adder in LGraph, we would need to devise a strategy of translating that 3 input adder to be parsable by Yosys (where the latter expects only 2 input adders with a certain maximum length).

## fromjson tolg

As part of a stretch goal for this quarter, we did not prioritize rewriting inou.json.tolg, since our goal is to use LiveHD to synthesize netlists and output a file that can be inserted into a Place and Route tool (such as nextpnr). For now, inou.json.tolg will immediately exit with a Pass::error, but we will rewrite this command over Summer 2020.

## TMapping

In the Yosys tool, you can specify an FPGA chip to synthesize one's netlist for, then you can output a file that can be parsed by a PnR tool. Currently, we are roughly 90% complete in creating a JSON file that can be parsed by Yosys, and we have a pass named "Mockturtle" that translates most operations into sets of LUTs, but we need to further develop LiveHD to be able to synthesize arbitrary netlists into a form that can commence Place and Route for FPGAs.

Note that this issue and "Operation Translations" are closely intertwined, since we have to know what physical chip or virtual circuit description to target in order to generate the correct corresponding output.

