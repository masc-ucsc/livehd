
# Select between inou and pass.

* inou: reads from external (non-LGraph) to create an LGraph, or exports external from a LGraph.
Examples, inou/json

* pass: optimizes or regenerates (regen) a LGraph, or generated a new set of LGraphs from a given LGraph (trans).
E.g: dead-code-elimination (dce) does a regen. A flattening of the LGraph pass (flatten?) uses the trans

Use one of the sample passes as starting point (inou/rand or pass/dce) and
 make sure to use the Options_base and inherit from Inou or Pass

- Add the commands to main/LGraph

 + Add to the main/BUILD

 + Add the inou_foo_api.hpp or pass_foo_api.hpp to main/main_api.cpp

 + Copy and edit, inou_rand_api.hpp to inou_foo_api.hpp or pass_abc_api.hpp to pass_foo_api.hpp

## Common variables

 One of the main goals is to have a uniform set of passes in lgshell. lgshell should use this common
variable names when possible

    name:foo        lgraph name
    path:lgdb       lgraph database path (lgdb)
    files:foo,var   comma separated list of files used for INPUT
    odir:.          output directory to generate files like verilog/pyrope...
