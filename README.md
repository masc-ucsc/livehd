
![LiveHD](https://masc.soe.ucsc.edu/logos/livehd5.png)

# LiveHD: Live Hardware Development

Code quality: [![CodeFactor](https://www.codefactor.io/repository/github/masc-ucsc/livehd/badge)](https://www.codefactor.io/repository/github/masc-ucsc/livehd)
[![Coverage Status](https://coveralls.io/repos/github/masc-ucsc/livehd/badge.svg?branch=HEAD&service=github)](https://coveralls.io/github/masc-ucsc/livehd?branch=HEAD)

Short CI: [![Build Status](https://travis-ci.org/masc-ucsc/livehd.svg?branch=master)](https://travis-ci.org/masc-ucsc/livehd)
Long CI: [![Build Status](https://dev.azure.com/renau0400/renau/_apis/build/status/masc-ucsc.livehd?branchName=master)](https://dev.azure.com/renau0400/renau/_build/latest?definitionId=2&branchName=master)

LiveHD is an infrastructure designed for Live Hardware Development. By live, we
mean that small changes in the design should have the synthesis and simulation
results in a few seconds, as the fast interactive systems usually response in
sub-second.

As the goal of "seconds," we do not need to perform too fine grain incremental
work. Notice that this is a different goal from having an typical incremental
synthesis, where many edges are added and removed in the order of thousands of
nodes/edges. 


## LiveHD Framework
LiveHD is optimized for synthesis and simulation. The main components of LiveHD
includes LGraph, LNAST, integrated 3rd-party tools, code generation, and "live"
techniques. The core of LiveHD is a graph structure called LGraph (Live Graph).
LGraph is built for fast synthesis and simulation, and interfaces other tools
like Yosys, ABC, OpenTimer, and Mockturtle. LNAST stands for language neutral
AST, which is a high-level IR on both front/back-end of LGraph. LNAST helps to
bridge different HDLs and HLS into LiveHD and is useful for HDLs/C++ code
generation.

![LiveHD overall flow](./docs/livehd.svg)

## Contribute to LiveHD
There is a list of available [projects.md](docs/projects.md) to further improve
LiveHD. If you want to contribute or seek for MS/undergraduate thesis projects, please
contact renau@ucsc.edu to query about them.

## Building

LiveHD uses bazel as a build system. [Bazel.md](docs/Bazel.md) has more details
about how to build, test, and debug with bazel.

For a simple release build:

```
$ bazel build //main:lgshell
```

## LGraph Structure

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
```cpp
auto node = lg->create_node(Node_Type_Op);

auto dpin = node.setup_driver_pin(1);

dpin.set_bits(8);

auto spin = node2.setup_sink_pin(3);

dpin.connect(spin);
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


## InOu

InOus are inputs and/or outputs to/from LiveHD. An input will create a LGraph,
e.g., from a verilog description, an json representation, or randomly.
Similarly, an output will read an existing LGraph and generate an alternative
representation, eg., verilog or json.

Examples of inou can be found in inou/yosys (for verilog handling) and inou/json.

## Passes

Passes are transformations over an existing LGraph. In the future, there may be
passes over LNAST, but for the moment, we just have LGraph passes. A pass will
read an LGraph and make changes to it. Usually this is done for optimizations.
Examples of passes can be found in `pass/sample`, which compute the histogram
and count wire numbers of a LGraph.


## LNAST vs LGraph

LGraph (Live hardware Graph) is the graph-like data structure and associated
API inside LiveHD. LNAST (Language Neutral AST) is the tree-like structure and
associated API to easily create new input languages to LiveHD.

LNAST has a separated [documentation](https://masc.soe.ucsc.edu/lnast-doc).

# Coding Style and Organization

## Style

For coding, please follow the coding styles from [Style.md](docs/Style.md). To contribute,
check [policy](docs/CONTRIBUTING.md) document that explains how to create pull requests
and more details about license and copyrights. Also, contributors to LiveHD are
expected to adhere to the [Code of Conduct](docs/CODE_OF_CONDUCT.md).

## Code Organization

The code is organized as:

- [`core/`](./core) - All the core classes of LGraph (nodes, edges, iterators, field tables, ...)
- [`meta/`](./meta) - All the additional fields added to the nodes
- [`inou/`](./inou) - All the inputs and outputs to and from LGraph
- [`pass/`](./pass) - Transformations over LGraph
- [`cops/`](./cops) - Combine operations, ie. take N graphs and creates another graph
- [`misc/`](./misc) - External libraries and other misc code
- [`test/`](./test) - Testing code, scripts, cases and infrastructure (Note: unit tests
  should be placed inside the corresponding subfolder)
- [`docs/`](./docs) - Documentation of LiveHD

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

Pull at least once a day when working, LiveHD is in active development.

Always target warning free compilation. It is okay to commit code that triggers
warning during development, but remember to clean up afterwards.

If you are not one of the code owners, you need to create a pull request as
indicated in [CONTRIBUTING.md](docs/CONTRIBUTING.md) and [GitHub-use.md](docs/GitHub-use.md).




# Publications
For more detailed information and paper reference, please refer to 
the following publications. If you are doing research or projects corresponding
to LiveHD, please send us a notification, we are glad to add your paper.

#### Live techniques 
1. [LiveSim: A Fast Hot Reload Simulator for HDLs](docs/papers/LiveSim_ISPASS20.pdf), Haven Skinner, Rafael T. Possignolo, Sheng-Hong Wang, and Jose Renau, International Symposium on Performance Analysis of Systems and Software (ISPASS), April 2020. **(Best Paper Nomination)**

1. [SMatch: Structural Matching for Fast Resynthesis in FPGAs](docs/papers/SMatch_DAC19.pdf), Rafael T.
   Possignolo and Jose Renau, Design Automation Conference (DAC), June 2019.

2. [LiveSynth: Towards an Interactive Synthesis Flow](docs/papers/LiveSynth_DAC17.pdf), Rafael T. Possignolo, and
   Jose Renau, Design Automation Conference (DAC), June 2017.

#### LGraph 
3. [LGraph: A Unified Data Model and API for Productive Open-Source Hardware Design](docs/papers/LGraph_WOSET19.pdf), 
   Sheng-Hong Wang, Rafael T. Possignolo, Qian Chen, Rohan Ganpati, and
   Jose Renau, Second Workshop on Open-Source EDA Technology (WOSET), November 2019.

4. [LGraph: A multi-language open-source database for VLSI](docs/papers/LGraph_WOSET18.pdf), Rafael T. Possignolo,
   Sheng-Hong Wang, Haven Skinner, and Jose Renau. First Workshop on Open-Source
   EDA Technology (WOSET), November 2018.

#### LNAST 
5. [LNAST: A Language Neutral Intermediate Representation for Hardware Description Languages](docs/papers/LNAST_WOSET19.pdf), Sheng-Hong Wang, Akash Sridhar, and Jose Renau,
   Second Workshop on Open-Source EDA Technology (WOSET), 2019.



