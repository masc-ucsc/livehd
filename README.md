
# LGraph


Code quality: [![CodeFactor](https://www.codefactor.io/repository/github/masc-ucsc/lgraph/badge)](https://www.codefactor.io/repository/github/masc-ucsc/lgraph)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4cae3de3de714e13b6003002f74b7375)](https://www.codacy.com/app/renau/lgraph?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=masc-ucsc/lgraph&amp;utm_campaign=Badge_Grade)
[![Coverage Status](https://coveralls.io/repos/github/masc-ucsc/lgraph/badge.svg?branch=HEAD)](https://coveralls.io/github/masc-ucsc/lgraph?branch=HEAD)
[![Coverage Status](https://coveralls.io/repos/github/masc-ucsc/lgraph/badge.svg?branch=HEAD&service=github)](https://coveralls.io/github/masc-ucsc/lgraph?branch=HEAD)
Short CI: [![Build Status](https://travis-ci.org/masc-ucsc/lgraph.svg?branch=master)](https://travis-ci.org/masc-ucsc/lgraph)
Long CI: [![Build Status](https://dev.azure.com/renau0400/renau/_apis/build/status/masc-ucsc.lgraph)](https://dev.azure.com/renau0400/renau/_build/latest?definitionId=1)

LGraph is a graph optimized for live synthesis (Live Synthesizes Graph or
LGraph for short). By live, we mean that small changes in the design should
have results in few seconds. The goal is that any code change can have its
synthesis and simulation setup ready under 30 seconds with a goal of under 4
seconds in most cases.

Notice that this is different goal from having a incremental synthesis were many
edges are added and removed. The typical graph reconstruction is in the order of
thousands of nodes.

## Building

LGraph uses bazel as a build system. [Bazel.md](docs/Bazel.md) has more details
about how to build, test, and debug with bazel.

For a simple release build:

```
$ bazel build //main:lgshell
```

## Structure

LGraph is optimized for synthesis, allowing forward and backward traversals in
the nodes (bidirectional graph). The reason is that some algorithms need a
forward and some a backward traversal. Being bidirectional helps.

The graph structure is based on synthesis graph requirements. Each conceptual
graph node has many inputs and outputs like a normal graph, but the inputs and
outputs are numbered. For example, a node can have 3 input ports and 2 output
ports. Each of the input and output ports can have many edges to other graph
nodes.

Each graph edge is between a specific graph node/port pair and another node/port
pair. The graph supports to add meta-information on each node and node/port
pair. The port identifier is an integer with up to 1024 (10 bits) value per
node. In the code, the port is a `Port_ID`.


The graph is build over a table structure. Each table entry is 64 bytes and
contains a full or part of a graph node information. To access the information,
we use the table entry number of `Index_ID`.


When a new node is added to the graph a new `Index_ID` is generated. The node
always has a `Index_ID` for the port zero, different `Index_ID` for other
node/port pairs, and potentially additional `Index_ID` for extra storage to keep
the graph edges. Each `Index_ID` can be used to store metainformation in
additional tables like the delay, or operation, but in reality we only store
information for the whole node or for each node/port pair.


The `Index_ID` that uniquely identifies the whole node is called `Node_ID` in
LGraph. This is typically accessed with methods like `get_nid()`.

The `Index_ID` that uniquely identifies a node/port pair is called `Outp_ID`
(Output Pair ID). This is typically accessed with methods like `get_oid()`. The
`Node_ID` and the `Outp_ID` is the same number when the port is zero.


When traversing the edges in the graph, it is possible to ask for:

```cpp
get_nid // Node_ID/Index_ID that uniquely identifies the node
get_oid // Outp_ID/Index_ID that uniquely identifies the node/port pair
get_idx // Index_ID raw index pointer where the info is stored
get_inp_pid // Port_ID Input port for this edge
get_out_pid // Port_ID for the output port driving this edge
```


A graph edge does not have a unique id. LGraph does not allow to store
meta-information for edges generic edges. It allows to store meta-information
for `Outp_ID`. This is different than edge because the same node/port output can
have many destinations and all have to share the same meta-information. This
would be an issue if we want to store information like resistance/capacitance
or distance per edge. If this becomes necessary, a potential solution would be
to modify the graph so that at most an output edge is inserted for each
`Index_ID`.

```
Index_ID // 37 bit index. Either a Outp_ID, Node_ID, or additional storage
Outp_ID  // 37 bit index, uniquely identifies a node/port pair
Node_ID  // 37 bit index, uniquely identifies a node
Port_ID  // 10bits, per node input/output port identifier
```

## Iterators

There are 3 types of iterators available over node is LGraph:

```cpp
for(auto nid : g->fast())     { } // unordered but very fast traversal

for(auto nid : g->forward())  { } // propagates forward from each input/constant

for(auto nid : g->backward()) { } // propagates backward from each output
```

Whenever possible, fast should be used. The type of `nid` is `Index_ID`.


### Edge iterators

To iterate over the input edges of node `nid` simply call:

```cpp
for(auto& edge : g->inp_edges(nid))
```

And for output edges:

```cpp
for(auto& edge : g->out_edges(nid))
```

Note that you *have* to use reference here (`&` required) since LGraph is
heavily optimized and uses memory positions. Not using reference would imply in
a copy-constructor, and thus a different memory position, generating an invalid
edge.


## InOu

InOus are inputs and/or outputs to/from LGraph. An input will create a graph,
eg., from a verilog description, an json representation, or randomly. Similarly,
an output will read an existing LGraph and generate an alternative
representation, eg., verilog or json.

Examples of inou can be found in inou/yosys (for verilog handling) and
inou/json.

## Passes

Passes are transformations over an existing LGraph. A pass will read an LGraph
and make changes to it. Usually this is done for optimizations. When creating a
new pass, use the binary from `pass/lgopt/`, examples of passes can be found in
`pass/lgopt_dce` which deletes any node that is not used by outputs of the
LGraph.


# Coding and contributing

## Style

For coding, please follow the coding styles from [Style.md](docs/Style.md). To contribute,
check [Policy.md](docs/Policy.md) document that explains how to create pull requests
and more details about license and copyrights. Also, contributors to LGraph are
expected to adhere to the [Code of Conduct](docs/CODE_OF_CONDUCT.md).

## Code Organization

The code is organized as:

- core/ # All the core classes of LGraph (nodes, edges, iterators, field tables, ...)
- meta/ # All the additional fields added to the nodes
- inou/ # All the inputs and outputs to and from LGraph
- pass/ # Transformations over LGraph
- cops/ # Combine operations, ie. take N graphs and creates another graph
- misc/ # External libraries and other misc code
- test/ # Testing code, scripts, cases and infrastructure (Note: unit tests
  should be placed inside the corresponding subfolder)
- docs/ # Documentation of LGraph

## Git Policies

Before pushing your code, make sure:

* The code builds `bazel build //...`
* The testbenches pass `bazel test //...`

Push frequently, if your code still has problems, use macros to turn parts of it
off:

```cpp
#if 0
//...
#endif
```

Pull at least once a day when working, LGraph is in active development.

Always target warning free compilation. It is okay to commit code that triggers
warning during development, but remember to clean up afterwards.

If you are not one of the code owners, you need to create a pull request as
indicated in [Policy.md](docs/Policy.md) and [GitHub-use.md](docs/GitHub-use.md).

