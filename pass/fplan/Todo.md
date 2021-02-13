Issues:
 - HardAspectRatio tries to satisfy the requested aspect ratio as well as possible, even if it's not possible without incorrect layouts.
    - This inevitably leads to problems when everything requests an aspect ratio of 1... is there a way around this?
    - Fixing aspect ratio issues is hard.  Checking if components collide with each other is $$ and figuring out where they should go is difficult.
 - ArchFP has a refcounting implementation built-in, which causes problems when destructors are called on anything other than the root node.
    - Does this need to be fixed?  It's well encapsulated by the Lhd_floorplanner class...

Issues not related to ArchFP:
 - fix bug where not passing lgraphs into a pass crashes LiveHD
 - fix checkfp bug(s)
 - fix analyzefp bug where node names are wrong
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo

Node hierarchy:
 - write non-root node to layouts[] in node_hier_floorp once I have a use for it

Ask about:
 - LiveHD has a random number class, use that in writearea
 - put fplan tests into yosys tests since they crash livehd fairly often
 - Dynamic loading of cairo lib...?
 - reason why I need mmap_tree:
 1. HighReg depends very heavily on hierarchy trees to function, and I don't think it's the only one.  Leaves open the possibility of more passes.
 2. When working with nodes (which is going to be a lot when more analysis passes get added), I need both implementation information and hierarchy information (which module instance is this node a part of?).  Currently there is no easy way to get this information.

Goals:
0. Test hier_lg with hier_test.v (waiting on yosys bugfix)
1. Test BOOM core (waiting on yosys memory implementation?)
2. Write node level hierarchy to file (mmap_tree not being written to file is known TODO)
3. Find / write a method that doesn't mess up on the wrong aspect ratio - HardAspectRatio not helpful for initial floorplans.

Things to add:
0. Interactivity
    - allow for querying of top level floorplans, current layout is messy (create a top level node below top level?)
1. Optimization
    - multithread the floorplanner (need deep hierarchies to play with - waiting on (0))
    - multithread the Lgraph traversal (need deep hierarchies to play with - waiting on (0))
       - create a way to floorplan using existing layouts
    - Check out the paper for ArchFP
    - improve implementation in ArchFP, fix todos, add better floorplan techniques
2. Incremental Floorplans (waiting on goal (2))
    - floorplan using existing geography hints instead of randomly choosing a hint
    - assign geography hints to nodes based on wirelength metrics
    - use HPWL as benchmark (waiting on goal (2) - need node hierarchy)
    - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
    - identify components that are far away, set geography hints to be closer?
3. More accurate floorplans
    - floorplan node pins - allows for more accurate HPWL estimation
    - scale area by bitwidth of node, if possible
    - implement blackboxes (waiting on goal (2))
4. add more checks to checkfp:
    - check for insane / unrealistic wirelength metrics
5. Create area-based floorplans
    - allows for efficient floorplan analysis, prevents disconnected segments entirely

Tabled:
 - Cairo is LGPL, so idk if we can ship it as part of LiveHD.
