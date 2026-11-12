import HDLProof.Sym.Native

open Projection Compiler HDLProof.Sym HDLProof.Sym.Native Std.Tactic.BVDecide

namespace NativeSymbolicTest

def demand (ok : Bool) (message : String) : IO Unit :=
  unless ok do throw (IO.userError message)

def need {α : Type} (message : String) : Option α → IO α
  | some x => pure x
  | none => throw (IO.userError message)

def fromExcept {α : Type} : Except String α → IO α
  | .ok x => pure x
  | .error message => throw (IO.userError message)

def evalVal (σ : Asg) : SVal → Val
  | .conc v => v
  | .bv w e => ofBV (mk_bv w (e.eval σ).toNat)
  | .bool e => .bool (bitOf (e.eval σ))
  | .int i => .int (i.val σ)
  | .cons h t => .cons (evalVal σ h) (evalVal σ t)
  | .ctor tag vs => .ctor tag (vs.map (evalVal σ))
  | .nbv w e => ofBV ⟨w, ((e.eval σ).toNat : Int)⟩
  | .alt c a b => if bitOf (c.eval σ) then evalVal σ a else evalVal σ b

def packed (b : BV) : PBV := ⟨BitVec.ofInt b.width (bv_uint b)⟩

def scalar (op : LGraphOp) (w : Nat) (args : List BV) (constantMask := false) : IO Unit := do
  let ss := args.zipIdx.map fun (b, k) =>
    if constantMask && k == 1 then SVal.conc (ofBV b) else .bv b.width (.var k)
  let σ := asgOf (args.map packed).toArray
  let expected := eval_op op w args
  let actual ← need s!"IR refused {repr op}" (evalOp op w ss)
  let actualBV ← need "IR result is not scalar" (asBV (evalVal σ actual))
  demand (actualBV == expected)
    s!"IR {repr op} w={w} args={repr args}: got {repr actualBV}, expected {repr expected}"
  let rhs ← match compileOp 0 { op, width := w, deps := (List.range args.length).toArray } with
    | .ok rhs => pure rhs
    | .error err => throw (IO.userError s!"compileOp: {repr err}")
  let compiled ← need s!"VC refused {repr rhs}" (evalExpr ss.toArray rhs)
  let compiledBV ← need "VC result is not scalar" (asBV (evalVal σ compiled))
  demand (compiledBV == expected) s!"VC {repr rhs}: {repr compiledBV} != {repr expected}"

def testScalar : IO Nat := do
  let mut count := 0
  for wa in [1, 2, 4] do
    for wb in [1, 3] do
      for a in List.range (2 ^ wa) do
        for b in List.range (2 ^ wb) do
          let args := [mk_bv wa a, mk_bv wb b]
          for w in [1, 4, 8] do
            for op in [LGraphOp.Op_Sum 1, .Op_Sum 2, .Op_Mult, .Op_UDiv, .Op_SDiv,
                       .Op_And, .Op_Or, .Op_Xor, .Op_SHL, .Op_SRA, .Op_Sext,
                       .Op_EQ, .Op_ULT, .Op_UGT, .Op_SLT, .Op_SGT, .Op_Ror] do
              scalar op w args
              count := count + 1
            scalar .Op_GetMask w args true
            scalar .Op_SetMask w (args ++ [mk_bv 3 5]) true
            -- a SYMBOLIC mask (gmFold / smRunS)
            scalar .Op_GetMask w args
            scalar .Op_SetMask w (args ++ [mk_bv 3 5])
            scalar .Op_SetMask w (args ++ [mk_bv 2 2])
            count := count + 3
            scalar .Op_MuxBool w (mk_bv 2 b :: args)
            scalar .Op_MuxN w (mk_bv 2 b :: args)
            count := count + 4
  for op in [LGraphOp.Op_And, .Op_Or, .Op_Xor, .Op_Ror, .Op_EQ, .Op_SHL, .Op_MuxN] do
    if op == .Op_MuxN then continue
    scalar op 4 []
    count := count + 1
  return count

def symInput (i : RuntimeInput) : SVal :=
  sListOf (i.toList.zipIdx.map fun (b, k) => .bv b.width (.var k))

def checkStep (name : String) (D : DesignCert) (edges : ClockEdges) (inputs : RuntimeInput)
    (state : StateM) : IO Unit := do
  match Direct.checkDesign D with
  | .error err => throw (IO.userError s!"invalid fixture {name}: {repr err}")
  | .ok _ => pure ()
  let R ← match compileDesign D with
    | .error err => throw (IO.userError s!"fixture compile {name}: {repr err}")
    | .ok r => pure r
  let reference := interpretDesign D edges inputs state.denote
  for (route, stepper) in [("ir", irStepper D), ("vc", vcStepper R)] do
    let (result, st) ← fromExcept (stepper.run 1000 ⟨inputs.size, #[]⟩
      (.conc (encEdges edges)) (symInput inputs) (.conc (StateM.encStateM state)))
    demand (scopedB st) s!"{name}/{route}: forward cut definition"
    let σ := extendSt (inputs.map packed) st
    let decoded ← need s!"{name}/{route}: result shape" (StateM.decResultM (evalVal σ result))
    demand (decoded.outputs == reference.outputs) s!"{name}/{route}: outputs differ"
    demand (decoded.nextState.flops == reference.nextState.flops) s!"{name}/{route}: flops differ"
    demand (decoded.nextState.mems.size == reference.nextState.mems.size) s!"{name}/{route}: memory count"
    for idx in List.range decoded.nextState.mems.size do
      let image := decoded.nextState.mems[idx]!
      for addr in [-1, 0, 1, 2, 3, 4, 5] do
        demand (image.denote addr == reference.nextState.mems[idx]! addr)
          s!"{name}/{route}: memory {idx}[{addr}] differs"

