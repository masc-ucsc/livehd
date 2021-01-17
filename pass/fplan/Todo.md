Benchmarking tests (built using -c opt):
 - BOOM RISC-V core:
   - flat: 
   - hier: 
 - XOR chain (100):
   - flat:
 - XOR chain (10,000):
   - flat:
 - XOR chain (50,000):
   - flat: 
 - XOR chain (100,000):
   - flat: 

TODOs:
1. Map a floorplan instance from ArchFP to a node instance
   1. write Node colors and parent colors to ArchFP
   2. extract floorplans from ArchFP w/o using file
   3. write easy nodes to LiveHD
   4. choose mapping for grid nodes, write to LiveHD
2. Analysis
   - total area
   - HPWL
   - ^^ but for a given LGraph
3. ArchFP code cleanup
   - replace 90's C++ code with std::vector / std::unique_ptr

Traversal improvements:
 - Node flat:
 - Node hier:
   - floorplan taking into account node type (ArchFP has internals for this?)
   - floorplan taking into account wire length (do a pass to determine wire length, then put stuff nearby using geography hints)
   - change between individual calls to addComponentCluster and a batched addComponentCluster based on node amounts (allows for better handling of huge modules)
 - Lgraph flat:
 - Lgraph hier:
   - fix it

General:
 - some sort of check() method that verifies netlists and internal functionality
   - find a way to discover illegal floorplans (overlapping segments)?
   - correct number of nodes are present
 - LGraph attributes (minor thing - node hier is more important)
   - Node attributes and Node pin attributes already exist (attribute.hpp)
   - Add aspect ratio information to LGraphs
   - Add placement hint info to LGraphs
 - Can ArchFP be multithreaded?
   - Check out the paper for ArchFP
   - see what can be improved on implementation
   - use other layout methods (bagLayout)?
 - Overlay lgraph borders over actual nodes?

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?

Bugs:
 - calling delete on geogLayouts causes problems for some reason
 - view.py output is flipped due to inverted vertical coords in cairo

LiveHD:
 - Node colors: make new color sets?

Easy:
 - add license stuff (incl lg_attribute.hpp)
 - update usage doc
 - add more asserts
 - update LiveHD doc on hierarchy traversal - mention if you need advanced global hierarchy tools, just extract the htree itself.
