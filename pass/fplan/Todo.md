Tests:
 - boom core(s)
 - clocked logic
 - 500K XOR table
 - memory

 Improvements:
 - LGraph attributes
   - Node attributes and Node pin attributes already exist (attribute.hpp)
   - Is there a way to write attributes to a file?
 - Add aspect ratio information to LGraphs
 - Add placement info to LGraphs
 - Can we multithread ArchFP?
 - Check out the paper for ArchFP - see what can be improved on implementation

Style guide refactors:
 - make sure all class/type names are uppercase
 - string ops -> abseil versions
 - getters and setters?
 - replace geogLayout new/delete used with archfp to some sort of smart pointer
   - want the lifetime of the layout to be for all of pass_fplan...

Long term:
 - some sort of check() method that verifies netlists and internal functionality
 - a way to call BloBB or CompaSS directly, since they're really fast (faster than ArchFP? Need to test this...)
 - floorplan at node level
 - floorplan taking into account node type (ArchFP has internals for this, I think)

Easy:
 - add license stuff (incl lg_attribute.hpp)
 - update usage doc (http://lava.cs.virginia.edu/archfp/index.htm)
 - have bazel clone archfp from github

Tried:
 - namespaces: none of the other passes are namespaced so whatever
