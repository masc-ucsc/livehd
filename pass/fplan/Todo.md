Issues:
 - HardAspectRatio tries to satisfy the requested aspect ratio as well as possible, even if it's not possible without incorrect layouts.
    - Fixing aspect ratio issues automatically is hard.  Checking if components collide with each other is $$ and figuring out where they should go is difficult.

Issues not related to ArchFP:
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo

Ask about:
 - reason why I need mmap_tree:
 1. HighReg depends very heavily on explicit hierarchy trees to function, and I don't think it's the only one.  Leaves open the possibility of more kinds of passes.
 2. When working with nodes (which is going to be a lot when more analysis passes get added and connections are taken into account), I need to be able to easily move between implementation information and hierarchy information (which module instance is this node a part of?).  Currently there is no easy way to get this information.
 3. Sometimes node attributes aren't hierarchical when I want them to be, but sacrificing speed / what everyone else needs for a single floorplanner doesn't make a ton of sense.

 - put fplan tests into yosys tests since they crash livehd fairly often
 - Dynamic loading of cairo lib...?
 - Node names aren't hierarchical, can they be?

 - LiveHD does not function correctly with non-english names (rename broken, importing broken as well (non-ascii characters skipped?))

Goals:
2. Test hier_lg with hier_test.v (waiting on yosys bugfix)
3. Test BOOM core (waiting on yosys memory implementation?)
4. Write node level hierarchy to file (mmap_tree not being written to file is known TODO)
5. Find / write a method that doesn't mess up on the wrong aspect ratio - HardAspectRatio not helpful for initial floorplans.

Performance (using -c opt):
    xor_30000.v -> file: 38 ms
    xor_30000.v -> livehd: 16 ms

    xor_100000.v -> file: 131 ms
    xor_100000.v -> livehd: 46 ms

Things to add:
0. Interactivity
    - allow for querying of top level floorplans, current layout is messy (create a top level node below top level?)
    - add a recursive dump option
    - dump everything if no nodes are passed
    - Node names are not hierarchical, so I can't query information about a specific Or node...
1. Optimization
    - multithread the floorplanner (need deep hierarchies to play with - waiting on (0))
    - multithread the Lgraph traversal (need deep hierarchies to play with - waiting on (0))
       - create a way to floorplan using existing layouts
    - Check out the paper for ArchFP
    - improve implementation in ArchFP, resolve todos, add better floorplan techniques
       - double -> float?
2. Incremental Floorplans (waiting on goal (2))
    - floorplan using existing geography hints/specific AR instead of randomly choosing a hint/using AR = 1.0
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
 - Cairo is LGPL, so it can't be shipped with LiveHD.
 - Lrand random class doesn't support floats, not using it for writearea pass
