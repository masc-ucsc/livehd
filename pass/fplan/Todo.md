Issues:
 - HardAspectRatio tries to satisfy the requested aspect ratio as well as possible, even if it's not possible without incorrect layouts.
    - Fixing aspect ratio issues automatically is hard.  Checking if components collide with each other is $$ and figuring out where they should go is difficult.

Issues not related to ArchFP:
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo

Ask about:
 - LiveHD does not function correctly with non-english names (rename broken, importing broken as well (non-ascii characters skipped?))

Goals:

1. Check on how nextpnr gets hints, if it gets hints at all.  Can we write hints for nextpnr to consume?
2. Test BOOM core
3. Write node level hierarchy to file (mmap_tree not being written to file is known TODO)
4. Find / write a method that doesn't mess up on the wrong aspect ratio - HardAspectRatio not helpful for initial floorplans.

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
    - improve implementation in ArchFP, resolve todos
       - double -> float?
    - write a slicing floorplanner (HardAspectRatio quality goes down significantly with larger floorplans to the point of being unusable)
       - seperate pass from geogLayout - need to make Lhd_floorplanner a template beforehand
       - http://eda.ee.ucla.edu/EE201A-04Spring/polish.pdf
       - https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.12.3375&rep=rep1&type=pdf
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
 - Cairo is LGPL, so it can't be shipped with LiveHD.  Python script is fine.
 - Lrand random class doesn't support floats, not using it for writearea pass
