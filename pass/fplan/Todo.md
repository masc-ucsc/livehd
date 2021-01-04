Benchmarking tests (built using -c opt):
TODO: get a node-level floorplan going.  module level is useless for XOR chains.
 - BOOM RISC-V core:
   - flat: 
   - hier: ?
 - XOR chain (100):
   - flat:
 - XOR chain (10,000):
   - flat:
 - XOR chain (50,000):
   - flat: 
 - XOR chain (100,000):
   - flat: 

Misc:
 - write test for archfp, and find a way to discover illegal floorplans (overlapping segments)
 - some sort of check() method that verifies netlists and internal functionality
   - correct number of nodes are present

Improvements:
 - LGraph attributes
   - Node attributes and Node pin attributes already exist (attribute.hpp)
   - Add aspect ratio information to LGraphs
   - Add placement hint info to LGraphs
 - Can we multithread ArchFP?
 - Check out the paper for ArchFP
   - see what can be improved on implementation
 - Write floorplan back into hierarchy tree?

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?


 - floorplan at node level
   1. floorplan nodes
   2. create node attribute for size
   3. exclude hier and attr nodes, and others?
   4. write node floorplan information back into node?


 - floorplan taking into account node type (ArchFP has internals for this, I think)

Bugs:
 - calling delete on geogLayouts causes problems for some reason

Easy:
 - add license stuff (incl lg_attribute.hpp)
 - update usage doc (http://lava.cs.virginia.edu/archfp/index.htm)
 - have bazel clone archfp from github
