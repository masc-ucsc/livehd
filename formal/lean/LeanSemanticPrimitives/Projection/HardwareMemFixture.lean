/-
# The whole `mainM` entry, instantiated and executed

A design that exercises everything the memory route now proves, with the
theorem's premises DISCHARGED rather than merely computed against:

* two clock domains, ordinals 0 and 1, used by different flops;
* a MUTABLE memory (`aw = 5`, `dw = 32`, `byte_w = 1` — PicoRV32's shape) and a
  read-only `memConst` ROM whose table is its actual `contents.size`;
* a flop with an ENABLE and a SYNCHRONOUS reset, and a second flop with an
  ASYNCHRONOUS reset on the other clock;
* initialised memory contents, so a write visibly changes a word.

The returned `ResultM` is decoded and its state fed to a SECOND `mainM` step,
with reachable-state preservation supplied by the theorem itself.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSlotTy

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw Projection.HwLocal

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The design -/

def fullDesign : DesignCert :=
  { sources  := #[ .memImg 0 5 32              -- 0: the mutable table
                 , .memConst 2 32 #[7, 8, 9]   -- 1: a ROM, contents.size = 3
                 , .input 0 5                  -- 2: address
                 , .input 1 32                 -- 3: byte-enable mask
                 , .input 2 1                  -- 4: read enable
                 , .input 3 1                  -- 5: flop enable
                 , .input 4 1                  -- 6: reset pin
                 , .flopQ 0 32 ]               -- 7: flop 0's current value
  , nodes    := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 2, 4] }  -- 8
                 , { op := .Op_MemRead,      width := 32, deps := #[1, 2, 4] }  -- 9
                 , { op := .Op_Xor,          width := 32, deps := #[8, 9] }     -- 10
                 , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 10, 3] } ] -- 11
  , outputs  := #[ { slot := 10, width := 32 } ]
  , flops    := #[ { width := 32, din := 10, enable := some 5, resetPin := some 6
                   , resetValue := 0, resetActiveLow := false, clock := 0
                   , asyncReset := false }
                 , { width := 32, din := 10, enable := none, resetPin := some 6
                   , resetValue := 255, resetActiveLow := false, clock := 1
                   , asyncReset := true } ]
  , memories := #[ { aw := 5, dw := 32, nextImg := 11, clock := 0 } ]
  , clocks   := #[{ name := "clkA" }, { name := "clkB" }] }

/- the REAL checker accepts it -/
#guard Direct.acceptsDesign fullDesign
#guard fullDesign.nodes.toList.all nodeAdmittedB
/- slot 0 is a MUTABLE image, slot 1 is a ROM image, slot 11 is the write's -/
#guard Direct.slotIsMem fullDesign 0 == some true
#guard Direct.slotIsMem fullDesign 1 == some true
#guard Direct.slotIsMem fullDesign 10 == some false
#guard Direct.slotIsMem fullDesign 11 == some true
/- the ROM keeps its ACTUAL contents.size, which is not a full 2^2 table -/
#guard (MemImg.ofMemConst 32 #[7, 8, 9]).words.length == 3
#guard !(decide (MemImg.FullTable 2 (MemImg.ofMemConst 32 #[7, 8, 9])))

/-! ## 1b. …and through the NEW static admission checker

`fullDesign` is admitted by `admitsB`, and the general soundness lemmas are
APPLIED to it — theorems, not `#guard` evaluations.  The geometry the checker
infers is exactly the `fullG` used below, so the hand-written assignment and
the checked one agree. -/

theorem fullDesign_admits : admitsB fullDesign = true := by decide +kernel

theorem fullDesign_semwf' : Direct.DesignSemWF fullDesign :=
  admits_semwf fullDesign_admits

theorem fullDesign_nextImgGeom : NextImgGeomOK fullDesign (geomAt fullDesign) :=
  admits_nextImgGeom fullDesign_admits

theorem fullDesign_geom_src :
    ∀ k, k < fullDesign.sources.size → geomAt fullDesign k = srcGeomOf fullDesign k :=
  admits_geom_src fullDesign_admits

