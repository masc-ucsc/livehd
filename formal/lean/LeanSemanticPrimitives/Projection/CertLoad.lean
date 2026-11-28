/-
# Loading a real certificate, and running it both ways

Phase 6 infrastructure.  FP's ported DCERT1 loader is `Compiler/CertIOV1.lean`
(namespace `Compiler.CertIO.V1`; on master the unqualified `Compiler.CertIO`
is DCERT4, read by `Projection/CertLoadAny.lean`); this file is what uses it.
FP HISTORY: no driver reads DCERT1 (each admits DCERT4 only); the checks below
stay as the record of FP's codec:

  * round-trip and malformed-input regressions for the TRUSTED parser;
  * a per-field support report -- all six `SupportedByProjection` conjuncts
    reported SEPARATELY, because "supported" is not one fact and a failure in
    one field says something quite different from a failure in another;
  * the small IO helpers (`peakRSS`, `ms`, `say`) the fp tools print with.

The command-line runner that ran `mkSim`'s simulator against `interpretDesign` on one
certificate (`cert_runner`, `run_cert.sh`) was removed on 2026-11-27: nothing called it, and
`dcert2_memroute` checks every design it could, cycle by cycle against the reference.

The theorem boundary does not move.  `stepOf_correct` is still conditional on
`SimWF`, which contains `projectDesign D = .ok R` -- a KERNEL equation that the
specializer does not reduce.  Agreement observed on a run is EXECUTION EVIDENCE about one file, not a
proof about it, and nothing below claims otherwise.
-/

import LeanSemanticPrimitives.Projection.ProjectedStep
import LeanSemanticPrimitives.Compiler.CertIOV1

namespace Projection
namespace CertLoad

open Compiler Compiler.CertIO.V1 Projection.Hw

/-! ## The six support fields, decided separately -/

def slotsInRangeB (D : DesignCert) : Bool :=
  D.outputs.toList.all (fun o => decide (o.slot < D.numSlots)) &&
  D.flops.toList.all (fun f => decide (f.din < D.numSlots)) &&
  D.flops.toList.all (fun f =>
    match f.enable with | none => true | some e => decide (e < D.numSlots)) &&
  D.flops.toList.all (fun f =>
    match f.resetPin with | none => true | some r => decide (r < D.numSlots)) &&
  D.memories.toList.all (fun m => decide (m.nextImg < D.numSlots))

theorem slotsInRange_of_bool {D : DesignCert} (h : slotsInRangeB D = true) :
    DesignCert.SlotsInRange D := by
  simp only [slotsInRangeB, Bool.and_eq_true, List.all_eq_true] at h
  obtain ⟨⟨⟨⟨ho, hd⟩, he⟩, hr⟩, hm⟩ := h
  refine ⟨fun o hoo => of_decide_eq_true (ho o hoo), fun f hff => ⟨?_, ?_, ?_⟩,
          fun m hmm => of_decide_eq_true (hm m hmm)⟩
  · exact of_decide_eq_true (hd f hff)
  · intro e0 h0; have := he f hff; rw [h0] at this; exact of_decide_eq_true this
  · intro r0 h0; have := hr f hff; rw [h0] at this; exact of_decide_eq_true this

/-- All six conjuncts, as a record of `Bool`s.  Reported separately on purpose:
an `ops` failure means the design uses an operator `I_hw` lacks, an `arities`
failure means an operator is given the wrong operand count, and a `memFree`
failure means Phase 7 -- three quite different answers. -/
structure SupportReport where
  wf         : Bool
  memFree    : Bool
  sources    : Bool
  ops        : Bool
  arities    : Bool
  flopClocks : Bool
deriving Repr

def supportReport (D : DesignCert) : SupportReport where
  wf         := DesignCert.depsBoundedB D && slotsInRangeB D
  memFree    := D.memories.size == 0
  sources    := D.sources.toList.all (fun sd => SourceSupported sd)
  ops        := D.nodes.toList.all (fun c => OpSupported c.op)
  arities    := D.nodes.toList.all (fun c => decide (ArityOK c.op c.deps.toList))
  flopClocks := D.flops.toList.all (fun f => decide (f.clock < D.clocks.size))

def SupportReport.allOK (r : SupportReport) : Bool :=
  r.wf && r.memFree && r.sources && r.ops && r.arities && r.flopClocks

/-- …and the report is FAITHFUL: all six true really does give the predicate.
Not used by the runner -- a run cannot produce a proof term -- but without it
the report would be six booleans with no stated meaning. -/
theorem supported_of_report {D : DesignCert} (h : (supportReport D).allOK = true) :
    SupportedByProjection D := by
  simp only [SupportReport.allOK, supportReport, Bool.and_eq_true, beq_iff_eq,
             List.all_eq_true] at h
  obtain ⟨⟨⟨⟨⟨⟨hdb, hsl⟩, hmf⟩, hsr⟩, hop⟩, har⟩, hck⟩ := h
  exact
    { wf := ⟨DesignCert.depsBounded_of_bool D hdb, slotsInRange_of_bool hsl⟩
      memFree := Array.eq_empty_of_size_eq_zero hmf
      sources := hsr
      ops := hop
      arities := fun c hc => of_decide_eq_true (har c hc)
      flopClocks := fun f hf => of_decide_eq_true (hck f (by simpa using hf)) }