def counter (async : Bool) : DesignCert where
  sources := #[.input 0 1, .input 1 1, .flopQ 0 4, .const 4 1]
  nodes := #[{ op := .Op_Sum 2, width := 4, deps := #[2, 3] }]
  outputs := #[{ slot := 2, width := 4 }]
  flops := #[{
    width := 4, din := 4, enable := some 0, resetPin := some 1,
    resetValue := 9, resetActiveLow := true, clock := 1, asyncReset := async }]
  memories := #[]
  clocks := #[{ name := "a" }, { name := "b" }]

def memory : DesignCert where
  sources := #[.memImg 0 2 8, .input 0 2, .input 1 8, .input 2 2, .const 1 1]
  nodes := #[{ op := .Op_MemWriteBE 4, width := 8, deps := #[0, 1, 2, 3] },
             { op := .Op_MemRead, width := 8, deps := #[0, 1, 4] },
             { op := .Op_MemRead, width := 8, deps := #[5, 1, 4] }]
  outputs := #[{ slot := 6, width := 8 }, { slot := 7, width := 8 }]
  flops := #[]
  memories := #[{ aw := 2, dw := 8, nextImg := 5, clock := 1 }]
  clocks := #[{ name := "a" }, { name := "b" }]

def bulkMemory (async : Bool) : DesignCert where
  sources := #[.memImg 0 2 8, .input 0 32, .input 1 1]
  nodes := #[{ op := .Op_MemLoad 4, width := 8, deps := #[0, 1, 2] },
             { op := .Op_MemToBus 4, width := 32, deps := #[3] },
             { op := .Op_MemToBus 3, width := 12, deps := #[3] }]
  outputs := #[{ slot := 4, width := 32 }, { slot := 5, width := 12 }]
  flops := #[]
  memories := #[{
    aw := 2, dw := 8, nextImg := 3, clock := 1,
    asyncImg := if async then some 3 else none }]
  clocks := #[{ name := "a" }, { name := "b" }]

def twoClockMemory : DesignCert where
  sources := #[.memImg 0 2 8, .input 0 2, .input 1 8, .input 2 1,
               .input 3 2, .input 4 8, .input 5 1, .const 1 1]
  nodes := #[{ op := .Op_MemWrite, width := 8, deps := #[0, 1, 2, 3] },
             { op := .Op_MemWrite, width := 8, deps := #[0, 4, 5, 6] },
             { op := .Op_MemWrite, width := 8, deps := #[8, 4, 5, 6] },
             { op := .Op_MemRead, width := 8, deps := #[10, 1, 7] }]
  outputs := #[{ slot := 11, width := 8 }]
  flops := #[]
  memories := #[{
    aw := 2, dw := 8, nextImg := 10, clock := 0,
    commits := #[{ mask := 1, nextImg := 8 }, { mask := 2, nextImg := 9 }] }]
  clocks := #[{ name := "a" }, { name := "b" }]

def testState : IO Nat := do
  let mut count := 0
  for async in [false, true] do
    for edge in [false, true] do
      for en in [0, 1] do
        for rst in [0, 1] do
          checkStep "counter" (counter async) #[true, edge] #[mk_bv 1 en, mk_bv 1 rst]
            { flops := #[mk_bv 4 6], mems := #[] }
          count := count + 1
  for edge in [false, true] do
    for addr in List.range 4 do
      for be in List.range 4 do
        checkStep "memory" memory #[true, edge] #[mk_bv 2 addr, mk_bv 8 165, mk_bv 2 be]
          { flops := #[], mems := #[⟨[mk_bv 8 17, mk_bv 8 34, mk_bv 8 51, mk_bv 8 68], mk_bv 8 0⟩] }
        count := count + 1
  for async in [false, true] do
    for edge in [false, true] do
      for en in [0, 1] do
        checkStep "bulk-memory" (bulkMemory async) #[false, edge] #[mk_bv 32 305419896, mk_bv 1 en]
          { flops := #[], mems := #[⟨[mk_bv 8 17, mk_bv 8 34, mk_bv 8 51, mk_bv 8 68], mk_bv 8 0⟩] }
        count := count + 1
  for edgeA in [false, true] do
    for edgeB in [false, true] do
      for addrB in [1, 2] do
        for enA in [0, 1] do
          for enB in [0, 1] do
            checkStep "two-clock-memory" twoClockMemory #[edgeA, edgeB]
              #[mk_bv 2 1, mk_bv 8 165, mk_bv 1 enA, mk_bv 2 addrB, mk_bv 8 90, mk_bv 1 enB]
              { flops := #[], mems := #[⟨[mk_bv 8 17, mk_bv 8 34, mk_bv 8 51, mk_bv 8 68], mk_bv 8 0⟩] }
            count := count + 1
  return count

end NativeSymbolicTest

def main : IO Unit := do
  let scalar ← NativeSymbolicTest.testScalar
  let state ← NativeSymbolicTest.testState
  IO.println s!"PASS native symbolic execution: {scalar} scalar cases, {state} state cases, both IR and VC"
