/-
  Fast model bridge theorem templates.
  Port of translation-correctness/Translation_Step.thy.

  The generated step is correct once generated combinational outputs and
  generated next-state are each shown equivalent to their mathematical LGraph
  certificate counterparts.
-/

import LeanSemanticPrimitives.Translation.LGraphModel

--------------------------------------------------------------------------------
-- Generic step definitions that per-design theories instantiate
--------------------------------------------------------------------------------

def generated_step {i s o : Type} (gen_next : i → s → s) (gen_comb : i → s → o)
    (inp : i) (st : s) : s × o :=
  (gen_next inp st, gen_comb inp st)

def lgraph_step {i s o : Type} (spec_next : i → s → s) (spec_comb : i → s → o)
    (inp : i) (st : s) : s × o :=
  (spec_next inp st, spec_comb inp st)

--------------------------------------------------------------------------------
-- The bridge: if generated next = spec next and generated comb = spec comb,
-- then the steps are equal.
--------------------------------------------------------------------------------

theorem generated_step_equals_lgraph_step {i s o : Type}
    (gen_next spec_next : i → s → s)
    (gen_comb spec_comb : i → s → o)
    (next_correct : ∀ inp st, gen_next inp st = spec_next inp st)
    (comb_correct : ∀ inp st, gen_comb inp st = spec_comb inp st)
    (inp : i) (st : s) :
    generated_step gen_next gen_comb inp st =
    lgraph_step spec_next spec_comb inp st := by
  simp [generated_step, lgraph_step, next_correct, comb_correct]
