

## Tree-sitter Pyrope

Using https://github.com/tekinengin/tree-sitter-pyrope as a starting point, complete the Pyrope grammar
to correctly parse the Pyrope grammar (https://masc.soe.ucsc.edu/pyrope.html), interface with LiveHD
and third party tools.

Main features:

* Pyrope tree-sitter grammar
* tree-sitter to LNAST generation (Comparable to https://github.com/masc-ucsc/livehd/tree/master/inou/pyrope)
* Atom and neovim integration
* Atom go definition, highlight, and attribute
* Atom capacity to query LNAST/Lgraph generated grammar for bit-width. The incremental grammar passed to LNAST, passed to Lgraph,
  and incremental bit-width inference.
* neovim highlight, indent, fold support
* Integrate with atom-hide as extra language

In addition to the packages, there should be an iterator that use the incremental builder to support incremental changes.

## Bring Back Incremental Synthesis to Lgraph

WARNING: Traversal depdendence

Dependence: none

Main features:

* Reimplement the DAC synthesis in Lgraph
* Re-run anubis to get data

## Mockturtle

There are some issues with Mockturtle integration (new cells) and it is not using the latest Mockturtle library versions.
The goal is to use Mockturtle (https://github.com/lsils/mockturtle) with LiveHD. The main characteristics:

* Use mockturtle to tmap to LUTs
* Use mockturtle to synthesize (optimize) logic
* Enable cut-rewrite as an option
* Enable hierarchy cross optimization (hier:true option)
* Use the graph labeling to find cluster to optimize
* Re-timing
* Map to LUTs only gates and non-wide arithmetic. E.g: 32bit add is not mapped to LUTS, but a 2-bit add is mapped.
* List of resources to not map:
    * Large ALUs. Large ALUs should have an OpenWare block (hardcoded in FPGAs and advanced adder options in ASIC)
    * Multipliers and dividers
    * Barrell shifters with not trivial shifts (1-2 bits) selectable at run-time
    * memories, luts

## Query shell (not lgshell) to query graphs

* Based on replxx (like lgshell)
* Query bits, ports...  like
    * https://github.com/rubund/netlist-analyzer
    * https://www.jameswhanlon.com/querying-logical-paths-in-a-verilog-design.html
* It would be cool if subsections (selected) parts can be visualized with something like https://github.com/nturley/netlistsvg
* The shell may be expanded to support simulation in the future
* Wavedrom/Duh dumps

Wavedrom and duh allows to dump bitfield information for structures. It would be interesting to explore to dump tables and bit
fields for Lgraph IOs, and structs/fields inside the module. It may be a way to integrate with the documentation generation.

Example of queries: show path, show driver/sink of, do topo traversal,....

As an interesting extension would be to have some simple embedded language (TCL or ChaiScript or ???) to control queries more
easily and allow to build functions/libraries.

## Lgraph and LNAST check pass

Create a pass that checks that the Lgraph (and/or LNAST) is sementically correct. Some checks:

* No combinational loops
* No mismatch in bit widths
* No disconnected nodes
* Check for inefficient splits (do not split busses that can be combined)
* Transformations stages should not drop names if same net is preserved

## Term record/replay for benchmarking/testing

Use automatic asciinema generation. Compare the test speed and summarize the
performance difference from "a user" point of view. The results should allow to
track performance changes.

Maybe expand tmt_test and main_test to be a more stand-alone testing setup.

## OS X Support

LiveHD compiles (it did) with OS X, but there are some issues with the mmap infrastructure inside mmap_lib. The code functionality
should be able to run (the mmap_remap does not exist in OS X, but a more costly alternative is implemented for OS X, just not tested
and it seems faulty).

## Random CHISEL/Verilog/Pyrope generator

Create a python/ruby/C++ program that generates pseudo-random programs in
several languages (CHISEL/Verilog/Pyrope). The idea is that the same program
can be implemented in multiple ways but all should have the same result
(simulation and LEC).

## Iterators

We have fast/forward with and without hierarchy. Several improvements can
be done to make it more useful. The equivalent (inefficient) API:

```
for(auto node:lg->forward()) {
  if (!some_set.contains(node.get_compact()))
    continue;

  // do work here
}
```

* backward iterator
* Start iterators (forward/backward from a given position
* Allow to run a subset of the graph (method based). A method passed to the iterator returns true/false indicating if the node is part of the requested traversal.

Blocks like mockturtle will significantly benefit from such iterator

## unbitwidth Local and Global bitwidth

This pass is needed to create less verbose CHISEL and Pyrope code generation.

The LGraph can have bitwidth information for each dpin. This is needed for
Verilog code generation, but not needed for Pyrope or CHISEL.  CHISEL can
perform local bitwidth inference and Pyrope can perform global bitwidth
inference.

A new pass should remove redundant bitwidth information. The information is
redundant because the pass/bitwidth can regenerate it if there is enough
details. The goal is to create a pass/unbitwidth that removes either local or
global bitwidth. The information left should be enough for the bitwidth pass to
regenerate it.

* Local bitwidth: It is possible to leave the bitwidth information in many
places and it will have the same results, but for CHISEL the inputs should be
sized. The storage (memories/flops) should have bitwidth when can not be
inferred from the inputs.

* Global bitwidth: Pyrope bitwidth inference goes across the call hierarchy.
This means that a module could have no bitwidth information at all. We start
from the leave nodes. If all the bits can be inferred given the inputs, the
module should have no bitwidth. In that case the bitwidth can be inferred from
outside.

## LiveHD gRPC client

Leverage the gRPC client in liveHD to allow the submission of work to remote servers.

Once we can submit gRPC from inside LiveHD, we should have to re-structure the
pass API to have a gRPC call for each of the main steps.