/-- The whole static bundle for `fullDesign`, from the checker. -/
theorem fullDesign_static : StaticPremises fullDesign :=
  admits_static_premises fullDesign_admits

/- the CHECKER's geometry is the one the fixture uses: a mutable table at slot
   0, nothing claimed for the ROM at slot 1, and the write's result at slot 11 -/
#guard geomAt fullDesign 0 == some 5
#guard geomAt fullDesign 1 == none
#guard geomAt fullDesign 11 == some 5

/-! ## 2. Discharging the theorem's premises

Computational agreement is not instantiation, so every antecedent of
`mainM_adequate_resolved` is proved here for this design. -/

theorem fullDesign_semwf : Direct.DesignSemWF fullDesign := by
  have hacc : Direct.acceptsDesign fullDesign = true := by decide
  cases hd : Direct.designErrors fullDesign with
  | none =>
      exact Direct.checkDesign_sound (u := ()) (by unfold Direct.checkDesign; rw [hd])
  | some e =>
      exfalso
      unfold Direct.acceptsDesign at hacc
      rw [hd] at hacc
      simp at hacc

theorem fullDesign_admitted : ∀ c ∈ fullDesign.nodes.toList, NodeAdmitted c := by
  intro c hc
  have h : nodeAdmittedB c = true := by revert hc; revert c; decide
  exact (nodeAdmittedB_iff c).mp h

theorem fullDesign_arity :
    ∀ c ∈ fullDesign.nodes.toList, ArityOK c.op c.deps.toList := by
  intro c hc; revert hc; revert c; decide

/-- The mutable table claims `aw = 5`; the ROM at slot 1 claims NOTHING, so a
write to it could not be discharged. -/
def fullG : Nat → Option Nat
  | 0  => some 5
  | 11 => some 5
  | _  => none

theorem fullG_src :
    ∀ k, k < fullDesign.sources.size → fullG k = srcGeomOf fullDesign k := by
  intro k hk
  have h8 : k < 8 := by simpa [fullDesign] using hk
  rcases k with _|_|_|_|_|_|_|_|k
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · omega

theorem fullG_nextImg : NextImgGeomOK fullDesign fullG := by
  intro idx md hmd
  rcases idx with _|idx
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 11, clock := 0 } : MemoryDesc) = md := by
      simpa [fullDesign] using hmd
    subst hmd'; rfl
  · simp [fullDesign] at hmd

theorem fullG_prop : ∀ (j : Nat) (c : DenseNodeCert),
    fullDesign.nodes[j]? = some c → WritePropagates fullDesign fullG j c := by
  intro j c hc bw hop d0 hd0
  rcases j with _|_|_|_|j
  · have hc' : ({ op := .Op_MemRead, width := 32, deps := #[0, 2, 4] } : DenseNodeCert) = c := by
      simpa [fullDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_MemRead, width := 32, deps := #[1, 2, 4] } : DenseNodeCert) = c := by
      simpa [fullDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_Xor, width := 32, deps := #[8, 9] } : DenseNodeCert) = c := by
      simpa [fullDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 10, 3] } : DenseNodeCert) = c := by
      simpa [fullDesign] using hc
    subst hc'
    have h0 : (0 : Nat) = d0 := by simpa using hd0
    subst h0; rfl
  · simp [fullDesign] at hc

theorem full_addr_width (i : RuntimeInput) (st : StateM) :
    (slotValM fullDesign i st 2).asBV.width = 5 := by
  rw [slotValM_source (D := fullDesign) (i := i) (st := st)
        (k := 2) (by simp [fullDesign])]
  simp [fullDesign, sourceValue, CertVal.asBV, bv_resize, mk_bv]

