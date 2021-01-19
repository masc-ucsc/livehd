Benchmarking tests (built using -c opt):
 - BOOM RISC-V core:
   - flat: 
   - hier: 
 - XOR chain (100):
   - flat: ??

Bugs:
 - calling delete on geogLayouts causes problems for some reason
 - view.py output is flipped due to inverted vertical coords in cairo
 - passing super flat and wide hierarchies causes problems in ArchFP
 - fix Lgraph hier traversal

TODOs:
0. Verification
   - check for insane / unrealistic wirelength metrics
1. Analysis
   - total area, number of components, 
   - HPWL
   - Regularity (equation in HiReg paper)
2. Optimization
   - use HPWL as benchmark?
   - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
   - identify components that are far away, set geography hints to be closer?
   - if we have no information to go on, use basic assumptions (Const nodes can go anywere, memory nodes should be packed, etc.)
3. Incremental Floorplans
   - floorplan using existing geography hints instead of "Center" for everything
4. More accurate floorplans
   - floorplan node pins - allows for more accurate HPWL estimation
   - change between individual calls to addComponentCluster and a batched addComponentCluster based on node amounts (allows for better handling of huge modules)
     - might want to have different thresholds for this - probably want to pack memory cells more than const cells!

General:
 - checkfp:
   - correct number of nodes are present
   - no weird disconnected segments?
 - multithread hier_node traversal
   - Check out the paper for ArchFP
   - see what can be improved on implementation
   - create a way to floorplan using existing layouts
 - Overlay lgraph borders over actual nodes

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?
 - replace 90's C++ code in ArchFP with std::vector / std::unique_ptr?

Easy:
 - add license stuff
 - update usage doc
 - add more asserts
 - update LiveHD doc on hierarchy traversal - mention if you need advanced global hierarchy tools, just extract the htree itself.
 - write gtest test cases for both simple and regular hierarchy files
   - make a floorplan and check it, basically
