Issues:
 - ArchFP has basically no error checking, and fails on large floorplans while returning successfully from layout() on the root layout...?
    - Using a high aspect ratio and putting things in grids alleviates this problem, but does not eliminate it.
 - ArchFP has a refcounting implementation built-in, which causes problems when destructors are called on anything other than the root node.  Replace with std::shared_ptr

Issues not related to ArchFP:
 - fix Lgraph hier traversal
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo

Things to add:
1. Optimization
   - use HPWL as benchmark
   - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
   - identify components that are far away, set geography hints to be closer?
   - if we have no information to go on, use basic assumptions (Const nodes can go anywere, memory nodes should be packed, etc.)
2. Incremental Floorplans
   - floorplan using existing geography hints instead of randomly choosing a hint
3. More accurate floorplans
   - floorplan node pins - allows for more accurate HPWL estimation
4. multithread hier_node traversal
   - Check out the paper for ArchFP
   - improve implementation in ArchFP, fix todos, add better floorplan techniques
   - create a way to floorplan using existing layouts
5. add more checks to checkfp:
   - make sure correct number of nodes are present...?  Does ArchFP have problems with this?
   - check for insane / unrealistic wirelength metrics
6. Overlay lgraph borders over actual nodes on png

Easy things:
 - there's no real reason `pass.fplan.writearea` actually has to accept any lgraphs, other than that lgshell crashes if none are provided.  There's probably a better way to create and store livehd mmap maps...
 - write gtest test cases for both simple and regular hierarchy files
   - make a floorplan and check it, basically
