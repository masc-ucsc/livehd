/-
# One clock-edge admission rule for every FP driver

Three admission rules for the per-step clock-edge vector grew up independently,
each the minimum its own proof needed:

  * the scalar route, `Hw.runtimeOK` (`ProjectedStep.lean:50`):
      `D.clocks.size ≤ e.size` -- every declared clock has an entry, extras ignored;
  * IR-as-semantics, `Direct.checkRuntime` (`DirectCheck.lean`):
      `e.size = D.clocks.size` -- exact;
  * the memory route, `HwMem.edgesOKB` (`HardwareMemRuntime.lean:86`):
      every clock ordinal a flop or memory USES is `< e.size`.

They are not rules for different designs.  The reference semantics reads an
edge with `fires e c = e[c]?.getD false` (`Runtime.lean`): an ordinal the vector
does not carry reads as "does not fire", so a short vector silently freezes that
clock domain.  Each rule rules that out for the slots its own proof reads.

The DRIVER rule (`RunAdmission.edgesExactOK`) is therefore the strictest, exact
match, applied after the
certificate's well-formedness check, and `edges_exact_sound` proves it implies
all three premises at once.  No existing theorem changes: each still states its
own (weaker) premise; this lemma discharges all of them from one check.
-/

import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Compiler.RunAdmission
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace EdgePolicy

open Compiler Compiler.RunAdmission

-- The rule itself, `edgesExactOK`, lives in `Compiler/RunAdmission.lean` so
-- the drivers can apply it without importing the memory route; this module
-- proves what it buys.

/-- What `certWfError D = none` says about clocks: at least one is declared, and
every flop and every memory commits on a declared one. -/
theorem certWf_clocks {D : DesignCert} (h : CertIO.certWfError D = none) :
    0 < D.clocks.size ∧
    (∀ f ∈ D.flops, f.clock < D.clocks.size) ∧
    (∀ m ∈ D.memories, m.clock < D.clocks.size) := by
  unfold CertIO.certWfError at h
  simp only at h
  split at h
  · simp at h
  · rename_i hnz
    split at h
    · simp at h
    · rename_i hf
      split at h
      · simp at h
      · rename_i hm
        refine ⟨?_, ?_, ?_⟩
        · have : D.clocks.size ≠ 0 := by simpa using hnz
          omega
        · intro f hmem
          have := (Array.findIdx?_eq_none_iff.mp hf) f hmem
          simp at this
          omega
        · intro m hmem
          have := (Array.findIdx?_eq_none_iff.mp hm) m hmem
          simp at this
          omega

/-- **The bridge.** On a well-formed certificate, the exact-match driver rule
implies the edge premise of every route:

  1. the scalar route's `runtimeOK` conjunct `D.clocks.size ≤ e.size`,
  2. `Direct.RuntimeSemWF.edgesSized : e.size = D.clocks.size`,
  3. the memory route's `edgesOKB D e = true`. -/
theorem edges_exact_sound {D : DesignCert} {e : ClockEdges}
    (hwf : CertIO.certWfError D = none) (hex : edgesExactOK D e = true) :
    D.clocks.size ≤ e.size ∧ e.size = D.clocks.size ∧ HwMem.edgesOKB D e = true := by
  have he : e.size = D.clocks.size := by simpa [edgesExactOK] using hex
  obtain ⟨_, hf, hm⟩ := certWf_clocks hwf
  refine ⟨by omega, he, ?_⟩
  unfold HwMem.edgesOKB
  simp only [Bool.and_eq_true, List.all_eq_true, decide_eq_true_eq]
  refine ⟨fun f hfm => ?_, fun m hmm => ?_⟩
  · rw [he]; exact hf f (Array.mem_toList_iff.mp hfm)
  · rw [he]; exact hm m (Array.mem_toList_iff.mp hmm)

/-! ## Controls -/

private def twoClk : DesignCert where
  sources := #[SourceDesc.input 0 1]
  nodes := #[]
  outputs := #[⟨0, 1⟩]
  flops := #[{ width := 1, din := 0, enable := none, resetPin := none,
               resetValue := 0, resetActiveLow := false, clock := 1 }]
  memories := #[]
  clocks := #[⟨"a"⟩, ⟨"b"⟩]

#guard CertIO.certWfError twoClk == none
#guard edgesExactOK twoClk #[true, false] == true
#guard edgesExactOK twoClk #[true] == false               -- short: would freeze clock 1
#guard edgesExactOK twoClk #[true, false, true] == false  -- extra entry: refused at the driver
#guard HwMem.edgesOKB twoClk #[true, true] == true

#audit_axioms certWf_clocks
#audit_axioms edges_exact_sound

end EdgePolicy
end Projection
