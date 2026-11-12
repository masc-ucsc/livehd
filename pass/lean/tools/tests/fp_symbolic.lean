import HDLProof.Sym.Eval
import HDLProof.Sym.Asg

/-!
The fp route's symbolic primitives against the concrete object-language semantics: for each
primitive whose symbolic operands are new (`mulI`, `divI`, `modI`; `bvBit` at a symbolic
position; `bvGetMask` with a symbolic mask), build the operands from SAT variables, run
`symPrim`, evaluate the answer under an assignment, and compare it with `evalPrim` of the
concrete operands -- exhaustively at small widths (every value, negatives, zero divisors, the
most negative value, positions out of range) and at chosen edge values at wide widths. A
symbolic answer must exist (no refusal) and agree.
-/

open Projection HDLProof.Sym Std.Tactic.BVDecide

namespace FpSymbolicTest

def demand (ok : Bool) (message : String) : IO Unit :=
  unless ok do throw (IO.userError message)

def evalVal (σ : Asg) : SVal → Val
  | .conc v => v
  | .bv w e => ofBV (mk_bv w (e.eval σ).toNat)
  | .bool e => .bool (bitOf (e.eval σ))
  | .int i => .int (i.val σ)
  | .cons h t => .cons (evalVal σ h) (evalVal σ t)
  | .ctor tag vs => .ctor tag (vs.map (evalVal σ))
  | .nbv w e => ofBV ⟨w, ((e.eval σ).toNat : Int)⟩
  | .alt c a b => if bitOf (c.eval σ) then evalVal σ a else evalVal σ b

/-- Two values agree: equal, or bit vectors of one width with one normalized value. -/
def agree (a b : Val) : Bool :=
  match asBV a, asBV b with
  | some x, some y => x.width == y.width && bv_uint x == bv_uint y
  | _, _ => Val.beq a b

/-- A signed `w`-bit SAT variable `k` read as an exact integer. -/
def intVar (w k : Nat) : SVal := .int (SInt.ofSint (.var k : BVExpr w))

def packInt (w : Nat) (z : Int) : PBV := ⟨BitVec.ofInt w z⟩
def packNat (w : Nat) (z : Nat) : PBV := ⟨BitVec.ofNat w z⟩

/-- Run one primitive symbolically under `σ` and compare with `evalPrim` on `vs`. -/
def check (tag : String) (p : Prim) (ss : List SVal) (σ : Asg) (vs : List Val) : IO Unit := do
  match evalPrim p vs with
  | .error _ => pure ()   -- the concrete primitive fails: nothing to agree with
  | .ok expected =>
    match symPrim p ss with
    | .error m => throw (IO.userError s!"{tag}: symbolic {repr p} refused: {m}")
    | .ok s =>
      let got := evalVal σ s
      demand (agree got expected) s!"{tag}: {repr p} on {repr vs}: got {repr got}, expected {repr expected}"

def signedRange (w : Nat) : List Int :=
  (List.range (2 ^ w)).map fun (n : Nat) => (n : Int) - (2 : Int) ^ (w - 1)

/-- `mulI`, `divI`, `modI`: both operands symbolic, and each one concrete. -/
def testIntOps : IO Nat := do
  let mut n := 0
  for (wa, wb) in [(1, 1), (3, 3), (4, 2), (2, 4), (5, 3)] do
    for a in signedRange wa do
      for b in signedRange wb do
        let σ := asgOf #[packInt wa a, packInt wb b]
        for p in [Prim.mulI, .divI, .modI] do
          check s!"sym*sym {wa}x{wb}" p [intVar wa 0, intVar wb 1] σ [.int a, .int b]
          check s!"sym*conc {wa}x{wb}" p [intVar wa 0, .conc (.int b)] σ [.int a, .int b]
          check s!"conc*sym {wa}x{wb}" p [.conc (.int a), intVar wb 1] σ [.int a, .int b]
          n := n + 3
  -- wide operands at edge values
  let edges (w : Nat) : List Int :=
    [0, 1, -1, 2, -2, 3, 7, -7, (2 : Int) ^ (w - 1) - 1, -((2 : Int) ^ (w - 1)), -((2 : Int) ^ (w - 1)) + 1,
     (2 : Int) ^ (w / 2), 12345]
  for (wa, wb) in [(32, 32), (48, 16), (63, 63), (33, 17)] do
    for a in edges wa do
      for b in edges wb do
        let σ := asgOf #[packInt wa a, packInt wb b]
        for p in [Prim.mulI, .divI, .modI] do
          check s!"wide {wa}x{wb}" p [intVar wa 0, intVar wb 1] σ [.int a, .int b]
          n := n + 1
  return n

