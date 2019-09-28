
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


## FIRRTL 2 LNAST

There is an ongoing effort to bring from frontends to LNAST. This is a frontend/backend
project ot interface with high level FIRRTL.

Dependence: none

Main features:

* Bridge between high level FIRRTL and LGraph using LNAST (to/from FIRRTL translation)
* Ideally handle "high level FIRRTL" Then, we can read out of CHISEL.
* Benchmark is rocketchip and BOOM
* How to handle annotations? (unclear at the moment if we need this)

## slang 2 LNAST

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

## lhdview

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
 * Query to LGraph to have "order" in variables computed. This is shown in the wave form like small delays (up to 40% of the cycle for delays? maybe configurable)
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

## LGraph partition/decomposition/coloring

Implement several partitioning/coloring algorithms in LGraph. The
attributes/colors could be used for synthesis/placement/...

Dependence: none

Main features:

* Implement paper: A Fast Heuristic Algorithm for Disjoint Decomposition of Boolean Functions
* Break graph partitions in disjoin sets and areas that do not have cross optimization (disjoin)
* Mark graph with hypergraph partition
* Patch traversal so that we have fast/forward/backward for a "color graph"

## Bring Back Incremental Synthesis to LGraph

WARNING: Traversal/color depdendence

Dependence: none

Main features:

* Reimplement the DAC synthesis in LGraph
* Re-run anubis to get data

## OpenWare

We need a set of set of implementations with different trade-offs for each basic LGraph gate. The implementations
could optimize for FPGA and ASIC.

Dependence: none

Main features:

* Custom implementations for all the LGraph cells: reductions, add/sub, mult, shift, barrel shifter...
* The OpenWare library is built in Pyrope, with efficient verilog generated
* Benchmark the OpenWare against FPGA and ASIC default (designWare) targets
* Specific target implementations for FPGA (Xilinx) and ASIC (generic)
* Several speeds/trade-offs for each major block. E.g: adder RCA/Kogge/...

## Synthesis (ASIC/FPGA) Competitive Analysis

Competitive analysis finding main weakpoints for the synthesis flow. Both FPGA and ASIC targets.

Dependence: Mockturtle + OpenWare projects

Main features:

