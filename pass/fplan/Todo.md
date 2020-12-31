Benchmarking tests (built using -c opt):
 - BOOM RISC-V core:
   - flat:
   - hier:
 - 500K XOR table:
   - flat:
   - hier:

 Improvements:
 - Some way to visualize floorplans to check if they're correct
   - ArchFP perl script doesn't work properly
   - esesc perl script doesn't work properly either
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

Long term:
 - some sort of check() method that verifies netlists and internal functionality
 - a way to call BloBB or CompaSS directly, since they're really fast (faster than ArchFP? Need to test this...)


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

Tried:
 - namespaces: none of the other passes are namespaced so whatever