/-- `bvBit` at a symbolic (signed) position, inside and outside the width. -/
def testBitAt : IO Nat := do
  let mut n := 0
  for wx in [1, 3, 5] do
    for x in List.range (2 ^ wx) do
      for wi in [2, 4] do
        for i in signedRange wi do
          let σ := asgOf #[packNat wx x, packInt wi i]
          check s!"bvBit {wx}/{wi}" .bvBit [.bv wx (.var 0), intVar wi 1] σ [ofBV (mk_bv wx x), .int i]
          n := n + 1
  return n

/-- `bvGetMask` with a symbolic mask (and symbolic data), at several result widths. -/
def testGetMask : IO Nat := do
  let mut n := 0
  for (wx, wm) in [(1, 1), (3, 3), (4, 2), (2, 4), (4, 4)] do
    for x in List.range (2 ^ wx) do
      for m in List.range (2 ^ wm) do
        let σ := asgOf #[packNat wx x, packNat wm m]
        for w in [1, 2, 3, 5] do
          check s!"getMask {wx}/{wm}/{w}" .bvGetMask [.conc (.int w), .bv wx (.var 0), .bv wm (.var 1)] σ
            [.int w, ofBV (mk_bv wx x), ofBV (mk_bv wm m)]
          n := n + 1
  return n


/-- A normalized bit vector: `bvMk w` of a symbolic integer (the answer `symPrim` marks `.nbv`). -/
def nbvOf (w : Nat) (i : SVal) : SVal :=
  match symPrim .bvMk [.conc (.int w), i] with
  | .ok s => s
  | .error _ => .conc .nil

/-- `eqV` on every pair from a mix of symbolic values: integers, Booleans, normalized and raw bit
vectors, concrete values, list cells and constructors. Raw bit vectors of one width may be refused
(their raw value is not known); everything else must be answered and agree with `Val.beq`. -/
def testEqV : IO Nat := do
  let mut n := 0
  for a in [0, 1, 5] do
    for b in [0, 1, 5] do
      for c in [false, true] do
        let σ := asgOf #[packInt 4 a, packInt 4 b, ⟨BitVec.ofBool c⟩, packNat 3 (b.toNat % 8)]
        let ia := intVar 4 0
        let ib := intVar 4 1
        let vals : List SVal :=
          [ ia, ib, .conc (.int 1), .bool (.var 2), .conc (.bool true), .conc .nil
          , nbvOf 4 ia, nbvOf 4 ib, nbvOf 8 ia, .conc (ofBV (mk_bv 4 1))
          , .cons ia (.conc .nil), .cons (nbvOf 4 ib) (.cons ib (.conc .nil))
          , .ctor 7 [ia, .bool (.var 2)], .ctor 7 [ib, .conc (.bool true)], .ctor 8 [ia]
          , .bv 3 (.var 3) ]
        for x in vals do
          for y in vals do
            let rawSame := match x, y with
              | .bv w _, .bv w' _ => w == w'
              | .bv _ _, _ => true
              | _, .bv _ _ => true
              | _, _ => false
            let vx := evalVal σ x
            let vy := evalVal σ y
            match symPrim .eqV [x, y] with
            | .error m =>
                unless rawSame do throw (IO.userError s!"eqV refused on {repr vx} {repr vy}: {m}")
            | .ok r =>
                demand (agree (evalVal σ r) (.bool (Val.beq vx vy)))
                  s!"eqV {repr vx} {repr vy}: got {repr (evalVal σ r)}"
            n := n + 1
  return n

