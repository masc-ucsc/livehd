/-
# The FP route's step loop without the repeated state check and codec

`runMemSim` encodes the state, evaluates the residual, and decodes the result; a driver checks
`runtimeFullOKB` (the state, the input, the flop bound and the edges) before every step. Two
facts make most of that redundant from the second step on:

* Every route's correctness theorem (`runMemSim_correct`, `runMemSimF_correct`,
  `runMemSimV_correct`, `runMemSimW_correct`, ...) gives `stateFullOKB` of the state it returns.
  With it, `runtimeFullOKB` is its input, flop-bound and edge half (`runtimeFullOKB_after`),
  and that half depends only on the input's size, the flop vector's size and the edge vector's
  size (`edgesOKB_size`), so a driver computes it once per sizes.
* The codec is canonical (`canonical_stateM`): a result value that decodes holds, as its
  state part, the encoding of the next state itself (`result_state_part`). So a driver can hand
  that part to the next step (`runMemSimEnc`) instead of decoding it and encoding it again
  (`runMemSim_eq_enc`), and decode only the outputs and the flops it reports.

Nothing here changes a definition. Mathlib-free.
-/
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace MemRunFast

open Compiler HwMem MemProjection

/-! ## The state check, after a step -/

/-- Once the state half holds, `runtimeFullOKB` is its input, flop-bound and edge half. -/
theorem runtimeFullOKB_after {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (h : stateFullOKB D st = true) :
    runtimeFullOKB D i st e = (inputOKB D i.size && flopBoundOKB D st.flops.size && edgesOKB D e) := by
  unfold stateFullOKB at h
  simp only [Bool.and_eq_true] at h
  unfold runtimeFullOKB runtimeOKB
  simp [h.1, h.2]

/-- The edge check reads only the edge vector's size. -/
theorem edgesOKB_size (D : DesignCert) {e e' : ClockEdges} (h : e.size = e'.size) :
    edgesOKB D e = edgesOKB D e' := by
  unfold edgesOKB; rw [h]

/-! ## The codec is canonical -/

theorem field2_inv {tg : Nat} {v a b : Val} (h : field2 tg v = some (a, b)) : v = .ctor tg [a, b] := by
  unfold field2 at h
  split at h
  · rename_i t a' b'
    split at h
    · rename_i ht
      cases h; subst ht; rfl
    · cases h
  · cases h

theorem canonical_memImg : Canonical MemImg.encMemImg MemImg.decMemImg := by
  intro v m h
  unfold MemImg.decMemImg at h
  cases hf : field2 MemImg.tagMemImg v with
  | none => simp [hf] at h
  | some p =>
    obtain ⟨a, b⟩ := p
    simp only [hf, Option.bind_some] at h
    cases hw : decListG asBV a with
    | none => simp [hw] at h
    | some ws =>
      cases hd : asBV b with
      | none => simp [hw, hd] at h
      | some d =>
        simp only [hw, hd, Option.bind_some, Option.map_some, Option.some.injEq] at h
        subst h
        rw [field2_inv hf]
        have h1 : a = encListG ofBV ws := canonical_listG (e := ofBV) (d := asBV) canonical_BV hw
        have h2 : b = ofBV d := canonical_BV hd
        simp [MemImg.encMemImg, h1, h2]

theorem canonical_stateM : Canonical StateM.encStateM StateM.decStateM := by
  intro v st h
  unfold StateM.decStateM at h
  cases hf : field2 StateM.tagStateM v with
  | none => simp [hf] at h
  | some p =>
    obtain ⟨a, b⟩ := p
    simp only [hf, Option.bind_some] at h
    cases hfs : decBVs a with
    | none => simp [hfs] at h
    | some fs =>
      cases hms : decArr MemImg.decMemImg b with
      | none => simp [hfs, hms] at h
      | some ms =>
        simp only [hfs, hms, Option.bind_some, Option.map_some, Option.some.injEq] at h
        subst h
        rw [field2_inv hf]
        have h1 : a = encBVs fs := canonical_BVs hfs
        have h2 : b = encArr MemImg.encMemImg ms := canonical_arr canonical_memImg hms
        simp [StateM.encStateM, h1, h2]

/-- A result value that decodes holds the encoding of its next state, and the encoding of its
outputs. -/
theorem result_state_part {v : Val} {r : StateM.ResultM} (h : StateM.decResultM v = some r) :
    ∃ op, v = .ctor StateM.tagResultM [StateM.encStateM r.nextState, op] ∧ decBVs op = some r.outputs := by
  unfold StateM.decResultM at h
  cases hf : field2 StateM.tagResultM v with
  | none => simp [hf] at h
  | some p =>
    obtain ⟨a, b⟩ := p
    simp only [hf, Option.bind_some] at h
    cases hs : StateM.decStateM a with
    | none => simp [hs] at h
    | some st =>
      cases ho : decBVs b with
      | none => simp [hs, ho] at h
      | some os =>
        simp only [hs, ho, Option.bind_some, Option.map_some, Option.some.injEq] at h
        subst h
        exact ⟨b, by rw [field2_inv hf, canonical_stateM hs], ho⟩

/-! ## The step on an encoded state -/

/-- `runMemSim`'s evaluation, on a state already encoded. -/
def runMemSimEnc (sim : MemSimulator) (e : ClockEdges) (i : RuntimeInput) (sv : Val) : Option Val :=
  match evalFuel sim.bound sim.prog []
      (.call sim.prog.entry [.lit (encEdges e), .lit (encInput i), .lit sv]) with
  | .value v => some v
  | _ => none

theorem runMemSim_eq_enc (sim : MemSimulator) (e : ClockEdges) (i : RuntimeInput) (st : StateM) :
    runMemSim sim e i st = (runMemSimEnc sim e i (StateM.encStateM st)).bind StateM.decResultM := by
  unfold runMemSim runMemSimEnc
  cases evalFuel sim.bound sim.prog []
      (.call sim.prog.entry [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)]) <;> rfl

/-- The state part and the output part of a result value. -/
def resultParts (v : Val) : Option (Val × Val) := field2 StateM.tagResultM v

/-- When a step's result decodes to `r`, its state part is `encStateM r.nextState`: the next
step may run on it directly. -/
theorem next_from_parts {sim : MemSimulator} {e : ClockEdges} {i : RuntimeInput} {st : StateM}
    {v sp op : Val} {r : StateM.ResultM}
    (hv : runMemSimEnc sim e i (StateM.encStateM st) = some v) (hp : resultParts v = some (sp, op))
    (hr : runMemSim sim e i st = some r) :
    sp = StateM.encStateM r.nextState ∧ decBVs op = some r.outputs := by
  rw [runMemSim_eq_enc, hv, Option.bind_some] at hr
  obtain ⟨op', hv', ho⟩ := result_state_part hr
  rw [hv'] at hp
  unfold resultParts field2 at hp
  simp only [↓reduceIte, Option.some.injEq, Prod.mk.injEq] at hp
  obtain ⟨h1, h2⟩ := hp
  subst h1; subst h2
  exact ⟨rfl, ho⟩

#audit_axioms Projection.MemRunFast.runtimeFullOKB_after
#audit_axioms Projection.MemRunFast.canonical_stateM
#audit_axioms Projection.MemRunFast.next_from_parts

end MemRunFast
end Projection
