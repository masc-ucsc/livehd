
# Projects

This document has a list of potential projects for Live Hardware Development.
The document has a high level description and dependence chain for several
potential master thesis/project ideas. Contact Jose Renau if you want to work
on any of them to provide more details.

If you are an UCSC student, contact Professor Jose Renau
(https://users.soe.ucsc.edu/~renau/) to coordinate, the idea is that you can do
a MS thesis/project or an undergraduate senior design project. It should take 2
full quarters. If there is enough progress and funds are available, it may be
possible to provide 1 quarter GSR in the last quarter for master students.

At the end of the document there is a list of finished projects as example, and
small tasks that are not enough for a thesis/project, but great to help.


# Open Projects

Open projects are potential MS thesis/projects.

## Package Manager

Create a cargo like package to interact with LiveHD shell.

Sample commands:

```
lhd new prp:my_new_package  # equiv to cargo new
lhd test                # equiv to cargo test
lhd build               # equiv to cargo build
lhd run                 # equiv to cargo build
lhd run --bin top       # similar to cargo, but pick top             
lhd bench --bench xxx   # similar to cargo
...
```

A difference from cargo is that cargo reads just rust. lhd reads verilog,
pyrope, CHISEL. This means that some passes like "lhd new" must specify the
languages to generate.

CHISEL may need a build.bst to generate the chirrtl pb needed by livehd.

The lhd.toml is somewhat similar to cargo. The package fields could be the
same, the version and dependance (relative or github). For the moment, there is
no "lhd package server" but the idea is to build one.

Ideally, there are packages for tools like chipyard boom/rocket. Then, adding a
dependence we bring the code.

The dependences are like in cargo, inside the target directory. The source is
copied there and compiled.  Since LiveHD allows hierarchies, we can use two
subpackages with different versions. E.g: we can have a rocket version 1.1 in
top and a package (controller) can depend on rocket 1.0 (this should be fine).

* Allow to have pyrope/verilog/chisel libraries (adder, multipler, corex....) as packages
* Allow to specify a specific library/packages with SemVer
* It would be neat if it can generate bazel scripts to run tests, compile 
* It should allow to specify versions on software and toolchains

There lhd.toml specifies sources, tests, dependences....

There is a default livehd translation, but it may be interesting to have a
concept of "toolchain" so that different packages could go through different
tool chains. In the extreme, I can see an adder requiring livehd v1.0 and
another module needing livehd 1.3 to compile.

Some feedback is to look at things like:

https://doc.rust-lang.org/cargo/index.html
https://github.com/olofk/fusesoc
https://github.com/azukaar/GuPM
https://github.com/pnpm/pnpm

Some simpler/different goals but interesting to see:
https://github.com/buildinspace/peru
https://github.com/poacpm/poac
https://medium.com/@sdboyer/so-you-want-to-write-a-package-manager-4ae9c17d9527
https://github.com/olofk/edalize

Things that lhd is not:

* It is not a build/make/bazel alternative. It tracks repos, but it has a
  "toolchain" to decide/select how to run the tools. E.g: lhd run may create a
makefile and call "make run", but this is hidden.

* It does not have tons of options. The options are in toolchains that can be
  configured (not in the lhd.toml that should be quite minimal). The default is
that everything should run in the LiveHD toolchain, but it can be extended to
provide new toolchains.

## mmap_lib projects

* mmap_vset finish
    * iterator: find: returns an iterator  begin/end to allow an iterator
    * Use contains(key): to make it easy to port back/forth with abseil
    * Add tests with Node_pin::Compact and Node::Compact
* mmap_str
    * Implement class
    * replace sting_view/string for mmap_lib::str
    * Replace the string_view storage from mmap_map to use the new mmap_lib::str
* mmap_vector delete
* Each structure mmap_vector, mmap_map, mmap_bimap, ... mmap_str should have a test and bench

## ACT (Async) output

Once pyrope with Fluid is in the flow, we could use Pyrope to program async designs too.
It may be interesting to output ACT (https://avlsi.csl.yale.edu/act/doku.php) to interface
with AVLSI flow.

## New Memoize Map

Runtime (no persistence) memoization to reduce costly ops. Fixed size at
compile time, with statistics about hit/miss utilization during DEBUG runs.
Capacity to enable/disable at compile time if stats change.


E.g: avoid calling costly Lgraph::open if frequently used. Or remember last
insertion point in map/vector if just used....


It should be very fast, and able to notify when not useful so that it can be
enable/disabled per use if needed.


## Verilog input with a slang 2 LNAST pass

slang (https://github.com/MikePopoloski/slang) is an open source System Verilog parser/compiler.
It is fairly fast and complete. The goal of this project is to interface slang to LNAST. Notice
that this is just from slang to LNAST, not from LNAST to slang.

Dependence: none

Main features:

* Focus on synthesizable verilog. It has to support packages, structs, and interfaces but not need
to handle classes.
* Key benchmarks to handle are (in order):
    * CHISEL generated verilog. Mostly firesim (https://fires.im/) generated for rocket and BOOM
    * Titan23 (http://www.eecg.utoronto.ca/~kmurray/titan.html).
    * Anubis (https://github.com/masc-ucsc/anubis)
* Regression set
    * slang->lnast->lgraph->lnast->code_gen (C++ and verilog+lgcheck)
        * https://github.com/zachjs/sv2v/tree/master/test
    * slang->lnast->lgraph->lnast->code_gen (verilog+lgcheck)
        * BOOM
        * https://github.com/SymbiFlow/sv-tests
        * https://github.com/taichi-ishitani/tnoc
* The interface with slang could be through the C++ ASTVisitor in slang or maybe just handling the dumped json

## Lgraph to/from XLS IR

Create a bridge between Lgraph and XLS IR. Not LNAST because the XLS IR is very close to Lgraph.

Dependence: none

Main features:

* Bring XLS build to combine in a single bazel
* Bridge to allow runs of DSLX to LiveHD
* Bridge to allow from XLS IR to LiveHD
* Bridge to allow from Lgraph to XLS IR

## Lgraph to Yosys JSON

The json format from yosys is used by several tools like nextpnr and netlistsvg. Creating the json interface
could simplify the interface with yosys too.

Dependence: none

Main features:

* Create a yosysjson pass that generates yosys compatible json files out of Lgraph
* It may be also interesting to accept yosys json as input. Then, the bridge between yosys and LiveHD could be removed.

## Hot-Reload Simulation Console

Bring to the LNAST C++ code generation the capacity to perform Hot-Reload. Some of these concepts
were implemented in Heaven Skinner PhD thesis hot reload chapter.

Dependence: This project can not be started until the C++ from LNAST is not finished

Main features:

* Client/server
    * Client is the console
    * Server is running the simulation. Create a unique URL id for each run
    * A console can attach to different servers if URL is known (server.connect(url) command)
* Checkpointing
    * Run simulation and capacity to checkpoint contents
    * create simulation checkpoints (push/pop command)
* Waveform
    * Capacity to dump test waveforms on screen (wave.save command)
    * Live binary format with mmaps that allow the simulation to run and the console to monitor the values
    * Capacity to generate LXT2 (gtkwave). (wave.lxt2 command)
* Console
    * Use replxx for the console (same as lgshell)
    * A gdb-like deamon (remote zero-MQ?) and console to debug hardware
    * The console is a chaiscript language with extensions for the auto-complete
    * autocomplete in variables/wires
    * Show paths. From X to Y. Some similarity: https://www.jameswhanlon.com/querying-logical-paths-in-a-verilog-design.html
    * Allow to peek/poke variables
    * Allow to hot-reload pyrope generated assertions to stop simulation
    * Allow to create markers for passing failing code to other people
    * Aloow to have command script file (load checkpoint, run X, mark Y, insert assert Y, continue x, peek X, poke Y, save waveform)

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
* create a prp-fmt that outputs formatted pyrope from the tree-sitter AST

In addition to the packages, there should be an iterator that use the incremental builder to support incremental changes.

## Parallel forward/backward traversal

Lgraph has fwd/bwd/fast iterators. Those are efficient but single threaded. The idea is to create iterators
that accept a lambda function. The idea is to "chunk" the graph in subgraphs and give a subchunk of the graph
to each thread.

Dependence: none

Main features:

* The workload distribution is with livehd/task Thread_pool
* Possible to speedup the topological sort: https://link.springer.com/chapter/10.1007/978-981-10-5828-8_39
* Fast is more straightforward (no hierarchical, just chunk the graph). 
* The parallel should work with and without hierarchy, but it is more important
  WITH hierarchy because those are the large graph traversals.

To understand the forward, before a task starts with a sub-graph, all the inputs must be handled first.
Similar to the backward but out all the subgraph outputs.

A main challenge is that most data structures new a lock for read/writes. This
would KILL performance. There could be several options, but the best would be
to have a structure per thread. E.g: to compute bitwidth in parallel, each
thread can have its own bitwidth data and populate the bits as an aggregation
process once all the tasks have finished.

This adds a significant complexity bar, so it should be handled per task. The
most logical is bitwidth and cprop. Both can do the data structure per thread,
and traverse to aggregate.


The goal is to aim at 16 cores and achieve 10x speedup for those larger
bitwidth/cprop tasks.

## Lgraph partition/decomposition

Implement several partitioning algorithms in Lgraph. Each partition has a
different attribute. The attributes/partitions could be used for
synthesis/placement/...

Dependence: none

Main features:

* Break graph partitions in disjoin sets and areas that do not have cross optimization (disjoin). Similar to MockTurtle partition method (separate by flops, large adders/multipliers/dividers).
* Mincut partitioning. Use with https://github.com/SebastianSchlag/kahypar for min-cut
* Port ESSENT acycling partitioning to C++. This reads a LGraph (or hierarchical lgraphs) and partitions the graph to several acycling partitions.
* Live/Incremental partition. Given 2 graphs, find matching "partitions" across the graphs that finish in equivalent points (DAC paper has more details)

Optional partitionings:
* "Bottom-Up Disjoint-Support Decomposition Based on Cofactor and Boolean Difference Analysis" https://ieeexplore.ieee.org/abstract/document/7357181/
* "Bi-decomposition of large Boolean functions using blocking edge graphs" https://dl.acm.org/citation.cfm?id=2133553

Optional traversals:
* It would be good to have extensions on the fast/forward/backward iterators that accept a "partition" to traverse instead of the whole graph

## Parallel and Hierarchical Synthesis with Mockturtle

Mockturtle is integrated with LiveHD. The goal of this task is to iron out bugs
and issues and to use the LiveHD Tasks API to parallelize the synthesis.

Dependence: none

Main features:

* The current synthesis divides the circuit in partitions. Each partition can be synthesized in parallel.
* Support hierarchical synthesis to optimize cross Lgraphs (cross verilog module optimization)

## Bring Back Incremental Synthesis to Lgraph

WARNING: Traversal depdendence

Dependence: none

Main features:

* Reimplement the DAC synthesis in Lgraph
* Re-run anubis to get data

## OpenWare

We need a set of set of implementations with different trade-offs for each basic Lgraph gate. The implementations
could optimize for FPGA and ASIC.

Dependence: none

Main features:

* Custom implementations for all the Lgraph cells: reductions, add/sub, mult, shift, barrel shifter...
* The OpenWare library is built in Pyrope, with efficient verilog generated
* Benchmark the OpenWare against FPGA and ASIC default (designWare) targets
* Specific target implementations for FPGA (Xilinx) and ASIC (generic)
* Several speeds/trade-offs for each major block. E.g: adder RCA/Kogge/...

## Synthesis (ASIC/FPGA) Competitive Analysis

Competitive analysis finding main weakpoints for the synthesis flow. Both FPGA and ASIC targets.

Dependence: Mockturtle + OpenWare projects

Main features:

* Goal to run several large tests (Anubis) and BOOM and Titan23 (http://www.eecg.utoronto.ca/~kmurray/titan.html).
* Compare against vivado, quartus, DC, ABC, Mockturtle.... results can not be published directly but OK as A/B/C vs Lgraph+XX.
* Important to focus on OpenWare Lgraph flow vs others
* Create regression system that plots freq/area/power for several key benchmarks

## Useful Lgraph Passes

Lgraph has several passes. Many compiler like passes that can be used by
several projects. This is a set of those unrelated passes.

Dependence: none

Main features:

* Find combinational loops and notified LoC responsible
* Find cross clock domain and check that it has acceptable patters to cross. Allow to be extensible
* Copy propagation and Dead-code elimination in LGRAPH with and without hierarchy
* Check Lgraph consistency. E.g: NOT should have same bits input/output edges

## Dynamic Power Model

Performance is important, but dynamic power consumption it is too. The goal is to generate power consumption results.

Dependence: Hot Reload

Main features:

* Estimate power statically and dynamically
    * Statically: Use activity rates (could be imported from previous run)
    * Dynamically: generate power consumption trace
    * Dynamic power should be a switch from simulation. Not a VCD/LXT2 dump process. This will allow feedback on simulation.
    * Clock gating efficiency/static can be off-line
* Estimate clock gating efficiency, recommend clock gating points after dynamic run
* Read liberty for power and have some approximate model for interconnect. The idea is good average out, not exact per cell.

## Liberty

Liberty is de-factor industry standards. The goal is to parse them and populate the graph library accordingly.

Dependence: none

Main features:

* Liberty. We have some "read" pass in lgraph
* Create an object to hide the interface with a C++17 iterator
* Capacity to merge many files in the same object (inou_liberty)
* The read formats should be backed with mmap_lib (zero load time). Single per lgdb directory
* Multithread ready (infrequent update, frequent read)
* Unit tests for correctness and performance
* Wrapper to get timing for cells considering load

## FPGA Legalization

Legalize FPGA cell/LUT placement.

Dependence: FPGA Placer and RapidWright

Main features:

* Based on "A Fast, Robust Network Flow-based Standard-Cell Legalization Method for Minimizing Maximum Movement" but applied to FPGA
* Handle incremental. Only marked blocks need to be legalized. Already legalized blocks should not move (faster incremental that avoids re-routing blocks)
* Handle macros (SRAMs, DSPs, ....) that can have floating or fix location
* Handle packing and clock domains

## Floorplanner

Implement a floorplanner for large Lgraph designs (firesim target).

Dependence: partitioning

Main features:

* Planar graph. Cluster pipeline stages until the graph is planar.
* Placed blocks do not need boundaries, just centers (analytical placer will handle the shape.
* Implement a more traditional floorplanner leveraging min-cut.
* Do hierarchy. Hierarchy preserves symmetry. Bassed on "A hierarchical approach for generating regular ﬂoorplans"
* Different partitions are marked with different partitions.

Some related papers in clustering:
* FADE: Graph dra wing, clustering and visual abstraction
* A Semi-Persistent Clustering Technique for VLSI Circuit Placement
* Net Cluster: A Net-Reduction-Based Clustering Preprocessing Algorithm for Partitioning and Placement
* LSC: A large-scale consensus-based clustering algorithm for high-performance FPGAs
* Enumeration technique in very large-scale integration fixed-outline floorplanning
* A hierarchical approach for generating regular ﬂoorplans
* Network Flow Based Datapath Bit Slicing
* For planar/clustering ideas papers:
    * Visualizing fuzzy overlapping communities in networks
    * modularity and community structure in networks
    * The State of the Art in Visualizing Group Structures in Graphs


## Performance Monitoring Infrastructure

We perform regression for correctness, we should have regressions for
performance. This includes completion times and tracking CPU performance
counters. The setup uses prometheus/grafana
(https://prometheus.io/docs/visualization/grafana/)

Dependence: none

Main features:

* Create scbench to gather stats and performance counters
* stat.begin, stat.end, stat.pause
* Aggregate stats in a single json file even across many runs
* Gather performance counters (when available) like branch miss, L1/LLC cache miss, DTLB...
* Integrate with some plotting system for performance CD/CI
* Have some way to easily mark/detect outliners after a commit. Pointing top perf increase/decrease as a result from a commit.
* Create a prometheous/grafana setup that feeds from runs automatically.
* Promethous/grafana server notifies on new outliers on regression commit
* scbench should gather performance counters (PAPI?)
* Integrate with MALT to track memory (mmap and no mmap) usage
* Statistics on system calls to detect potential issues

Some reference codes:

https://github.com/yse/easy_profiler
https://github.com/nickbruun/hayai
https://github.com/david-grs/geiger
https://github.com/NERSC/timemory
https://epfl-vlsc.github.io/memoro/
https://memtt.github.io/malt/
https://github.com/jonasmr/microprofile

Some good collection of links https://github.com/MattPD/cpplinks/blob/master/performance.tools.md

## OpenSTA

 Evolve the opentimer to be more generic and have openTimer and openSTA as potential backends.

Dependence: none

Main features:

* Get timing from openSTA and get openTimer to work

## ECO pass with Lgraph

Leverage the incremental and push it further to make Lgraph the ECO flow for open source

Dependence: bring back incremental synthesis, partitioning, and SAT solver

Main features:

* Incremental synthesis optimizes for speed, ECO optimizes for small size partition.

## Amazon F1

Clean productive flow for Amazon F1

Dependence: Rapidwright, incremental synthesis

Main features:

* Do a full flow of Lgraph for F1. Fast incremental

## OpenDB

Integrate OpenDB flows with Lgraph

Dependence: none

Main features:
* The new OpenDB from OpenRoad is a good backend (ASIC) bridge that we may want to build.
* Through OpenDB we can leverage full ASIC synthesis and tools like OpenSTA

## SAT/SMT type checking for Pyrope

Use a SMT solver to constraint all the types. Use the SMT to find valid bits for each variable and check that it is "satisfiable"

Dependence: SAT Solver

Main features:

* when unsat, maybe use a MAXSAT to find the maxium SAT and report the other as the source of SAT problem.
* Bitwidth inference using SMT (min/max bits)
* Assertions "I"/"N", are placed to help constraints
* Provide a failing example
* Maybe a MAXSAT example? to report an error. For error reporting, the inputs/outputs per module and the LoC that violates the constraint may be an better way to debug.

## Pyrope/LNAST AI Warning

Create a AI based warning/error system for Pyrope and LNAST.

Dependence: Pyrope to LNAST

Main features:

* Break the code in "chunks" in parsed code (verilog, c-functions, pyrope).
* One error per chunk
* Build AST per chunk with incremental parsing (tree-sitter like)
* Create DB with error messages and "signatures"
* AI signatures are:
    * AST (pre and post-fix AST)
    * Find AST "diff" with other parts of code base. Feed diffs as signatures
    * Have DB with fuzz regex to point potentially similar variables
* Have a fuzz regex to find very similar messages to collapse at entry ("missing string" or "string not found")

## UHDM 2 LNAST

UHDM is able to interface with several tools like Verilator/yosys. It may be interesting to create a bridge
between LNAST and UHDM.

Dependence: none

Main features:

* UHDM2LNAST
* LNAST2UHDM
* Support structs/bundles and most UHDM features

## FPGA Placer

Build an analytical placer for FPGAs based on Ripple-FPGA.

Dependence: none

Main features:
* Use https://developers.google.com/optimization/lp/glop as solver
* Use with https://github.com/SebastianSchlag/kahypar for min-cut
* Being able to transfer placed design to Rapidwright
* Target Alveo U250

Main steps:

* Dump animations on placement with https://github.com/dtschump/CImg Just dump png every n-iterations (or seconds)
* Replace lpsolve with google glop (Bazel build) https://developers.google.com/optimization/lp/glop
* Replace patoh with kahypar (Bazel build) https://github.com/SebastianSchlag/kahypar
* Get it to build with Bazel as a separate repo (not in lgraph)
* Benchmark a potential replacement of Eigen library with SuperSCS (This is over 70% of the time for large bench. Try different libraries pick faster SuperSCS?)
    * gp_qsolve.cpp (location for Eigen)
    * SuperSCS code example in C: https://kul-forbes.github.io/scs/examples_in_c.html
    * In-place solver leveraging Lgraph is also very reasonable as alternative

Some characteristics/thoughts to consider:

* Get inspirantion from "BonnPlace: A Self-Stabilizing Placement Framework" and RippleFPGA
* If floorplaner is there, leverage floorplan, but do not assume shape (square) in contour. Allow to place around each floorplan area (amorphous. A center of gravity per floorplan block)
* Use timing in feedback
* Cluster flops (buses)
* To allow fast incremental placement. Allow to do analytical over subset of design, and other areas are fixed (legalization to handle)
* Critical paths are aligned
* Use a rough global router to "spread" when needed.
* Use constraint solver for packing
* Support "annotations"
    * Relative place (left/right/top/bottom)
    * Alignment (true/false)
    * Close by (add anchor)
* benchmark against:
    * https://github.com/YosysHQ/nextpnr-bench

## NextPNR

Integrate with the nextPNR FPGA placement/routing

Dependence: Rapidwright

Main features:
* Create a bridge to/from Lgraph and nextpntr (json)

## Cloud

Main requirements:

* Able to scale up/down automatically, and down to zero if not used
* Fast latencies
* Able to use google/AWS and local servers

Some potential implementation:

Need to have a way to generate a "patch/diff" from the lgdb. The idea is that
we send commands to the could with a lgdb, then we do a merge. If there are
conflicts (merge), we need to retrigger.  Similar to the "development" process
but with livehd (lgdb directory) runs. For the diff/patch command, I would start with
https://github.com/sisong/HDiffPatch

The hdiffpatch should be fine for everything but graph_library. This one may require a special program
to merge. E.g:

```
hdiffz -g#lgdb/graph_library.json  lgdb lgdb2 patch
hpatchz  lgdb patch lgdb3
```

After each command, the "delta" is sent back and applied to the front

* REST API for all the servers.
* Use httplib. See lgraph/main/userver_test.cpp and lgraph/main/uclient_test.cpp
* lgshell commands
    * cloud.server
    * cloud.ping     ; ping a server or front-end
    * cloud.frontend ; setup a front-end server to distributue the work to existing servers
    * cloud.link     ; setup lgshell as client. Next commands may go to server (in priority order)
* Server and client must have same git clone token (version) check for consistency.
* When in server mode, transfer client files, monitor server files, and transfer back. The trasfer uses the patch/diff. If patch files, it retriggers again.
* Add bazel the option of creating a alpine docker image directly to deploy lgraph in the cloud as an rest API service
* Create bringup and shutdown inside lgshell. Allow for local machines and gcloud/aws
    * cloud.start priority:1 local:true
    * cloud.start priority:2 ssh:mada1
    * cloud.start priority:33 cloud:xxx-parameters-for-gcloud
    * cloud.shutdown # kills any cloud.start that was spawned before
    * cloud.list   # list servers available at this lgdb setup (previous clould.start...)

# Active Projects (already selected)

## SAT Solver

Main requirements:

* Optimize equiv check. Traverse and remove subsets with same functionality
* Allow to SAT equiv graphs marked
* Reduce ports in rams, and collapse muxes when parallel/full case
* Partition graph in sub-graphs. Each subgraph can synthesis/check by itself.

Decide ezSAT or boolector or CVC or (ezSAT + XX)

* https://boolector.github.io/docs/cboolector.html
* Create a small benchmark and decide (some simple circuit)

extend ezSAT and interface with lgraph to:

* check equivalence of lgraphs
* Reduce port in SRAMs
* Check parallel cases in muxes

Potential paper:

* Use the net names from incremental synthesis to partition design (if partition verifies, we are done)
* Use structural traversal to remove search space (from inputs/outputs)
* Use parallel SAT checks (each output cone, partition of cones, after synth)
* Mark "boundaries" for equivalence in lgraph. Faster synthesis, verification...
* Different SAT solvers in parallel (ezsat, CVC4, SPT) and pick fastest

Maybe use https://github.com/Boolector/boolector

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

# Open Medium Size Tasks (not MS project/thesis)

This is a list of small tasks. Each should take 1-3 weeks to implement. These
are not thesis/projects but good ideas to get to know the setup and help, and they
can evolve for undergraduate senior design.

## mmap_lib::vector erase (pop_back)

The current only way to shrink a mmap_lib::vector is with a resize command. It
should be good to allow a pop_back (erase last element). Notice that the mmap
call is expensive, we just need to really reduce size when there is a
significant fraction to be saved.

## Setup a vagrant image

We have several dockers for testing, a simple vagrant (ubuntu based?) for most users may
be nice to have. Maybe based on https://github.com/VLSIDA/openram-vagrant-image

## lgshell ctrl+C

* Intercept the CTRL+C and finish the current command. Do not kill/terminate the lgshell
* This may require to spawn the commands as threads and kill them for ctrl+c

## lgshell perf summary

* Record the time (and perf stats) for each lgshell command executed.
* Print the statistics when closing the lgshell

## Query shell (not lgshell) to query graphs

* Based on replxx (like lgshell)
* Query bits, ports...  like
    * https://github.com/rubund/netlist-analyzer
    * https://www.jameswhanlon.com/querying-logical-paths-in-a-verilog-design.html
* It would be cool if subsections (selected) parts can be visualized with something like https://github.com/nturley/netlistsvg
* The shell may be expanded to support simulation in the future

Example of queries: show path, show driver/sink of, do topo traversal,....

As an interesting extension would be to have some simple embedded language (TCL or ChaiScript or ???) to control queries more
easily and allow to build functions/libraries.

## Use SLATE to document key API

Go over public methods in node, node pin, graph library, sub node, and create a
SLATE entry.

Point to some of the unit tests using the API, and create. Feel free to create
trivial examples that run with unit tests as sample of usage.

## Lgraph and LNAST check pass

Create a pass that checks that the Lgraph (and/or LNAST) is sementically correct. Some checks:

* No combinational loops
* No mismatch in bit widths
* No disconnected nodes
* Check for inefficient splits (do not split busses that can be combined)
* Transformations stages should not drop names if same net is preserved

## Short demos

Using ascinema, create some small demos to show how to use LiveHD. 

It would be good to have demos on how to "start to create a pass".

https://asciinema.org/

## Term record/replay for benchmarking/testing

Use automatic asciinema generation. Compare the test speed and summarize the
performance difference from "a user" point of view. The results should allow to
track performance changes.

Maybe expand tmt_test and main_test to be a more stand-alone testing setup.

## OS X Support

LiveHD compiles (it did) with OS X, but there are some issues with the mmap infrastructure inside mmap_lib. The code functionality
should be able to run (the mmap_remap does not exist in OS X, but a more costly alternative is implemented for OS X, just not tested
and it seems faulty).

## Smaller tasks

For even smaller tasks check the [cleanup.md](cleanup.md) file


# Deprecated projects

This is a list of projects that were proposed, but that they are dropped. It includes a justification of why they got dropped.
This section of the document is to learn about the selection/deselection process.

## lhdview

DEPRECATION REASON: The lhdview was a "custom" application proposal. There is an external effort to bring wavedrom zoom and atom
integration. The idea is to have the same functionality as the proposed lhdview but to complement/help/leverage the atom flow.
The flow is fairly similar to liveOS, there is a web server collecting statistics and vcd traces. The client has the capacity to
zoom/view signals (web client). An atom client can coordinate with the viewer and become a vivado like. A console could be open to
interact with the remote server.


Live Hardware Development Viewer. The long term goal is to create an
verdi/simvision alternative with a Live focus. This is a large project that
should be split for 2 thesis (waveform and rest). The plan is to use nana++
(http://nanapro.org/en-us/) or QT. Prof Renau has a skeleton for the
application.

Some example screenshot:
 https://www.cadence.com/content/dam/cadence-www/global/en_US/images/old-tools/system-design-verification/debug_fig_sim_vision_windows.jpg

Dependence: This project can needs the binary format with mmap from Hot Reload

Main features:

 * Annotated source window: A window that shows text (verilog/pyrope) and the values for variables. Simvision calls this the "source browser window".
 * Waveform window. A gtkwave like capacity to show waveforms.
 * Signals search window
 * Reads mmap_lib structures so that it automatically gets updated as the simulation runs (live updates)

 ### Source Window

 * Shows current and previou[s] values for each variable
 * double click on the variable gets you to the driver.
 * Clicking on variable shows on the "search window" the driver (top) and consumers (bottom) from the variable
 * It uses source map (https://github.com/mgreter/sourcemap.cpp) to show the "original" code, not the "generated" verilog or "C".
 * Variables can be draged to/from the waveform window. (shift+click adds to waveform view, shift+double click adds driver to waveform)
 * There can be several source windows
 * Style: avoid dialogs, minimalist, multiple panels, tune for dark background, vi-like, fuzzy search, focus on read code (not edit, but allow edit)
 * Some edits to potentially leverage code/ideas:
     * Very fast load times: https://github.com/arximboldi/ewig
     * Clean value search (debug): https://gitlab.com/cppit/jucipp
     * neovim component (implement vim commands): https://github.com/rhysd/neovim-component
 * Option: juice integrated with neovim, variables underneath, add verilog/pyrope style, read value dump (mmap-VCD), go back/forth time...
 * Option: integrate/leverage scintilla (must integrate with GUI, and other waveform viewer) and neovim

### Waveform Window

 * GTKwave like with capacity to drag to/from source window
 * Query to Lgraph to have "order" in variables computed. This is shown in the wave form like small delays (up to 40% of the cycle for delays? maybe configurable)
 * Capacity to add edges (like in wavedrom, https://observablehq.com/@drom/wavedrom) leveraging producer consumer info from lgraph
 * load/save configured signal configuration through the console window
 * Struct-like support for signals (groups)
 * markers

### Search Window

 * Fuzzy regex search for signals (fzf like https://github.com/hansonw/fuzzy-native/blob/master/src/score_match.cpp) but tuned for verilog/pyrope search
 * capacity to show hierarchy (alphabetically sorted) and restrict search per level (or sub-levels)
 * Only one search window

### Console window

 * hot reload console. Mostly allow drag variables from/to windows.
 * source/waveform/search windows can be controlled from console
 * allow to load/save list of commands


# Summer Intern Projects (2-3 months)

## Iterators

We have fast/forward with and without hierarchy. Several improvements can
be done to make it more useful.

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

## LNAST Opt

In LiveHD, LGraph has the cprop pass that performs constant folding, copy
propagation, strength reduction... and many other optimizations.


It may be useful to have a copy propagation, constant folding, and dead code
elimination in LNAST. There are several reasons:

* Doing code simplification early (LNAST is the earliest) reduces workload/steps in successive passes.
* The simulation saves checkpoints, a LNAST Opt without dead code elimination would be useful to create the intermediate values for debugging.