/-- `mkCtorP`, `ctorTagP`, `ctorFieldsP` on symbolic fields and on a normalized bit vector. -/
def testCtorPrims : IO Nat := do
  let mut n := 0
  for a in signedRange 3 do
    for b in signedRange 3 do
      let σ := asgOf #[packInt 3 a, packInt 3 b]
      let ia := intVar 3 0
      let ib := intVar 3 1
      let lst : SVal := .cons ia (.cons (nbvOf 3 ib) (.conc .nil))
      let mk := evalVal σ lst
      check "mkCtorP" .mkCtorP [.conc (.int 5), lst] σ [.int 5, mk]
      let c : SVal := .ctor 9 [ia, nbvOf 3 ib]
      check "ctorTagP" .ctorTagP [c] σ [evalVal σ c]
      check "ctorFieldsP" .ctorFieldsP [c] σ [evalVal σ c]
      check "ctorTagP bv" .ctorTagP [nbvOf 3 ia] σ [evalVal σ (nbvOf 3 ia)]
      check "ctorFieldsP bv" .ctorFieldsP [nbvOf 3 ia] σ [evalVal σ (nbvOf 3 ia)]
      check "ctorTagP raw bv" .ctorTagP [.bv 3 (.var 0)] σ [evalVal σ (.bv 3 (.var 0))]
      n := n + 6
  return n

/-- Run a term symbolically and concretely (`evalFuel`) and compare. -/
def checkTerm (tag : String) (vals : Array PBV) (senv : List SVal) (t : Term) : IO Unit := do
  let σ0 := asgOf vals
  let env := senv.map (evalVal σ0)
  match evalFuel 1000 ⟨[], 0⟩ env t with
  | .value expected =>
    match symEval 1000 ⟨vals.size, #[]⟩ senv t with
    | .error m => throw (IO.userError s!"{tag}: refused: {m}")
    | .ok (s, st) =>
      let σ := extendSt vals st
      demand (agree (evalVal σ s) expected) s!"{tag}: got {repr (evalVal σ s)}, expected {repr expected}"
  | _ => pure ()

/-- `caseT` on a normalized bit vector reads its width and value. -/
def testCaseNbv : IO Nat := do
  let mut n := 0
  for x in List.range 16 do
    -- let y := bvMk 4 (bvUint x) in case y of bvTag [w, v] => v + w
    let t : Term := .letIn (.prim .bvMk [.lit (.int 4), .prim .bvUint [.var 0]])
      (.caseT (.var 0) [(bvTag, 2, .prim .addI [.var 0, .var 1])])
    checkTerm s!"caseT nbv {x}" #[packNat 4 x] [.bv 4 (.var 0)] t
    n := n + 1
  return n


