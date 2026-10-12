/-
The WHOLE memory-aware cycle, executed through `mainM`, compared with the
independently checked reference scenario.

The design is the supervisor's `ReferenceClockScenarioCorrected` shape: two
mutable 32-word memories on DISTINCT clock domains (memory 0 on clock 1, memory 1
on clock 0), `Op_MemWriteBE 1` bit-enabled writes, before-write reads, and an
initialised ROM read.  Every run below is `evalFuel hwMemP` at the resolved
`mainM` entry on `encDesign`/`encEdges`/`encInput`/`encStateM` -- not a host
`stepOnce`, not a direct `srcValM`/`applyOpM` call.
-/
import LeanSemanticPrimitives.Projection.HardwareMem
open Projection Projection.HwMem Projection.MemImg Projection.StateM Compiler

private def design : DesignCert :=
{ sources := #[.memImg 0 5 32, .memImg 1 5 32, .input 0 5, .input 1 32,
               .input 2 32, .const 1 1, .memConst 5 32 #[11,22,33], .const 5 2]
, nodes := #[{op := .Op_MemRead, width := 32, deps := #[0,2,5]},
             {op := .Op_MemWriteBE 1, width := 32, deps := #[0,2,3,4]},
             {op := .Op_MemRead, width := 32, deps := #[1,2,5]},
             {op := .Op_MemWriteBE 1, width := 32, deps := #[1,2,3,4]},
             {op := .Op_MemRead, width := 32, deps := #[6,7,5]}]
, outputs := #[{slot := 8, width := 32}, {slot := 10, width := 32}, {slot := 12, width := 32}]
, flops := #[]
, memories := #[{aw := 5, dw := 32, nextImg := 9, clock := 1},
                {aw := 5, dw := 32, nextImg := 11, clock := 0}]
, clocks := #[{name := "clock0"}, {name := "clock1"}] }

private def initialM : StateM := ⟨#[], #[zeroImg 5 32, (zeroImg 5 32).setAt 7 (mk_bv 32 22)]⟩
private def inputOf (mask : Nat) (data : Nat := 0xFFFFFFFF) : RuntimeInput :=
  #[mk_bv 5 7, mk_bv 32 data, mk_bv 32 mask]

private def mainMId : Nat := (idOf "mainM").getD 999
private def F : Nat := 2000000

/-- One whole cycle through the ACTUAL entry, decoded back to a `ResultM`. -/
private def cycle (e : ClockEdges) (mask : Nat) (data : Nat := 0xFFFFFFFF) : Option ResultM :=
  match evalFuel F hwMemP []
          (.call mainMId [.lit (encDesign design), .lit (encEdges e),
                          .lit (encInput (inputOf mask data)), .lit (encStateM initialM)]) with
  | .value v => decResultM v
  | _        => none

/-- The word a committed memory holds at address 7. -/
private def wordAt (r : ResultM) (idx : Nat) : Option BV :=
  (r.nextState.mems[idx]?).map (fun img => img.get 7)

#guard (idOf "mainM").isSome
#guard hwMemS.entry == "mainM"

/-! ### Clock-1 only, mask 8 -- memory 0 commits, memory 1 holds -/
#guard (match cycle #[false, true] 8 with
        | some r => wordAt r 0 == some (mk_bv 32 8) | none => false)
#guard (match cycle #[false, true] 8 with
        | some r => wordAt r 1 == some (mk_bv 32 22) | none => false)
/- outputs: before-write reads are 0 and 22, the ROM read is 33 -/
#guard (match cycle #[false, true] 8 with
        | some r => r.outputs == #[mk_bv 32 0, mk_bv 32 22, mk_bv 32 33] | none => false)

/-! ### Clock-0 only -- memory 1 commits, memory 0 holds -/
#guard (match cycle #[true, false] 8 with
        | some r => wordAt r 0 == some (mk_bv 32 0) | none => false)
#guard (match cycle #[true, false] 8 with
        | some r => wordAt r 1 == some (mk_bv 32 30) | none => false)

/-! ### Both quiet -- NEITHER commits; both hold their exact prior contents -/
#guard (match cycle #[false, false] 8 with
        | some r => wordAt r 0 == some (mk_bv 32 0) | none => false)
#guard (match cycle #[false, false] 8 with
        | some r => wordAt r 1 == some (mk_bv 32 22) | none => false)
/- …and the whole image array is the prior one -/
#guard (match cycle #[false, false] 8 with
        | some r => r.nextState.mems == initialM.mems | none => false)

/-! ### A full enable writes DEADBEEF; a disabled write preserves contents -/
#guard (match cycle #[false, true] 0xFFFFFFFF 0xDEADBEEF with
        | some r => wordAt r 0 == some (mk_bv 32 0xDEADBEEF) | none => false)
#guard (match cycle #[false, true] 0 with
        | some r => wordAt r 0 == some (mk_bv 32 0) | none => false)

/-! ### A SECOND step from the decoded returned state observes the commit -/
private def cycle2 (e : ClockEdges) (st : StateM) (mask : Nat) : Option ResultM :=
  match evalFuel F hwMemP []
          (.call mainMId [.lit (encDesign design), .lit (encEdges e),
                          .lit (encInput (inputOf mask)), .lit (encStateM st)]) with
  | .value v => decResultM v
  | _        => none

#guard (match cycle #[false, true] 8 with
        | some r => (match cycle2 #[false, false] r.nextState 8 with
                     | some r2 => r2.outputs == #[mk_bv 32 8, mk_bv 32 22, mk_bv 32 33]
                     | none    => false)
        | none   => false)