* Goal to run several large tests (Anubis) and BOOM and Titan23 (http://www.eecg.utoronto.ca/~kmurray/titan.html).
* Compare against vivado, quartus, DC, ABC, Mockturtle.... results can not be published directly but OK as A/B/C vs LGraph+XX.
* Important to focus on OpenWare LGraph flow vs others
* Create regression system that plots freq/area/power for several key benchmarks

## Useful LGraph Passes

LGraph has several passes. Many compiler like passes that can be used by
several projects. This is a set of those unrelated passes.

Dependence: none

Main features:

* Find combinational loops and notified LoC responsible
* Find cross clock domain and check that it has acceptable patters to cross. Allow to be extensible
* Copy propagation and Dead-code elimination in LGRAPH with and without hierarchy
* Check LGraph consistency. E.g: NOT should have same bits input/output edges

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

## Liberty/LEF/DEF

Liberty, LEF, and DEF are de-factor industry standards. The goal is to parse them and populate the graph library accordingly.

Dependence: none

Main features:

* Liberty, LEF, and DEF are standard EDA file formats. We have some "read" pass in lgraph.
* Capacity to read/write the three formats (load/save)
* Create an object to hide the interface with a C++17 iterator.
* Capacity to merge many files in the same object (inou_lef, inou_def, inout_liberty)
* The read formats should be backed with mmap_lib (zero load time). Single per lgdb directory
* Multithread ready (infrequent update, frequent read)
* Unit tests for correctness and performance
* Populate subgraphs for Liberty and LEF
* Create connectivity for LEF/DEF
* Export LEF/DEF and liberty from internal DB. E.g: read several liberty, and then export a single one which is the aggregated.
* LEF/DEF export can be tagged for a color

## Analytical Placer

Implement an analytical placer that should work with FPGA/ASIC.

Dependence: Rapidwright

Main features:

* FPGA and ASIC targets
* Use stanford semidefinite
* Use timing in feedback
* Flop Clustering: "Flip-ﬂop Clustering by Weighted K-means Algorithm"
* Overall flow based on: "BonnPlace: A Self-Stabilizing Placement Framework"
* Do not assume shape (square) in contour. Allow to place around each floorplan area (amorphous. A center of gravity per floorplan block)
* To allow fast incremental placement. Allow to do analytical over subset of design, and other areas are fixed (legalization to handle)
* Critical paths are aligned
* Support "annotations"
    * Relative place (left/right/top/bottom)
    * Alignment (true/false)
    * Close by (add anchor)

## Legalization

Legalize ASIC/FPGA cell/LUT placement.

Dependence: Rapidwright

Main features:

* Based on " A Fast, Robust Network Flow-based Standard-Cell Legalization Method for Minimizing Maximum Movement" but applied to ASIC and FPGA
* Handle incremental. Only marked blocks need to be legalized. Already legalized blocks should not move (faster incremental that avoids re-routing blocks)
* Handle macros (SRAMs, DSPs, ....) that can have floating or fix location

## Placer simulation annealing

Implement a simulation annealing placer that should work with FPGA/ASIC.

Dependence: Rapidwright

Main features:

* FPGA and ASIC targets
* Can have many constraints:
    * cluster flops
    * Timing
    * pin aligment
    * favour structure
    * Easy to add extra rules
* It should be slow than analytical, but to speedup the search, it can start with an analitical place design.
* Support "annotations"
    * Relative place (left/right/top/bottom)
    * Alignment (true/false)
    * Close by (add anchor)

## Floorplanner

Implement a floorplanner for large LGraph designs (firesim target).

Dependence: coloring

Main features:

* Planar graph. Cluster pipeline stages until the graph is planar.
* Placed blocks do not need boundaries, just centers (analytical placer will handle the shape.
* Implement a more traditional floorplanner leveraging min-cut.
* Do hierarchy. Hierarchy preserves symmetry. Bassed on "A hierarchical approach for generating regular ﬂoorplans"
* Different partitions are marked with different colors.

Some related papers in clustering:
* FADE: Graph dra wing, clustering and visual abstraction
* A Semi-Persistent Clustering Technique for VLSI Circuit Placement
* Net Cluster: A Net-Reduction-Based Clustering Preprocessing Algorithm for Partitioning and Placement
* LSC: A large-scale consensus-based clustering algorithm for high-performance FPGAs
* Enumeration technique in very large-scale integration fixed-outline floorplanning
* A hierarchical approach for generating regular ﬂoorplans
* Network Flow Based Datapath Bit Slicing


## Performance Monitoring Infrastructure

We perform regression for correctness, we should have regressions for
performance. This includes completion times and tracking CPU performance
counters. The setup uses prometheous/grafana
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

## ECO pass with LGraph

Leverage the incremental and push it further to make LGraph the ECO flow for open source

Dependence: bring back incremental synthesis, coloring, and SAT solver

Main features:

* Incremental synthesis optimizes for speed, ECO optimizes for small size partition.

## Amazon F1

Clean productive flow for Amazon F1

Dependence: Rapidwright, incremental synthesis

Main features:

* Do a full flow of LGraph for F1. Fast incremental

## OpenRoad

Integrate OpenRoad flows with LGraph

Dependence: Liberty, LEF/DEF

Main features:
* Pick one of the OpenROAD projects and port it to LGraph. Compare against original.
* Mostly a show cases for OpenROAD of the advantages of using LGraph as the core.

## Pyrope to LNAST (Kenneth Mayer)

Create a elab parser for Pyrope and create LNAST nodes

Dependence: none

Main features:
* Implement Pyrope ELAB custom parser
* Error/warning friendly (clang class quality)
* Very fast parser
* Translate to LNAST

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

## RippleFPGA

RippleFPGA is one of the best open source placers avaiable for FPGAs (ICCD competition). The idea is to integrate
it with LGraph.

https://github.com/cuhk-eda/ripple-fpga

Dependence: none

Main features:
* Create a bridge to/from LGraph and ripple-fpga
* Replace solver with https://developers.google.com/optimization/lp/glop
* Replace patoh with https://github.com/SebastianSchlag/kahypar
* Being able to transfer placed design to Rapidwright
* Remove boost
* Target Alveo U250

## NextPNR

Integrate with the FPGA synthesis from Yosys

Dependence: none

Main features:
* Create a bridge to/from LGraph and nextpntr
* Being able to transfer placed design to Rapidwright

## VPR

Integrate LGraph wiht VPR

Dependence: none

Main features:
* Create a bridge to/from LGraph and nextpntr
* Being able to transfer placed design to Rapidwright

# Active Projects (already selected)

## Cloud

Main requirements:

* Able to scale up/down automatically, and down to zero if not used
* Fast latencies
* Able to use google/AWS and local servers

Some potential implementation:

* REST API for all the servers.
* Use httplib. See lgraph/main/userver_test.cpp and lgraph/main/uclient_test.cpp
* lgshell commands
    * cloud.server
    * cloud.ping     ; ping a server or front-end
    * cloud.frontend ; setup a front-end server to distributue the work to existing servers
    * cloud.client   ; setup lgshell as client. next commands may go to server
* Server and client must have same git clone token (version) check for consistency.
* When in server mode, transfer client files, monitor server files, and transfer back
* Add bazel the option of creating a alpine docker image directly to deploy lgraph in the cloud as an rest API service
* Create bringup and shutdown inside lgshell. Allow for local machines and gcloud/aws
    * cloud.start ssh:mada1 ssh:mada2
    * cloud.start gcloud xxx-parameters-for-gcloud
    * cloud.shutdown # kills any cloud.start that was spawned before

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


# Past Projects

## Mockturtle (Qian Chen)

Integrate EPFL mockturtle (https://github.com/lsils/mockturtle) with LGraph. The main charactersistics:

* Use mockturtle to tmap to LUTs
* Use mockturtle to synthesize (optimize) logic
* Map to LUTs only gates and non-wide arithmetic. E.g: 32bit add is not mapped to LUTS, but a 2-bit add is mapped.
* List of resources to not map:
    * Large ALUs. Large ALUs should have an OpenWare block (hardcoded in FPGAs and advanced adder options in ASIC)
    * Multipliers and dividers
    * Barrell shifters with not trivial shifts (1-2 bits) selectable at run-time
    * memories, luts

Some tasks that were not finished that a potential future project can address: (Good undergraduate projects)

* Split the graph coloring code out of pass/mockturtle to pass/coloring
* Extend the LUTs to have "bit-width". If a LUT operates over a "bus", it can have a multi-bit input per port".
* Avoid simple NOTs by having negated ports. E.g: 0-3 are possitive, ports 4-7 are negated inputs.
* Go over the [cleanup.md](cleanup.md) pending tasks.

## ABC (Yunxun Qiu)

Integrate ABC with LGraph. The interface use the C-API, bit the file dump format.

# Open Medium Size Tasks (not MS project/thesis)

This is a list of small tasks. Each should take 1-3 weeks to implement. These
are not thesis/projects but good ideas to get to know the setup and help, and they
can evolve for undergraduate senior design.


## mmap_lib benchmark tune

Do some fine grain benchmarking. E.g: show the impact of huge TLB with filesystem backup and
performance impact. Setup mada0 and script to setup for multiusers hugeTLBfs

## Setup a vagrant image

We have several dockers for testing, a simple vagrant (ubuntu based?) for most users may
be nice to have. Maybe based on https://github.com/VLSIDA/openram-vagrant-image

## Fix lgshell

https://github.com/rubund/netlist-analyzer

## Fix lgshell

* **Autocompletion for lgraph names too (now, it is just files).
* Autocompletion patch for directories. Now finished with "foo", it should be "foo/"
* Upgrade to the latest replxx. There was a change in API, and it requires to rework lgshell

## Elab parser

The memblock is typically mmap, and the token list is managed manually. It
would be nice to have a Scanner context class that keeps the memblock and token
list. It can serialize the token list if needed.

 * Clearer API
 * Capacity to serialize (optional)
 * Callback when a memmap changes to retrigger parse, and pass token list to step down the flow

## Query shell (not lgshell) to query graphs

* Based on replxx (like lgshell)
* Query bits, ports...  like
    * https://github.com/rubund/netlist-analyzer
    * https://www.jameswhanlon.com/querying-logical-paths-in-a-verilog-design.html
* It would be cool if subsections (selected) parts can be visualized with something like https://github.com/nturley/netlistsvg
* The shell may be expanded to support simulation in the future

## Benchmark API in lgshell

* Able to get time and performance statistics for tasks in lghsell
* perf.start, perf.stop, perf.dump commands to allow things like:
```
lgshell> perf.start
lgshell> lgraph.open name:foo |> ....
lgshell> perf.stop

## Setup gupm for Pyrope and LiveHD

https://github.com/azukaar/GuPM

* Create pyrope repo for gupm
* Allow to have pyrope libraries (adder, multipler, corex....) as packages
* Allow to specify a specific lgraph library
* Allow to specify passes/commands in lgraph

## Smaller tasks

For even smaller tasks check the [cleanup.md](cleanup.md) file