/-- Guarded unions: `ite` with branches of different shapes, `caseT` on such a union, structural
primitives on it (`isNil`/`hd`/`tl`, distributed), and a width chosen by a branch. -/
def testUnions : IO Nat := do
  let mut n := 0
  for c in [false, true] do
    for x in List.range 8 do
      let vals := #[(⟨BitVec.ofBool c⟩ : PBV), packNat 3 x]
      let senv : List SVal := [.bool (.var 0), .bv 3 (.var 1)]
      let cond : Term := .var 0
      let xv : Term := .var 1
      -- a list of length 0 or 2, then isNil / hd / tl.hd
      let lst : Term := .ite cond (.lit .nil) (.prim .consP [xv, .prim .consP [xv, .lit .nil]])
      checkTerm "isNil(union)" vals senv (.prim .isNil [lst])
      checkTerm "hd(union)" vals senv
        (.ite (.prim .isNil [lst]) (.lit (.int 7)) (.prim .bvUint [.prim .hd [lst]]))
      -- constructors of different tags and arities, then caseT
      let ct : Term := .ite cond (.ctorT 1 [xv]) (.ctorT 2 [xv, .prim .bvUint [xv]])
      checkTerm "caseT(union)" vals senv
        (.caseT ct [(1, 1, .prim .bvUint [.var 0]), (2, 2, .prim .addI [.var 1, .lit (.int 100)])])
      -- a width chosen by the branch (finite union of widths), then bvMk / bvWidth / bvUint
      let w : Term := .ite cond (.lit (.int 2)) (.lit (.int 5))
      let bv : Term := .prim .bvMk [w, .prim .bvUint [xv]]
      checkTerm "bvMk(union width)" vals senv (.prim .bvUint [bv])
      checkTerm "bvWidth(union width)" vals senv (.prim .bvWidth [bv])
      checkTerm "bvResize(union width)" vals senv (.prim .bvUint [.prim .bvResize [w, xv]])
      -- an int union used as data collapses to a mux
      checkTerm "addI(union)" vals senv (.prim .addI [w, .prim .bvUint [xv]])
      -- three-way union of widths (nested ite)
      let w3 : Term := .ite cond (.lit (.int 1)) (.ite (.prim .eqI [.prim .bvUint [xv], .lit (.int 3)])
        (.lit (.int 4)) (.lit (.int 6)))
      checkTerm "bvMk(3 widths)" vals senv (.prim .bvUint [.prim .bvMk [w3, .prim .bvUint [xv]]])
      n := n + 8
  return n

/-- Refusals that must stay, with their class: a width that depends on the data, and a union
past the 64-leaf bound. -/
def testRefusals : IO Nat := do
  let refuses (tag : String) (cls : String) (senv : List SVal) (vals : Array PBV) (t : Term) : IO Unit := do
    match symEval 1000 ⟨vals.size, #[]⟩ senv t with
    | .ok _ => throw (IO.userError s!"{tag}: expected a refusal")
    | .error m => demand ((m.splitOn cls).length > 1) s!"{tag}: refusal {m} lacks class {cls}"
  -- bvMk with a width read from an input: data-dependent
  refuses "data-dependent width" "data-dependent-width" [.bv 3 (.var 0)] #[packNat 3 5]
    (.prim .bvMk [.prim .bvUint [.var 0], .lit (.int 1)])
  -- 70 nested ites over constructors of 70 different tags: the union passes 64 leaves
  let chain := Id.run do
    let mut u : Term := .ctorT 1000 []
    for k in List.range 70 do
      u := .ite (.prim .eqI [.prim .bvUint [.var 0], .lit (.int k)]) (.ctorT (k + 1) []) u
    return u
  refuses "union bound" "resource-limit" [.bv 8 (.var 0)] #[packNat 8 3] chain
  return 2

end FpSymbolicTest

def main : IO Unit := do
  let a ← FpSymbolicTest.testIntOps
  let b ← FpSymbolicTest.testBitAt
  let c ← FpSymbolicTest.testGetMask
  let d ← FpSymbolicTest.testEqV
  let e ← FpSymbolicTest.testCtorPrims
  let f ← FpSymbolicTest.testCaseNbv
  let g ← FpSymbolicTest.testUnions
  let h ← FpSymbolicTest.testRefusals
  IO.println s!"PASS fp symbolic primitives: {a} integer cases (mulI/divI/modI), {b} bvBit cases, \
{c} bvGetMask cases, {d} eqV cases, {e} constructor-primitive cases, {f} caseT-on-bit-vector cases, \
{g} guarded-union cases, {h} refusal-class cases"
