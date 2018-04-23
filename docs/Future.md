
Integrate OpenTimer

 -extend opentimer to handle LEF?

 -Reuse LEF/DEF reader from opentimer?

Incremental timing with something like:

 LibAbs: An Efficient and Accurate Timing Macro-Modeling Algorithm for Large Hierarchical Designs

----------------------------
projects/MS thesis:

openware (Garvit)

LEF/DEF (Zachary)

ABC Interface ()

Global Router

Legalization

C++11 GDS generator

Timing (Rohan)
  Our own fast timing model
  OpenTimer integration

Fast Parasitic Extraction

In-Place Conjugate Gradient Solver

Small projects
  yaml 2 json

----------------------------

 Cloud based parallelism. Use C++ library?

 https://github.com/datasift/served
 https://github.com/Qihoo360/evpp

 Build a library for Asynchronous Stocastic Gradient Descent (incremental) to reuse across

 Start detailed routing synchrnously, do the job totally parallel. If something could not be routed,
try to handle locally. Propagate to neighbour only if not possible.


-----

 A docker https://flynn.io or dokku or kubernetes

 -One long term option is to listed in port 80, and submit jobs to load balance. This seems fine
for synth, but for place/route a mesh is more realistic.

 For the HPC (mesh). Build a VLSI optimized mesh library? We may want to have
an octree to decide different levels of mesh granularity. If the circuit has
100M gates, 10K gates per graph, this gives around 10K graphs (100x100). The
max grid size (lots of communication should be 100x100). Even with unlimitd
resources something like 10x10 seems more reasonable (10x10 subgraph, on
average should have 4****10 graphs in the edges, if 10% are in/outs. The graphs
in the edges may be mapped by two bins). We have to deal with the communication
factor.

 Maybe a 1000+ cluster for synthesis, and another mesh cluster for placement/routing/timing.

 It would be good if synth/placement/CTS/SRAM opt can happen in parallel (overlap)


