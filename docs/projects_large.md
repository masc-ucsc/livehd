
# Open Medium/Large Size Projects

Open projects are potential MS thesis/projects. They are ranked from "most" to
less "urgent". Each has a easy/medium/hard level. Easy should take a a bit over
a quarter to implement, hard could take 2-3 quarters.

## [medium] Alternative cprop with egg for LNAST

Egg (https://dl.acm.org/doi/10.1145/3434304) allows to perform optimizations similar to cprop, but a bit cleaner more optimal.

It would be interesting to have a another pass that operates over LNAST nodes
and uses egg to perform optimizations.

## [medium] Verilog input (slang 2 LNAST)

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

## [easy] LNAST to Pyrope

Dependence: none

Main features:

inou.code_gen shared code between C++, Verilog, Javascript, and Pyrope code
generation. Pyrope and Javascript code generation do not need bitwidth are are
the easier to fix. Pyrope may be the easiest because it does not need any support
code like jops or sops.

## [medium] LNAST to C++ 

Dependence: 
 OPT1: LNAST bitwidth and sops
 OPT2: dops

Main features:

To avoid the longer dependence with LNAST bitwidth, ut may be good if the first
release targets dops (Dynamic OPS core).

inou.code_gen shared code between C++, Verilog, Javascript, and Pyrope code
generation. The current inou.code_gen.verilog is very defective. Once we have a
LNAST bitwidth, we may want to patch it. The BW patch is also needed to create
correct signed/unsigned Verilog code.

## (medium) Parallel and Hierarchical Synthesis with Mockturtle

Mockturtle is integrated with LiveHD. The goal of this task is to iron out bugs
and issues and to use the LiveHD Tasks API to parallelize the synthesis.

Dependence: none

Main features:

* The current synthesis divides the circuit in partitions. Each partition can be synthesized in parallel.
* Support hierarchical synthesis to optimize cross Lgraphs (cross verilog module optimization)


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

## [easy] LNAST to Javascript

Dependence: none

Main features:

Once jops is done (or done at the same time of MS thesis), we should be able to
translate from LNAST to javascript. All the file "test" blocks could be tested
in a browser client as the program executes/edits the code.

The LNAST javascript does not need the LNAST bitwidth like LNAST to C++ and
LNAST to Verilog, it should be able to work with just generated LNAST from
passes like inou.prp/inou.verilog (not inou.chisel because it needs translation
steps)

## [medium] LNAST to  Verilog 

Dependence: LNAST bitwidth

Main features:

inou.code_gen shared code between C++, Verilog, Javascript, and Pyrope code
generation. The current inou.code_gen.verilog is very defective. Once we have a
LNAST bitwidth, we may want to patch it. The BW patch is also needed to create
correct signed/unsigned Verilog code.

The inou.cgen.verilog can be a good hint on the verilog semantics.

This pass has an easier verification step because it is possible to do LEC
against inou.cgen.verilog

## (very hard) Bring Back Incremental Synthesis to Lgraph

WARNING: Traversal depdendence

Dependence: none

Main features:

* Reimplement the DAC synthesis in Lgraph
* Re-run anubis to get data

## (medium) Hot-Reload Simulation Console

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

## (medium) Keep Lgraph topologically sorted (incremental)

Dependence: iterators in graph_core

Loading/saving a Lgraph is already topologically sorted. If we keep additions
topologically sort, we can avoid the more costly forward/backward traversal
operations.


The goal is to have to track nodes not in topological order. The load/save does
not need to keep it just during runtime.

## (easy) HIF Tooling

HIF (https://github.com/masc-ucsc/hif) stands for Hardware Interchange Format.
It is designed to be a efficient binary representation with simple API that
allows to have generic graph and tree representations commonly used by hardware
tools. It is not designer to be a universal format, but rather a storate and
traversal format for hardware tools.

LiveHD has 2 HIF interfaces, the tree (LNAST) and the graph (Lgraph). Both can
read/write HIF format. The idea of this project is to expand the hif repository
to create some small but useful tools around hif. Some projects:

* hif_diff + hif_patch: Create the equivalent of the diff/patch commands that
  exist for text but for HIF files. Since the HIF files have a more clear
  structure, some patches changes are more constrained or better understood
  (IOs and dependences are explicit).

* hif_tree: Print the HIF hierarchy, somewhat similar to GNU tree but showing the HIF hieararchy.

* hif_grep: capacity to grep for some tokens and outout a hif file only with those. Then a hif_tree/hif_cat can show the contents.

## [easy] unbitwidth Local and Global bitwidth

Dependency: inou.cgen.pyrope and LNAST bitwidth

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


## [medium] Package Manager

Create a cargo/npm-lite package to interact with LiveHD shell. The package manager
is not to run, but to download, and setup bazel system for remote packages. If
there are some run/bench/build, they are just proxies to bazel commands.

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
  "toolchain" to decide/select how to run the tools. 

* It does not have tons of options. The options are in toolchains that can be
  configured (not in the lhd.toml that should be quite minimal). The default is
that everything should run in the LiveHD toolchain, but it can be extended to
provide new toolchains.

* It hides some of the bazel complexities for new users, but it allows full bazel
scripting to co-exists.


## (easy) HIF Javascript/rust/python

HIF (https://github.com/masc-ucsc/hif) stands for Hardware Interchange Format.
It is designed to be a efficient binary representation with simple API that
allows to have generic graph and tree representations commonly used by hardware
tools. It is not designer to be a universal format, but rather a storate and
traversal format for hardware tools.


The goal of this task is to create HIF read/write APIs for popular languages
like Python, Javascript, and RUST.

## (medium) SMT Solver for LEC

There are several attems to translate LGraph to SMT (lec pass), but none is working.

The proposal is to create a pass that uses https://github.com/stanford-centaur/smt-switch as
a common SMT front-end. There is an API to can process each node and create an smt-switch
circuit. As a sample of use, iterating over 2 LGraphs, the resulting smt-switch can be
used as LEC. Another sample of use is to iterate over 2 subsections of a LGraph and check
if the output edges are equivalent (to allow simplication).

Sample passes to perform:

* Optimize equiv check. Traverse and remove subsets with same functionality
* Allow to SAT equiv graphs marked
* Reduce ports in rams, and collapse muxes when parallel/full case
* Partition graph in sub-graphs. Each subgraph can synthesis/check by itself.


Potential functionality to simplify. Remove the ezSAT pass, use smt-switch.

Potential paper:

* Use the net names from incremental synthesis to partition design (if partition verifies, we are done)
* Use structural traversal to remove search space (from inputs/outputs)
* Use parallel SAT checks (each output cone, partition of cones, after synth)
* Mark "boundaries" for equivalence in lgraph. Faster synthesis, verification...
* Different SAT solvers in parallel (ezsat, CVC4, SPT) and pick fastest

## (medium) Useful Lgraph Passes

Lgraph has several passes. Many compiler like passes that can be used by
several projects. This is a set of those unrelated passes.

Dependence: none

Main features:

* Find combinational loops and notified LoC responsible
* Find cross clock domain and check that it has acceptable patters to cross. Allow to be extensible
* Copy propagation and Dead-code elimination in LGRAPH with and without hierarchy
* Check Lgraph consistency. E.g: NOT should have same bits input/output edges

## (medium) NextPNR

Integrate with the nextPNR FPGA placement/routing

Dependence: mockturtle

Main features:
* Create a bridge to/from Lgraph and nextpntr (json)



## (medium) Zoom drom viewer

 Help with https://github.com/wavedrom/zoom

 This can become a nice viewer for LiveHD simulations (verdi substitute goal)

## (hard) Lgraph to/from XLS IR

Create a bridge between Lgraph and XLS IR. Not LNAST because the XLS IR is very
close to Lgraph. Maybe the interface is with HIF format.

Dependence: none

Main features:

* Bring XLS build to combine in a single bazel
* Bridge to allow runs of DSLX to LiveHD
* Bridge to allow from XLS IR to LiveHD
* Bridge to allow from Lgraph to XLS IR

## (hard) Google Circuit-Training Floorplanner

The circuit training (https://github.com/google-research/circuit_training) provide a mechanism to floorplan
some blocks. It uses a placer and AI, but the code is quite messy. Namely:

-It uses hmetis (license is not open)
-Several python scripts (not easy to deploy or automatic)
-To estimate time, it would be nicer to use opentimer/livehd.

The idea would be something more automatic. E.g:

> lgraph.open X |> pass.compiler |> pass.synth |> pass.circuit_training 


It would be good if the project is tested also with FPGAs and check with something like nextpnr/vivado

## (very hard) FPGA Placer

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

