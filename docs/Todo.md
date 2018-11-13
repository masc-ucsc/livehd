# List of projects / features that are needed in LGraph

## 1.Synthesis

* S01 - Liberty Reader / TMap
     1. @Rohan Find a Liberty parser or write one
     2. @Rohan Read cell delays

* S02 - OpenTimer
   1. @Rohan First iteration:
        * Initially, add "fake" cell delays
        * Need to export the graph to something OpenTimer can read, or integrate
        OpenTimer as a library
        * Calculate critical path information
        * No dependencies
   2. @Rohan Second iteration:
        * Use information from Liberty file (take into account cell size/fanout)
        * Depends on S01
   3. Additionally, read constraint file (make compatible with ABC and synopsys
      SDC)
        * No dependencies
   4. Generate nice report\_timing feature
        * Depends on S02.i
   5. @Rohan Annotate the graph with critical path info
        * Depends on S02.i
* S03 - @? Cell sizing
    1. After timing, implement cell sizing algorithm (this entails re-mapping some
      cells)
        * Depends on S02.v
* S04 - @? Logic Synthesis / Optimization
    1. Port some logic optimization from Yosys
        * No dependencies
    2. Add any optimization that would be neat (basic logic operator)
        * No dependencies
    3. Espresso:
        * Add the ability to export any two level sub-block and optimize it with
        espresso
        * Read back into the graph replacing the original block
    4. Find heuristics on where to apply Espresso (This can be part of a background
      optimization process in the graph)
        * Depends on S04.iv
* S05 - @? TMap
    1. Extra project
    2. Add TMap algorithms directly in the graph
        * Depends on S01
    3. Ideally, read both CCS and NLDM formats (not done in ABC, not sure if
      supported by OpenTimer)
        * Depends on S01 and S02
* S07 - @? Power / Area ?
    1. Any way to estimate power / area?
        * Depends on S01
    2. This would be a rough estimation before physical implementation

## 2. Physical Design

* P01 - Placement:
    1. @? Use yosys quadratic placement (qwp) to populate coordinates (need to
      patch yosys to read placement info)
        * Depends on P01.i
    2. @? Integrate opensource placer?
        * Depends on P01.i, P01.iii
    3. @? Implement Analytical Placement
        * Depends on P01.i
    4. @? Integrate open source legalization
        * Depends on P01.i, P01.iii
* P03 - Placement metrics:
    1. @? Wirelength estimation
        * Depends on P01
    2. @? Area estimation
        * Depends on P01
* P04 - Bookshelf integration
    1. Extra project? (maybe we need it at least for technology information like
      metal resistance and so forth)
    2. @? Read/write bookshelf
        * @? Same as for LEF/DEF/LIB, try not to overdepend on all of them, use as
      much information as possible from each
        * Depends on P01.i
* P05 - OpenTimer Reloaded:
    1. @? Feed placement information to OpenTimer for more accurate timing
      estimation
        * Depends on S02 and P01
* P06 - Routing
    1. @Sheng? How to represent wires?
    2. @Sheng? Integrate open source router (fastrouter?)
    3. @Sheng? Implement SAT based router
* P07 - CTS / PDN?

## 3. Simulation
* L1 - LLVM inou
* L2 - Testbench1 generation (from C/C++)
* L3 - Testbench2 verilog (using yosys and synthesizable verilog only)
* L4 - Testbench3: any plans for non-synthesizable verilog support?
* L5 - Physical Simulation: It should not be that hard to pass physical information once we have it

## 4. Pyrope:
* H1 - Pyrope tb
    1. @?  (will they go through the graph?)
        * Depends on H1
* H2 - Pyrope inou test
    1. @? Read verilog, write pyrope, read pyrope, write verilog, sat
       solve
        * Depends on H1

## 5. Live:
* L1 - @? Logic Decomposition
    1. Reduced the subgraph size
        * Depends on L1
