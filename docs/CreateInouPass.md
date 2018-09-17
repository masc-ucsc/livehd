
-Select between inou and pass.

 inou: reads from external (non-lgraph) to create an lgraph, or exports external from a lgraph.
Examples, inou/json

 pass: optimizes or regenerates (regen) a lgraph, or generated a new set of lgraphs from a given lgraph (trans).
E.g: dead-code-elimination (dce) does a regen. A flattening of the lgraph pass (flatten?) uses the trans

-Use one of the sample passes as starting point (inou/rand or pass/dce)

 make sure to use the Options_base and inherit from Inou or Pass

-Add the commands to main/lgraph

 +Add to the main/BUILD 

 +Add the inou_foo_api.hpp or pass_foo_api.hpp to main/main_api.cpp

 +Copy and edit, inou_rand_api.hpp to inou_foo_api.hpp or pass_abc_api.hpp to pass_foo_api.hpp

