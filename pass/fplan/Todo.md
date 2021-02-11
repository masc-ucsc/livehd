Issues:
 - HardAspectRatio tries to satisfy the requested aspect ratio as well as possible, even if it's not possible without incorrect layouts.
    - This inevitably leads to problems when everything requests an aspect ratio of 1... is there a way around this?
    - Fixing aspect ratio issues is hard.  Checking if components collide with each other is $$ and figuring out where they should go is difficult.
       - Creating a slicing tree could be an option...?
 - ArchFP has a refcounting implementation built-in, which causes problems when destructors are called on anything other than the root node.  Replace with std::shared_ptr

Issues not related to ArchFP:
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo

 - blackboxes are not supported (use fixedLayout for this)
    - create a way to write aspect ratio / area information to each subnode

Node hierarchy:
 - add a is_valid method to Ntype_area, use it instead of hier_color to determine if a node has been placed yet
 - write non-root node to layouts[] in node_hier_floorp once I have a use for it

Easy things:
 - LiveHD has a random number class, use that in writearea

High level goals:
0. Test hier_lg with hier_test.v (waiting on yosys bugfix)
1. Test BOOM core (waiting on yosys memory implementation?)
2. Find / write a method that doesn't mess up on the wrong aspect ratio - HardAspectRatio not helpful for initial floorplans.

Things to add:
1. Optimization

    - use HPWL as benchmark
    - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
    - identify components that are far away, set geography hints to be closer?

    - multithread the floorplanner
    - multithread the Lgraph traversal
    - Check out the paper for ArchFP
    - improve implementation in ArchFP, fix todos, add better floorplan techniques
    - create a way to floorplan using existing layouts
2. Incremental Floorplans
    - floorplan using existing geography hints instead of randomly choosing a hint
    - assign geography hints to nodes based on wirelength metrics
3. More accurate floorplans
    - floorplan node pins - allows for more accurate HPWL estimation
    - scale area by bitwidth of node, if possible
4. add more checks to checkfp:
    - check for insane / unrealistic wirelength metrics
5. Overlay lgraph borders over actual nodes on png
6. Generate a slicing tree
    - allows for efficient floorplan analysis (disconnected segments, etc)

Tabled ideas:
 - Cairo is LGPL, so idk if we can ship it as part of LiveHD.  I'd have to choose a different library we can ship if someone needs floorplan images from LiveHD directly.