* L2 - @? Incremental Placement
    1. Use partially solved matrix for analytical replacement
    2. Fix position of cells not replaced
        * Depends on L1, P01
* L4 - @? Closed loop tests with sat solvers


Extra TODOs:

LLVM:

* Avoid unnecessary bit size checks

  If we prove that a value never goes over X-bits, no need to use a smaller bit size (and mask) to drop
the upper bits

* Continuous flop array (memcpy)

## Compactable char array

* Create a char array where string being added can be compacted on the fly
      - This will allow us to reduce the memory footprint when storing very
        similar names (net_0, net_1, net_2, ...)
      - The algorithm can be simple, but needs to be fast
      - We should also be able to search/print the compacted array


# Improve lgraph usage

* Create methods to get type specific parameters
      - eg: shift amount, shift sign extension, pick offset, pick width
      - memory parameters, so forth

# Known bugs and other maintenance needs

## Memory Usage

* Implement a persistent sparse template for generic types (like the current
    dense)
      - It should use a memory map, and implement the same methods as the current
        persistent types: clear, reload, sync, emplace_back
      - Emplace\_back should not actually allocate memory every time, mostly when
        adding new elements.

* When creating a new graph (calling clear), the sizes of the files are not deallocated.
      - This happens due to the lazy instantiation of the file pointer. Also, when save\_size is called, it does not call munmap right way.
      - There are two main options:
          - Open the mmap file when mmap\_allocator is instantiated (we want to avoid that)
          - Make sure munmap is called when cleaning. But not in other cases that save\_size is called

* Memory profiling
      - Find a good library to profile memory usage in lgraph. It doesn't look like it is a problem right now, but it is a nice tool to have

* Create a histogram of number of edges (sedge vs ledge) per node in the benchmarks
      - Should we reduce the internal node from 64 to 32? This would reduce to the number of edges from 27 shorts (or 9 longs) to 11 shorts (3 longs)

* Implement dense vector that can be reallocated when the mmap grows
      - The std::vector implementation does not allow mmap reallocation

## JSON generation

* Complete json description to take into account extra fields
      - Some missing fields: node delay, wirename, subgraph id, ...

* Closed loop json test
      - Go to json, back to lgraph and compare lgraphs

## Verilog generation

* Prevent the generation of wires used for parameters only
      - Maybe one way of doing it is generating the constants only when used, not
        in the "fast" pass in the beginning.

* Patch memory ports with forwarding
      - This will most likely involve patching yosys to recognize different code
        styles for memory forwarding.

## Style:

* all the methods in LGraph\_Base extensions should match the beginning. E.g: all node\_type\* or node\_delay\*
     - rename node\_u32type\_set to node\_type\_set\_u32
     - rename node\_value\_get to node\_type\_get\_u32

* rename node\_file\_loc\_get to node\_loc\_get
     - node\_log\_get should return a const File\_loc & (to avoid copy overheads)

* remove the node\_file\_name\_get
     - extend File\_loc to include a ptr to filename

* rename nodesrcloc.\* to nodeloc.\*

## Plugin like extension

* Instead of extending Graph\_base, we create a plug in interface, and to access
  the individual fields, we have a "vector-like" interface.
    - How to handle multiple graphs cleanly? node\_type::get(g)[nid], is it slow?

```cpp
  Node_type &nt = node_type::get(g);
  nt[nid] = 3;
  for(auto idx:g.fast()) {
    lc[idx].file_name
  }
```

## General

* Delete char\_array entries
      - We want to be able to remove a char\_array entry
      - If possible, we want to be able to reuse the space to prevent leakage

* Capacity to delete a root node with no edges. To avoid spourios type/delay/... data. Call a "clear(nid)"
  method in each of the types. If a value is read before a set or after a clear, the LGraph_Node should raise
  an exception.
      - Get nodes from free list

* lgrand
      - add the option to randomly add/delete edges and do iterations. A way to create a benchmark

* use catch (catch-lib) for unit testing. Looks nicer and more in tune with our Pyrope style