/-! ## Residual shape, for the report -/

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b      => 1 + tsize a + tsize b
  | .ite a b c      => 1 + tsize a + tsize b + tsize c
  | .caseT s as     => 1 + tsize s + (as.map (fun a => tsize (Alt.body a))).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts =>
      1 + (ts.map tsize).foldl (·+·) 0

partial def tagsOf : Term → List Nat
  | .caseT s as => (as.map Alt.tag) ++ tagsOf s ++ (as.map (fun a => tagsOf (Alt.body a))).flatten
  | .lit _ | .var _ => []
  | .letIn a b => tagsOf a ++ tagsOf b
  | .ite a b c => tagsOf a ++ tagsOf b ++ tagsOf c
  | .prim _ ts | .ctorT _ ts | .call _ ts => (ts.map tagsOf).flatten

def tagsIn (R : Program) : List Nat :=
  ((R.funs.map (fun fd => tagsOf fd.body)).flatten).eraseDups

/-! ## Round trip and malformed input

The parser is trusted, so these are REGRESSIONS, not a verification of it. -/

/-- Field-wise, because `DesignCert` has no `DecidableEq` (`ClockDesc` aside,
the point is that `Repr`-level equality is not what matters here). -/
def certEq (a b : DesignCert) : Bool :=
  a.sources == b.sources && a.nodes == b.nodes && a.outputs == b.outputs &&
  a.flops == b.flops && a.memories == b.memories

def roundTrips (D : DesignCert) : Bool :=
  match parseCert (renderCert D) with
  | .ok D'   => certEq D D'
  | .error _ => false

#guard roundTrips Projection.Acceptance.tinyD
#guard roundTrips Projection.Acceptance.seqD

/-- A design exercising every record shape DCERT1 v1 can express: all six
source tags, a payload-carrying operator, both flop option fields present and
absent, and a memory. -/
def wideD : DesignCert where
  sources  := #[.input 0 4, .const 4 (-3), .flopQ 0 4,
                .flopQAsync 1 4 2 7 true, .memImg 0 2 4, .memConst 2 4 #[0, 1, 2, 3]]
  nodes    := #[{ op := .Op_Sum 1, width := 4, deps := #[0, 1], origin := 9 },
                { op := .Op_MuxBool, width := 4, deps := #[0, 1, 2] },
                { op := .Op_Not, width := 4, deps := #[6] }]
  outputs  := #[{ slot := 7, width := 4 }, { slot := 8, width := 2 }]
  flops    := #[{ width := 4, din := 6, enable := none, resetPin := none,
                  resetValue := 0, resetActiveLow := false },
                { width := 4, din := 7, enable := some 0, resetPin := some 1,
                  resetValue := 5, resetActiveLow := true }]
  memories := #[{ aw := 2, dw := 4, nextImg := 8 }]

#guard roundTrips wideD
-- …and a negative `const`, which is the one place `readInt` differs from `readNat`
#guard (match parseCert (renderCert wideD) with
        | .ok D'   => D'.sources[1]? == some (.const 4 (-3))
        | .error _ => false)

def rejects (s : String) : Bool := (parseCert s.toUTF8).toOption.isNone

-- malformed inputs, each failing for a DIFFERENT reason
#guard rejects ""                                   -- no magic
#guard rejects "DCERX1\n0 0 0 0 0\n"                -- wrong magic
#guard rejects "DCERT1\n"                           -- truncated: no counts
#guard rejects "DCERT1\n1\n0 0 4\n0\n0\n0\n"        -- count 1 source, missing nodes count
#guard rejects "DCERT1\n0\n1\n99 0 4 0 7\n0\n0\n0\n"   -- unknown opcode 99
#guard rejects "DCERT1\n0\n0\n0\n0\n0\n7\n"         -- trailing garbage
#guard rejects "DCERT1\n2\n0 0 4\n0\n0\n0\n0\n"     -- source count exceeds records
#guard rejects "DCERT1\n1\n9 0 4\n0\n0\n0\n0\n"     -- unknown source tag 9
-- …and the minimal WELL-formed file is accepted
#guard (parseCert "DCERT1\n0\n0\n0\n0\n0\n".toUTF8).toOption.isSome

/-! ## IO helpers -/

def peakRSS : IO String := do
  try
    let txt ← IO.FS.readFile "/proc/self/status"
    match (txt.splitOn "\n").find? (fun l => l.startsWith "VmHWM:") with
    | some l => pure (l.replace "VmHWM:" "")
    | none   => pure "n/a"
  catch _ => pure "n/a"

@[inline] def ms : IO Nat := do pure (← IO.monoMsNow)

def say (s : String) : IO Unit := IO.println s

end CertLoad
end Projection