/-- The checker's own assignment is `fullG`, EVERYWHERE -- so `fullG_fits`
below is a control, not a dependency: `fullStep` takes its write-fit from
`admits_writeAddrFits`. -/
theorem geomAt_full : geomAt fullDesign = fullG := by
  obtain ⟨ts, hts⟩ := admitsB_infer fullDesign_admits
  have hsz : ts.size = 12 := by
    rw [admitsB_size fullDesign_admits hts]; rfl
  funext k
  rcases k with _|_|_|_|_|_|_|_|_|_|_|_|n
  all_goals first
    | decide +kernel
    | (rw [geomAt_out hts (by omega)]; rfl)

/-- `fullG_fits`, with the hand case analysis deleted -- the generic theorem
proves exactly the statement the hand proof below proves. -/
theorem full_fits_checked (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ fullDesign.nodes.toList, WriteAddrFits fullDesign i st fullG c := by
  rw [← geomAt_full]
  exact admits_writeAddrFits fullDesign_admits i st

theorem fullG_fits (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ fullDesign.nodes.toList, WriteAddrFits fullDesign i st fullG c := by
  intro c hc bw hop d0 d1 d3 hd0 hd1 hd3 _hen
  have hc4 : c = ({ op := .Op_MemRead, width := 32, deps := #[0, 2, 4] } : DenseNodeCert)
           ∨ c = ({ op := .Op_MemRead, width := 32, deps := #[1, 2, 4] } : DenseNodeCert)
           ∨ c = ({ op := .Op_Xor, width := 32, deps := #[8, 9] } : DenseNodeCert)
           ∨ c = ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 10, 3] } : DenseNodeCert) := by
    simpa [fullDesign] using hc
  rcases hc4 with h | h | h | h <;> subst h
  · simp at hop
  · simp at hop
  · simp at hop
  · have h0 : (0 : Nat) = d0 := by simpa using hd0
    have h1 : (2 : Nat) = d1 := by simpa using hd1
    subst h0; subst h1
    exact ⟨5, rfl, by simp [full_addr_width]⟩

/-! ## 3. One step, from the CHECKER

`fullG_fits` below is the hand-written write-fit this fixture used to carry.
It is retained as documentation that the hand route and the checked route
agree, but `fullStep` no longer uses it: the premise now comes from
`admits_writeAddrFits`, derived from the inferred widths. -/

/-- The whole entry, on the RESOLVED program, for any state that carries the
declared geometry.  The returned state carries it too, so the step composes. -/
theorem fullStep (i : RuntimeInput) (hi : i.size = 5)
    (e : ClockEdges) (he : e.size = 2)
    (st : StateM) (hshape : StateShapeOK fullDesign st) (hfl : 2 ≤ st.flops.size) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign fullDesign, encEdges e, encInput i,
            StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign fullDesign e i (StateM.denote st)
      ∧ StateShapeOK fullDesign r.nextState := by
  have hin : ∀ sd ∈ fullDesign.sources.toList, SourceInputBound sd i.size := by
    intro sd hsd
    rw [hi]
    revert hsd; revert sd; decide
  have hstb : ∀ sd ∈ fullDesign.sources.toList, SourceFlopBound sd st.flops.size := by
    intro sd hsd
    have : sd = .memImg 0 5 32 ∨ sd = .memConst 2 32 #[7, 8, 9]
         ∨ sd = .input 0 5 ∨ sd = .input 1 32 ∨ sd = .input 2 1
         ∨ sd = .input 3 1 ∨ sd = .input 4 1 ∨ sd = .flopQ 0 32 := by
      simpa [fullDesign] using hsd
    rcases this with h|h|h|h|h|h|h|h <;> subst h <;>
      first | exact trivial | (simp [SourceFlopBound]; omega)
  have hfclk : ∀ f ∈ fullDesign.flops.toList, f.clock < e.size := by
    intro f hf
    rw [he]
    revert hf; revert f; decide
  have hmclk : ∀ md ∈ fullDesign.memories.toList, md.clock < e.size := by
    intro md hmd
    rw [he]
    revert hmd; revert md; decide
  have hfsize : fullDesign.flops.size ≤ st.flops.size := by
    simpa [fullDesign] using hfl
  -- the write-fit is now the GENERIC checker-derived theorem, not a hand proof
  exact admits_mainM_resolved fullDesign_admits hshape hin hstb hfclk hmclk hfsize

/-! ## 4. A state, and two steps -/

/-- Word `k` holds `k + 1`, so a write visibly changes something. -/
def initTable : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k + 1)), mk_bv 32 0⟩

def fullState : StateM := { flops := #[mk_bv 32 0, mk_bv 32 0], mems := #[initTable] }

theorem initTable_full : MemImg.FullTable 5 initTable := by
  simp [MemImg.FullTable, initTable]

theorem fullState_shape : StateShapeOK fullDesign fullState := by
  intro idx md hmd
  rcases idx with _|idx
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 11, clock := 0 } : MemoryDesc) = md := by
      simpa [fullDesign] using hmd
    subst hmd'; exact ⟨initTable, rfl, initTable_full⟩
  · simp [fullDesign] at hmd

/-- The returned state has as many flops as the design declares — read off the
adequacy equation, not assumed. -/
theorem flops_size_of_denote {D : DesignCert} {e : ClockEdges} {i : RuntimeInput}
    {st : StateM} {r : StateM.ResultM}
    (h : r.denote = Compiler.interpretDesign D e i (StateM.denote st)) :
    r.nextState.flops.size = D.flops.size := by
  have hx := congrArg (fun x => x.nextState.flops.size) h
  simpa [StateM.ResultM.denote, StateM.denote, Compiler.interpretDesign] using hx

/-- **Two steps.**  The second runs on the state the FIRST returned, and both
agree with `Compiler.interpretDesign` on their own inputs and edges. -/
theorem fullTwoSteps (i₁ i₂ : RuntimeInput) (h1 : i₁.size = 5) (h2 : i₂.size = 5)
    (e₁ e₂ : ClockEdges) (he1 : e₁.size = 2) (he2 : e₂.size = 2) :
    ∃ r₁ r₂ : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign fullDesign, encEdges e₁, encInput i₁,
            StateM.encStateM fullState].map Term.lit)) (StateM.encResultM r₁)
      ∧ r₁.denote = Compiler.interpretDesign fullDesign e₁ i₁ (StateM.denote fullState)
      ∧ Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign fullDesign, encEdges e₂, encInput i₂,
            StateM.encStateM r₁.nextState].map Term.lit)) (StateM.encResultM r₂)
      ∧ r₂.denote
          = Compiler.interpretDesign fullDesign e₂ i₂ (StateM.denote r₁.nextState) := by
  obtain ⟨r₁, hev1, hden1, hsh1⟩ :=
    fullStep i₁ h1 e₁ he1 fullState fullState_shape (by decide)
  have hfl1 : 2 ≤ r₁.nextState.flops.size := by
    rw [flops_size_of_denote hden1]
    simp [fullDesign]
  obtain ⟨r₂, hev2, hden2, -⟩ := fullStep i₂ h2 e₂ he2 r₁.nextState hsh1 hfl1
  exact ⟨r₁, r₂, hev1, hden1, hev2, hden2⟩

