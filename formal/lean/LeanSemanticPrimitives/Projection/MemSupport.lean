/-
# The memory route's checked support answer, and its actual entry

`CertLoad.supportReport` is the production support/readiness report, and it is
**untouched**.  Its `memFree` conjunct is a refusal: a memory-bearing design is
unsupported BY THAT ROUTE, and `runOne` refuses it with code 3.  That stays
exactly as it is, because the scalar route's predicates
(`SourceSupported`, `OpSupported`, `SupportedByProjection`) are what the scalar
adequacy theorems are stated about, and broadening them would subtract meaning
from those theorems.

This module states the ALTERNATIVE answer, beside it: a memory-bearing design
is not simply "unsupported" — it may be **admitted by the checked memory
route**, and that answer comes with the entry, the codecs and the adequacy the
scalar answer comes with.  The two reports are deliberately separate, so a
refusal still says WHICH route refused and why.

## What this module does NOT do

It does not modify `supportReport`, `SupportReport.allOK`, `runOne`, or any
scalar predicate; `report` CARRIES the scalar verdict unchanged so the two are
comparable.  It gives no whole-unit admission credit: a design passing `report`
is a design the memory route's own theorems apply to, nothing more.
-/
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Projection.HardwareMemFixture
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace MemSupport

open Compiler Projection.HwMem

/-! ## 1. The report

Five fields, reported separately for the same reason the scalar report has six:
they are different answers.  `scalar`/`memFree` say what the ORIGINAL route
answers and why; `accepts`, `sized` and `nextImgs` are the three components of
the memory admission check, so a refusal names the one that failed. -/

structure Report where
  /-- the scalar route's verdict, carried unchanged -/
  scalar   : Bool
  /-- …and the conjunct that decides it for a memory-bearing design -/
  memFree  : Bool
  /-- the retained `Direct.checkDesign` acceptance -/
  accepts  : Bool
  /-- the slot-type inference succeeded at all -/
  typed    : Bool
  /-- …and typed every slot -/
  sized    : Bool
  /-- every memory's next image types as THAT memory's table -/
  nextImgs : Bool
deriving Repr

def report (D : DesignCert) : Report where
  scalar   := (CertLoad.supportReport D).allOK
  memFree  := D.memories.size == 0
  accepts  := Direct.acceptsDesign D
  typed    := (inferTys D).isSome
  sized    := match inferTys D with
              | some ts => ts.size == D.numSlots
              | none    => false
  nextImgs := match inferTys D with
              | some ts => D.memories.toList.all (fun md =>
                  match ts[md.nextImg]? with
                  | some (.tbl aw dw) => aw == md.aw && dw == md.dw
                  | _                 => false)
              | none    => false

/-- The memory route's verdict. -/
def Report.admitOK (r : Report) : Bool := r.accepts && r.sized && r.nextImgs

/-- **The report is faithful**: its three admission fields ARE the check, not a
summary of it. -/
theorem admitOK_eq (D : DesignCert) : (report D).admitOK = admitsB D := by
  unfold Report.admitOK report admitsB
  cases hts : inferTys D with
  | none      => simp [hts]
  | some ts   =>
      simp only [hts, Bool.and_assoc]
      all_goals rfl

theorem report_admits {D : DesignCert} (h : (report D).admitOK = true) :
    admitsB D = true := by rw [← admitOK_eq]; exact h

/-- …and it does not quietly restate the scalar one. -/
theorem report_scalar (D : DesignCert) :
    (report D).scalar = (CertLoad.supportReport D).allOK := rfl

/-- A design with a memory is refused by the SCALAR route, necessarily — the
`memFree` conjunct is false.  Stated so the alternative answer cannot be
mistaken for a broadening of the original. -/
theorem scalar_refuses_memory {D : DesignCert} (h : 0 < D.memories.size) :
    (report D).scalar = false := by
  have hne : D.memories.size ≠ 0 := by omega
  have hmf : (CertLoad.supportReport D).memFree = false := by
    simp [CertLoad.supportReport, hne]
  simp [report, CertLoad.SupportReport.allOK, hmf]

/-! ## 2. The actual entry, and the actual codecs

