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

Traversal improvements:
 - Node flat:
   - more information
   - node size
   - floorplan taking into account node type (ArchFP has internals for this?)
 - Node hier:
   - node size
 - Lgraph flat:
   - none
 - Lgraph hier:
   - fix it

General:
 - add node attribute for area
   - area per node type?
 - add/use node attribute for location

 - write floorplan information back hierarchy?
 - some sort of check() method that verifies netlists and internal functionality
   - find a way to discover illegal floorplans (overlapping segments)?
   - correct number of nodes are present
 - LGraph attributes
   - Node attributes and Node pin attributes already exist (attribute.hpp)
   - Add aspect ratio information to LGraphs
   - Add placement hint info to LGraphs
 - Can ArchFP be multithreaded?
   - Check out the paper for ArchFP
   - see what can be improved on implementation

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?

Bugs:
 - calling delete on geogLayouts causes problems for some reason
 - view.py output is flipped due to inverted vertical coords in cairo

Easy:
 - add license stuff (incl lg_attribute.hpp)
 - update usage doc
 - have bazel clone archfp from github
