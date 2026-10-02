/-
# Loading a real certificate, and running it both ways

Phase 6 infrastructure.  `Compiler/CertIO.lean` is the ported DCERT1 loader;
this file is what uses it:

  * round-trip and malformed-input regressions for the TRUSTED parser;
  * a per-field support report -- all six `SupportedByProjection` conjuncts
    reported SEPARATELY, because "supported" is not one fact and a failure in
    one field says something quite different from a failure in another;
  * a command-line runner that loads one certificate, reports that, specializes
    ONCE with `mkSim`, and then runs the projected simulator and the direct
    `interpretDesign` on identical deterministic stimuli.

The theorem boundary does not move.  `stepOf_correct` is still conditional on
`SimWF`, which contains `projectDesign D = .ok R` -- a KERNEL equation that the
specializer does not reduce.  Agreement observed by the runner is EXECUTION
EVIDENCE about one file, not a proof about it, and nothing below claims
otherwise.
-/

import LeanSemanticPrimitives.Projection.ProjectedStep
import LeanSemanticPrimitives.Compiler.CertIO

namespace Projection
namespace CertLoad

open Compiler Compiler.CertIO Projection.Hw

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

/-! ## The runner -/

def peakRSS : IO String := do
  try
    let txt ← IO.FS.readFile "/proc/self/status"
    match (txt.splitOn "\n").find? (fun l => l.startsWith "VmHWM:") with
    | some l => pure (l.replace "VmHWM:" "")
    | none   => pure "n/a"
  catch _ => pure "n/a"

@[inline] def ms : IO Nat := do pure (← IO.monoMsNow)

def say (s : String) : IO Unit := IO.println s

/-- Load one certificate, report it, specialize once, run both paths on the
same deterministic stimuli, and compare. -/
def runOne (path : String) (cycles : Nat) (seed : Nat) : IO UInt32 := do
  say s!"=== {path}"
  -- 1. parse
  let t0 ← ms
  let bytes ← IO.FS.readBinFile path
  let t1 ← ms
  let D ← match parseCert bytes with
          | .ok D    => pure D
          | .error e => do say s!"  PARSE FAILED: {e}"; return 1
  let t2 ← ms
  say s!"  bytes {bytes.size}  read {t1 - t0} ms  parse {t2 - t1} ms"
  say s!"  sources {D.sources.size}  nodes {D.nodes.size}  outputs {D.outputs.size} \
flops {D.flops.size}  memories {D.memories.size}  clocks {D.clocks.size}"
  -- 2. support, field by field
  let t3 ← ms
  let r := supportReport D
  let t4 ← ms
  say s!"  support: wf {r.wf}  memFree {r.memFree}  sources {r.sources}  ops {r.ops} \
arities {r.arities}  flopClocks {r.flopClocks}   ({t4 - t3} ms)"
  say s!"  asyncOK (DCERT1 v1 cannot carry FlopDesc.asyncReset): {asyncOK D}"
  if !asyncOK D then
    say "  REFUSED: the file has flopQAsync sources but v1 gives every flop \
asyncReset = false, so this would simulate a DIFFERENT design."
    return 2
  if !r.allOK then
    say "  REFUSED: see the per-field report above."
    return 3
  -- 3. specialize once
  let t5 ← ms
  let sim ← match mkSim D with
            | some sim => pure sim
            | none     => do
                say "  SPECIALIZATION or FRAGMENT CHECK FAILED"
                return 4
  let t6 ← ms
  say s!"  specialize+check {t6 - t5} ms   residual funs {sim.prog.funs.length} \
size {(sim.prog.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0}  bound {sim.bound}"
  say s!"  surviving caseT tags {tagsIn sim.prog}   (tagState = {tagState})"
  -- 4. one cycle, both paths
  let (inp, st) := stimulus D seed
  let e := allEdges D
  let t7 ← ms
  let projected := stepOf sim D e inp st
  let t8 ← ms
  let direct := interpretDesign D e inp st
  let t9 ← ms
  match projected with
  | .error _ => do say "  ONE CYCLE: projected path refused"; return 5
  | .ok pr   =>
      say s!"  one cycle: projected {t8 - t7} ms  direct {t9 - t8} ms"
      if digest pr != digest direct then
        say s!"  MISMATCH\n    projected {digest pr}\n    direct    {digest direct}"
        return 6
      say s!"  one cycle AGREES: {digest pr}"
  -- 5. a trace, both paths
  let stim : List Stim := (List.range cycles).map (fun k => (e, (stimulus D (seed + k)).1))
  let t10 ← ms
  let ptrace := stepTrace (stepOf sim) D st stim
  let t11 ← ms
  let dtrace := refTrace D st stim
  let t12 ← ms
  match ptrace with
  | .error _ => do say "  TRACE: projected path refused"; return 7
  | .ok rs   =>
      say s!"  trace {cycles} cycles: projected {t11 - t10} ms  direct {t12 - t11} ms"
      if rs.map digest != dtrace.map digest then
        say "  TRACE MISMATCH"; return 8
      say s!"  trace AGREES ({rs.length} results)"
  say s!"  peak RSS {← peakRSS}"
  say "  NOTE: agreement here is EXECUTION EVIDENCE about this file, not a \
kernel proof about it; `SimWF` is still undischarged for a loaded certificate."
  return 0

/-- Write the two shared fixtures out as DCERT1 files, so the pipeline can be
smoke-tested on something whose answer is already known before it is pointed at
a real design. -/
def emitFixtures (dir : String) : IO Unit := do
  IO.FS.createDirAll dir
  for (name, D) in [("tiny", Projection.Acceptance.tinyD),
                    ("seq",  Projection.Acceptance.seqD),
                    ("wide", wideD)] do
    let p := s!"{dir}/{name}.dcert"
    IO.FS.writeBinFile p (renderCert D)
    say s!"wrote {p}"

def main (args : List String) : IO UInt32 := do
  match args with
  | "--emit" :: dir :: _ => do emitFixtures dir; return 0
  | path :: rest =>
      let cycles := (rest.head?.bind String.toNat?).getD 4
      let seed   := ((rest.drop 1).head?.bind String.toNat?).getD 1
      runOne path cycles seed
  | [] => do
      say "usage: cert_runner <file.dcert> [cycles] [seed]"
      return 64

end CertLoad
end Projection