The memory route's entry is `mainM`, a function ADDED to `hwS`; the scalar
`main` is still there, unedited, under its own name, and the two resolve to
different indices.  So "the memory route" is a different entry of the same
program, not a replacement of the scalar one. -/

theorem entry_name : hwMemS.entry = "mainM" := hwMemS_entry
theorem entry_index : idOf "mainM" = some 50 := mainM_id
theorem entry_distinct : idOf "main" ≠ idOf "mainM" := main_ne_mainM

/-- **The answer the report is worth.**  A design the memory route admits, run
on a state/input/edge vector the runtime policy accepts, evaluates at the ACTUAL
resolved entry to a value that DECODES through the real result codec, and the
decoded result is `Compiler.interpretDesign`'s — with the returned state passing
the same policy, so the next step needs no new state check. -/
theorem run_of_report {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges}
    (hrep : (report D).admitOK = true)
    (hR : runtimeFullOKB D i st e = true) :
    ∃ (v : Val) (r : StateM.ResultM),
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ∧ StateM.decResultM v = some r
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true := by
  obtain ⟨r, hev, hden, hok⟩ := admits_run_full (report_admits hrep) hR
  exact ⟨StateM.encResultM r, r, hev, StateM.decResultM_encResultM r, hden, hok⟩

/-- …and from the design's own canonical state, with no state argument at all.
`initStateOf` is a legal-state witness — zero mutable tables, reset-valued flops
— and NOT an RTL startup contract; a driver with real initial contents must pass
them in as `st` above. -/
theorem run_of_report_init {D : DesignCert} {i : RuntimeInput} {e : ClockEdges}
    (hrep : (report D).admitOK = true)
    (hin : inputOKB D i.size = true) (hed : edgesOKB D e = true) :
    ∃ (v : Val) (r : StateM.ResultM),
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i,
            StateM.encStateM (initStateOf D)].map Term.lit)) v
      ∧ StateM.decResultM v = some r
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote (initStateOf D))
      ∧ stateFullOKB D r.nextState = true := by
  obtain ⟨r, hev, hden, hok⟩ :=
    admits_run_full_init (report_admits hrep) hin hed
  exact ⟨StateM.encResultM r, r, hev, StateM.decResultM_encResultM r, hden, hok⟩

/-! ## 3. Readiness lines

Pure, so they are testable; the `IO` wrapper matches `CertLoad.say`'s shape so a
driver can print them without any change to `runOne`'s own reporting. -/

def Report.line (r : Report) : String :=
  s!"  memroute: accepts {r.accepts}  typed {r.typed}  sized {r.sized} \
nextImgs {r.nextImgs}  -> admitted {r.admitOK}"

/-- The scalar fields that ACTUALLY failed, read off the scalar report itself.
A memory-bearing design fails `memFree`, but a design with a `memConst` SOURCE
and no declared memories has `memFree = true` and fails `sources` instead — so
the reason is reported, never inferred from the memory route's success. -/
def scalarFailures (D : DesignCert) : List String :=
  let r := CertLoad.supportReport D
  (if r.wf then [] else ["wf"])
    ++ (if r.memFree then [] else ["memFree"])
    ++ (if r.sources then [] else ["sources"])
    ++ (if r.ops then [] else ["ops"])
    ++ (if r.arities then [] else ["arities"])
    ++ (if r.flopClocks then [] else ["flopClocks"])

/-- When a field really is `false`, the report NAMES it — so the reason is read
off the scalar report, never inferred. -/
theorem memFree_mem_scalarFailures {D : DesignCert}
    (h : (CertLoad.supportReport D).memFree = false) :
    "memFree" ∈ scalarFailures D := by
  unfold scalarFailures; simp [h]

theorem sources_mem_scalarFailures {D : DesignCert}
    (h : (CertLoad.supportReport D).sources = false) :
    "sources" ∈ scalarFailures D := by
  unfold scalarFailures; simp [h]

