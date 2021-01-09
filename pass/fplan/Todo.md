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
   - use colors initially
   - full mapping is basically two-pass
2. Analysis
   - total area
   - HPWL
   - ^^ but for a given LGraph
3. ArchFP code cleanup
   - replace 90's C++ code with std::vector / std::unique_ptr

Traversal improvements:
 - Node flat:
 - Node hier:
   - more information
   - floorplan taking into account node type (ArchFP has internals for this?)
   - floorplan taking into account wire length (do a pass to determine wire length, then put stuff nearby using geography hints)
 - Lgraph flat:
   - none
 - Lgraph hier:
   - fix it

General:
 - use node attribute for location
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
   - use other layout methods (bagLayout)?
 - Overlay lgraph borders over actual nodes?

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?

Bugs:
 - calling delete on geogLayouts causes problems for some reason
 - view.py output is flipped due to inverted vertical coords in cairo
 - dimension information doesn't persist across runs of lgshell?

LiveHD:
 - Node colors: make new color sets?

Easy:
 - add license stuff (incl lg_attribute.hpp)
 - update usage doc
 - have bazel clone archfp from github
 - add more asserts