/-! ## 5. Executed: the same entry, run and decoded

Address 1: the mutable table holds 2 there, the ROM holds 8, so the `Op_Xor`
node gives 10 and the write stores 10 — a visible change.  The ROM is read at
an address it DOES hold, so its actual contents matter. -/

private def runMain (st : StateM) (i : RuntimeInput) (e : ClockEdges) :
    Option StateM.ResultM :=
  match evalFuel 4000000 hwMemP []
      (.call ((idOf "mainM").getD 999)
         [ .lit (encDesign fullDesign), .lit (encEdges e), .lit (encInput i)
         , .lit (StateM.encStateM st) ]) with
  | .value v => StateM.decResultM v
  | _        => none

/- addr, mask, readEn, flopEn, reset -/
private def inRun   : RuntimeInput := #[mk_bv 5 1, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 1 1, mk_bv 1 0]
private def inNoWr  : RuntimeInput := #[mk_bv 5 1, mk_bv 32 0,          mk_bv 1 1, mk_bv 1 1, mk_bv 1 0]
private def inReset : RuntimeInput := #[mk_bv 5 1, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 1 1, mk_bv 1 1]

private def eA   : ClockEdges := #[true,  false]
private def eB   : ClockEdges := #[false, true]
private def eOff : ClockEdges := #[false, false]
private def eAll : ClockEdges := #[true,  true]