/-- …and when the scalar route refuses, it names at least one. -/
theorem scalarFailures_ne_nil {D : DesignCert}
    (h : (CertLoad.supportReport D).allOK = false) : scalarFailures D ≠ [] := by
  unfold scalarFailures
  intro hnil
  apply absurd h
  simp only [CertLoad.SupportReport.allOK, ne_eq, Bool.not_eq_false]
  simp only [List.append_eq_nil_iff] at hnil
  obtain ⟨⟨⟨⟨⟨h1, h2⟩, h3⟩, h4⟩, h5⟩, h6⟩ := hnil
  have g : ∀ (b : Bool) (n : String), (if b then ([] : List String) else [n]) = [] → b = true := by
    intro b n hb; cases b with | false => simp at hb | true => rfl
  rw [g _ _ h1, g _ _ h2, g _ _ h3, g _ _ h4, g _ _ h5, g _ _ h6]
  rfl

def verdict (D : DesignCert) : String :=
  let r := report D
  if r.scalar then "SCALAR ROUTE: supported"
  else
    let f := String.intercalate "," (scalarFailures D)
    if r.admitOK then s!"SCALAR ROUTE: refused ({f}) -- MEMORY ROUTE: admitted"
    else s!"REFUSED BY BOTH ROUTES (scalar: {f})"

def sayReport (D : DesignCert) : IO Unit := do
  CertLoad.say (report D).line
  CertLoad.say s!"  verdict: {verdict D}"

/-! ## 4. Executed

A memory-bearing design that the scalar route necessarily refuses and the memory
route admits; the `byte_w = 8` lane design beside the `byte_w = 1` one; and a
memory-FREE design, which the scalar route still supports — the original answer
is unchanged where it was already right. -/

private def scalarDesign : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8]
  , nodes    := #[{ op := .Op_And, width := 8, deps := #[0, 1] }]
  , outputs  := #[{ slot := 2, width := 8 }]
  , flops    := #[]
  , memories := #[]
  , clocks   := #[{ name := "clk" }] }

#guard (report fullDesign).memFree == false
#guard (report fullDesign).scalar == false
#guard (report fullDesign).admitOK == true
#guard (report mixedDesign).admitOK == true
#guard (report twoMemDesign).admitOK == true
#guard (report laneDesign).admitOK == true

#guard (report scalarDesign).memFree == true
#guard (report scalarDesign).scalar == true
#guard (report scalarDesign).admitOK == true

#guard (scalarFailures fullDesign).contains "memFree"
#guard verdict scalarDesign == "SCALAR ROUTE: supported"
#guard scalarFailures scalarDesign == []

/- a design the MEMORY route refuses too: the write target is a ROM -/
#guard (verdict { mixedDesign with
                  sources := #[ .memConst 5 32 #[1, 2, 3], .input 0 5, .input 1 32
                              , .input 2 1, .input 3 32 ] }).startsWith
       "REFUSED BY BOTH ROUTES"

/- the control the audit asked for: a ROM-only design has NO declared memories,
   so `memFree` is TRUE and the scalar refusal is `sources` -- the verdict must
   not infer the reason from the memory route's success -/
private def romOnlyDesign : DesignCert :=
  { sources  := #[.memConst 2 32 #[7, 8, 9], .input 0 2, .input 1 1]
  , nodes    := #[{ op := .Op_MemRead, width := 32, deps := #[0, 1, 2] }]
  , outputs  := #[{ slot := 3, width := 32 }]
  , flops    := #[]
  , memories := #[]
  , clocks   := #[{ name := "clk" }] }

#guard (report romOnlyDesign).memFree == true
#guard (report romOnlyDesign).scalar == false
#guard (report romOnlyDesign).admitOK == true
/- the refusal is `sources`, and `memFree` is NOT named -/
#guard (scalarFailures romOnlyDesign).contains "sources"
#guard !((scalarFailures romOnlyDesign).contains "memFree")
/- …so the verdict can no longer be the one the audit caught -/
#guard verdict romOnlyDesign != "SCALAR ROUTE: refused (memFree) -- MEMORY ROUTE: admitted"

#audit_axioms memFree_mem_scalarFailures
#audit_axioms sources_mem_scalarFailures
#audit_axioms scalarFailures_ne_nil
#audit_axioms admitOK_eq
#audit_axioms scalar_refuses_memory
#audit_axioms run_of_report
#audit_axioms run_of_report_init

end MemSupport
end Projection
