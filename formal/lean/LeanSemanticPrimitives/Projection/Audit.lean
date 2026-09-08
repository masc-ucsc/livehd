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
import LeanSemanticPrimitives.Projection.BTA
import LeanSemanticPrimitives.Projection.PartialEvaluatorCorrect

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
#print axioms bta_sound
#print axioms bta_erases
#print axioms erase_annT
#print axioms eraseProgram_fn
#print axioms findAlt_eraseAlts
#print axioms Compat_shift
#print axioms Compat_fields_dyn
#print axioms Compat_fields_stat
#print axioms Compat_stat_lookup
#print axioms Compat_dyn_lookup
#print axioms buildEnv_Compat
#print axioms mixTerm_complete
#print axioms mixAlts_ok
#print axioms mixUArgs_ok
#print axioms mixTerms_ok
#print axioms specOK_all
#print axioms mixDriver_sound
#print axioms generateFrom_spec
#print axioms Val.eq_of_beq
#print axioms indexOfReq_spec
#print axioms wrapLets_eval
#print axioms allStatic_forall₂
#print axioms toCode_forall₂
#print axioms Compat_allStat

end Projection