/- the starting contents, and the ROM's actual words -/
#guard initTable.get 1 == mk_bv 32 2
#guard (MemImg.ofMemConst 32 #[7, 8, 9]).get 1 == mk_bv 32 8
/- …and the ROM's DEFAULT above its contents -/
#guard (MemImg.ofMemConst 32 #[7, 8, 9]).get 20 == mk_bv 32 0

/- CLOCK A ONLY: output 10, memory word 1 becomes 10, flop 0 captures 10,
   flop 1 (clock B, quiet, reset low) holds -/
#guard (match runMain fullState inRun eA with
        | some r => r.outputs == #[mk_bv 32 10]
                    && r.nextState.flops == #[mk_bv 32 10, mk_bv 32 0]
                    && (r.nextState.mems[0]!).get 1 == mk_bv 32 10
        | none   => false)

/- BOTH QUIET: the memory word and BOTH flops are preserved EXACTLY -/
#guard (match runMain fullState inRun eOff with
        | some r => r.nextState.flops == #[mk_bv 32 0, mk_bv 32 0]
                    && r.nextState.mems[0]! == initTable
        | none   => false)

/- NEGATIVE CONTROL: the quiet result DIFFERS from the active one, so a proof
   that ignored the clock would be wrong here -/
#guard (match runMain fullState inRun eA, runMain fullState inRun eOff with
        | some ra, some rq => ra.nextState != rq.nextState
        | _,       _       => false)

/- ASYNCHRONOUS RESET with BOTH CLOCKS QUIET: flop 1 still resets to 255,
   because its reset is asynchronous; flop 0's SYNCHRONOUS reset does not fire
   and it holds -/
#guard (match runMain fullState inReset eOff with
        | some r => r.nextState.flops == #[mk_bv 32 0, mk_bv 32 255]
        | none   => false)

/- NEGATIVE CONTROL for the reset: with the reset low and the same quiet edges,
   flop 1 does NOT become 255 -- so the async path is really being taken -/
#guard (match runMain fullState inRun eOff with
        | some r => r.nextState.flops[1]! != mk_bv 32 255
        | none   => false)

/- SYNCHRONOUS reset needs its edge: with clock A firing and reset high,
   flop 0 loads 0 rather than the din value 10 -/
#guard (match runMain fullState inReset eAll with
        | some r => r.nextState.flops == #[mk_bv 32 0, mk_bv 32 255]
        | none   => false)

/- DISABLED WRITE: a zero byte-enable leaves the image EXACTLY alone even with
   clock A firing -/
#guard (match runMain fullState inNoWr eA with
        | some r => r.nextState.mems[0]! == initTable
        | none   => false)

/- TWO STEPS: feed the decoded state back in.  The second step reads the word
   the first wrote (10), xors it with the ROM's 8, and writes 2 back. -/
#guard (match runMain fullState inRun eA with
        | some r1 =>
            (match runMain r1.nextState inRun eA with
             | some r2 => r2.outputs == #[mk_bv 32 2]
                          && (r2.nextState.mems[0]!).get 1 == mk_bv 32 2
             | none    => false)
        | none    => false)

#print axioms fullDesign_admits
#print axioms fullDesign_static
#print axioms fullDesign_semwf
#print axioms fullG_src
#print axioms fullG_nextImg
#print axioms fullG_prop
#print axioms geomAt_full
#print axioms full_fits_checked
#print axioms fullG_fits
#print axioms fullState_shape
#print axioms fullStep
#print axioms fullTwoSteps

end HwMem
end Projection
