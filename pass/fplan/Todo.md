Issues:
 - ArchFP has basically no error checking, and fails on large floorplans while returning successfully from layout() on the root layout...?
    - Using a high aspect ratio and putting things in grids alleviates this problem, but does not eliminate it.
 - ArchFP has a refcounting implementation built-in, which causes problems when destructors are called on anything other than the root node.  Replace with std::shared_ptr

Issues not related to ArchFP:
 - fix Lgraph hier traversal after getting hier_node working properly - useful for large designs (BOOM)
    - populate hierarchies with area
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo
 - blackboxes are not supported (use fixedLayout for this)

Node hierarchy:
 - check for TODOs elsewhere in the code and resolve if possible

 - check grid layout not pushing mirror contexts?
 - verify node hierarchy is correct
 - add a is_valid method to Ntype_area, use it instead of hier_color to determine if a node has been placed yet
 - write non-root node to layouts[] in node_hier_floorp

Easy things:
 - Node::bimap -> Node::map for hier_node_color fails?
 - LiveHD has a random number class, use that in writearea
 - put warning bazel file in //tools

Things to add:
1. Optimization
   - use HPWL as benchmark
   - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
   - identify components that are far away, set geography hints to be closer?
2. Incremental Floorplans
   - floorplan using existing geography hints instead of randomly choosing a hint
   - assign geography hints to nodes based on wirelength metrics
3. More accurate floorplans
   - floorplan node pins - allows for more accurate HPWL estimation
   - scale area by bitwidth of node, if possible
4. multithread hier_node traversal
   - Check out the paper for ArchFP
   - improve implementation in ArchFP, fix todos, add better floorplan techniques
   - create a way to floorplan using existing layouts
5. add more checks to checkfp:
   - check for insane / unrealistic wirelength metrics
   - check for disconnected parts of floorplan
6. Overlay lgraph borders over actual nodes on png
   - rewrite view.py in C++, and have it source data from LiveHD
