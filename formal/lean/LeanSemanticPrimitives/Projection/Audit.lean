/-
  Axiom audit for the Projection library.

  The same gate the manual path uses: every load-bearing theorem must rest on
  the three standard axioms and nothing else.  A `sorry` anywhere shows up here
  as `sorryAx`, and `native_decide` would show up as `Lean.ofReduceBool` -- this
  library must not use it, since these theorems are about all programs rather
  than about one fixed graph.

  This module is checked by CI shape, not by a human reading it: it is the last
  module the build script compiles, and it prints. -/

import LeanSemanticPrimitives.Projection.Encoding
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

namespace Projection

#print axioms evalFuel_sound
#print axioms evalFuelList_sound_of
#print axioms decTerm_encTerm
#print axioms decTerms_encTerms
#print axioms decAlts_encAlts
#print axioms decProgram_encProgram
#print axioms encTerm_decTerm
#print axioms encProgram_decProgram
#print axioms encEnv_decEnv
#print axioms encTerm_inj
#print axioms encProgram_inj

end Projection
