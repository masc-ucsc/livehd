
# Deprepaced or Completed Projects

This is a list of deprecated or projects kept for historical reasons.


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



## Performance Monitoring Infrastructure (done)

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

## Pyrope/LNAST AI Warning

 (cancelled after trying. It was too fleaky)

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

## Cloud

(Deprecated to use bazel instead)

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


## (medium) HIF Yosys/nextpnr

(not really deprecated but not important for LiveHD)

Use HIF for Yosys read/write, and read in nextpnr. This should allow a faster
and more compact interaction between Yosys/nextpnr. Also, simplify the
structure in nextpnr allowing other tools to interface more easily.

## (hard) ACT (Async) output

Once pyrope with Fluid is in the flow, we could use Pyrope to program async
designs too.  It may be interesting to output ACT.

(https://avlsi.csl.yale.edu/act/doku.php) to interface with AVLSI flow.

## (medium) HIF CIRCT 

(not really deprecated but not important for LiveHD)

HIF (https://github.com/masc-ucsc/hif) stands for Hardware Interchange Format.
It is designed to be a efficient binary representation with simple API that
allows to have generic graph and tree representations commonly used by hardware
tools. It is not designer to be a universal format, but rather a storate and
traversal format for hardware tools.

The goal of this project is to create a HIF read/write interface with different
MLIR levels in CIRCT. This allows to have several tools/passes for simple
queries without requiring the whole compiler.



## (hard) UHDM 2 LNAST

UHDM is able to interface with several tools like Verilator/yosys. It may be interesting to create a bridge
between LNAST and UHDM.

Dependence: none

Main features:

* UHDM2LNAST
* LNAST2UHDM
* Support structs/bundles and most UHDM features
