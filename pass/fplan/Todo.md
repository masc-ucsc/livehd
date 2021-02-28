Plan:
1. Find DeFer src/license, check
2. Possibly adjust stuff so DeFer and ArchFP can both operate on the same node hierarchy (and so that different floorplanners can operate on different segments)


lgraph.list - lists lgraphs!


1. Write/pull in a floorplanning method that actually works properly into ArchFP

    General idea:
    - if someone wants to manually provide hints to a module, use geogLayout for it (since it is assumed that the person knows what they're doing)
    - otherwise, use some other method to generate an initial floorplan (run DeFer per module, cache the result?)
    - this initial floorplan is used to generate hints for the placer (it would be really cool if the floorplanner itself used some of the same hints!)

    - write/pull another floorplanner, and make sure it generates legal floorplans automatically.
       - research what an analytical placer actually does, because this will tell us how good our floorplanner needs to be.
       - Chris Chu writes good tools/papers (code has weird license issues) - wrote DeFer, could use that.  Pretty fast.
       - http://eda.ee.ucla.edu/EE201A-04Spring/polish.pdf: original simulated annealing floorplanner
2. Create method of writing and reading geographic hints manually
    - implement hint storage
3. Work on creating methods for automatic hint insertion
    - better hints (place near here, put in this section, block divide design around here, put anchor points here, etc)
       - goal of the whole floorplanner project is to provide hints to placer!
       - anchor points are exact, but also want to be able to have hard lines between sections of a chip
       - give hints for anchor points!
       - adjust floorplan unit size with better granularity?

Possible Improvements:
1. Optimization
    - multithread the floorplanner (how to decide what layout() calls get spun off into threads?)
    - multithread the Lgraph traversal
       - create a way to floorplan using existing layouts
    - double -> float in ArchFP?  We use float everywhere else...

2. Incremental Floorplans
    - floorplan using existing geography hints/specific AR instead of randomly choosing a hint/using AR = 1.0
    - assign geography hints to nodes based on wirelength metrics
    - use HPWL as benchmark
    - easy: swap positions of leaves of the same type (within a hierarchy), see if HPWL gets better
    - identify components that are far away, set geography hints to be closer?
3. Misc
    - implement blackboxes

Issues:
 - view.py output is flipped due to mismatch between coordinates for HotSpot and png coordinates in PyCairo
 - LiveHD does not function correctly with non-english names (rename broken, importing broken as well (non-ascii characters skipped?))

Tabled:
 - Cairo is LGPL, so it can't be shipped with LiveHD.  Python script is fine.
 - Lrand random class doesn't support floats, not using it for writearea pass
 - floorplaning node pins allows for more accurate HPWL estimation, but doing sub node-level floorplans isn't a job for LiveHD
