import HDLProof.Sym.Step
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

/-! Shared symbolic helpers of the direct (IR) and compiled (VC) traversals, and the
decoding of a step's arguments. The operator builders and the two dispatchers are in
`Sym.NativeBuilders` and `Sym.Native`; their soundness in `NativeSound`, `NativeBuilders`
and `NativeRoute`.
-/

open Projection Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

def widthVal (w : Nat) : SVal := .conc (.int w)

def constVal (w : Nat) (v : Int) : SVal := .conc (ofBV (mk_bv w v))

def resize (w : Nat) (x : SVal) : Option SVal := symBvResize [widthVal w, x]

def select (w : Nat) (c a b : SVal) : Option SVal := do
  let ⟨_, C⟩ ← asSBV c
  let ⟨_, A⟩ ← asSBV a
  let ⟨_, B⟩ ← asSBV b
  pure (.bv w (muxE (nzBit C) (zextE w A) (zextE w B)))

def foldBits (op : BVBinOp) (w : Nat) (init : SVal) (xs : List SVal) : Option SVal :=
  xs.foldlM (fun a b => symBvBitwise op [widthVal w, a, b]) init

def neg {w : Nat} (a : BVExpr w) : BVExpr w :=
  .bin (.un .not a) .add (.const 1#w)

/-- Divide at a width large enough to preserve both operands. The reference
returns zero for division by zero, unlike BitVec's unsigned division. -/
def compare (signed greater : Bool) (w : Nat) (a b : SVal) : Option SVal := do
  let ⟨_, A⟩ ← asSBV a
  let ⟨_, B⟩ ← asSBV b
  let x := if signed then SInt.ofSint A else SInt.ofUint A
  let y := if signed then SInt.ofSint B else SInt.ofUint B
  pure (.bv w (zextE w (if greater then SInt.lt y x else SInt.lt x y)))

def stateParts : SVal → Option (SVal × SVal)
  | .ctor tag [fs, ms] => if tag = StateM.tagStateM then some (fs, ms) else none
  | .conc (.ctor tag [fs, ms]) =>
      if tag = StateM.tagStateM then some (.conc fs, .conc ms) else none
  | _ => none

def edgesOf : SVal → Option ClockEdges
  | .conc v => decEdges v
  | _ => none

def source (inputs flops memories : SVal) : SourceDesc → Option SVal
  | .input idx w => sNth inputs idx >>= resize w
  | .const w v => some (constVal w v)
  | .flopQ idx w => sNth flops idx >>= resize w
  | .flopQAsync idx w ri rv al => do
      let r ← sNth inputs ri
      let q ← sNth flops idx
      if al then select w r q (constVal w rv)
      else select w r (constVal w rv) q
  | .memImg idx _ _ => sNth memories idx
  | .memConst _ dw contents => some (.conc (MemImg.encMemImg (MemImg.ofMemConst dw contents)))

end HDLProof.Sym.Native
