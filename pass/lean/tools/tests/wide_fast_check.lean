/-
Differential test of FastBV's wide-value fast forms against the operators' definitions.

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/wide_fast_check.lean)

The `@[csimp]` theorems in `Translation/FastBV` and `Compiler/FastResidual` already PROVE every
fast form equal to its definition for every input; this checks that the compiled code agrees
too. The operators below (`eval_op` for ir, the `r*V` operators for vc and tv) run the fast
forms, since this file is compiled after the swaps. The reference `spec*` functions are the
definitions of `Translation/LGraphModel.lean` written again with plain `Int` and `Nat`
arithmetic (`%`, `^`, `/`, `testBit`), which no `@[csimp]` theorem replaces.

Inputs: widths 0 to 4096 (around 63, 64, 128 and 129, where the fast forms change path);
values that are zero, one, all ones, the sign bit alone, patterns with and without the sign
bit, and unreduced values (negative, or wider than the width); shift and extension amounts
0, 1, around the widths, and one far past them; result widths narrower and wider.

Prints `PASS wide_fast: <n> cases` or one `FAIL` line per disagreement (at most 20).
-/
import LeanSemanticPrimitives.Translation.FastBV
import LeanSemanticPrimitives.Compiler.FastResidual

open FastBV Compiler.Residual

/-! ## The definitions, with plain arithmetic -/

def specUint (x : BV) : Int := x.value % (2 ^ x.width : Int)
def specMk (w : Nat) (v : Int) : BV := { width := w, value := v % (2 ^ w : Int) }
def specSint (x : BV) : Int :=
  let w := x.width
  let u := specUint x
  if w = 0 then 0 else if u < (2 ^ (w - 1) : Int) then u else u - (2 ^ w : Int)
def specBit (x : BV) (i : Nat) : Bool := (specUint x).toNat.testBit i
def specBitwise (w : Nat) (f : Bool → Bool → Bool) (a b : BV) : BV := Id.run do
  let mut r : Nat := 0
  for i in [0:w] do
    if f (specBit a i) (specBit b i) then r := r + 2 ^ i
  return specMk w (r : Int)
def specNot (w : Nat) (a : BV) : BV := Id.run do
  let mut r : Nat := 0
  for i in [0:w] do
    if !specBit a i then r := r + 2 ^ i
  return specMk w (r : Int)
def specShlStep (w : Nat) (a b : BV) : BV :=
  let k := (specUint b).toNat
  specMk w (if k ≥ w then 0 else specUint a * (2 : Int) ^ k)
def specShl (w : Nat) : List BV → BV
  | [] => specMk w 0
  | a :: bs => bs.foldl (fun acc b => specBitwise w (fun x y => xor x y) acc (specShlStep w a b)) (specMk w 0)
def specSra (w : Nat) (x sh : BV) : BV :=
  let k := (specUint sh).toNat
  if k ≥ x.width then specMk w (if x.width = 0 then 0 else if specSint x ≥ 0 then 0 else -1)
  else specMk w (specSint x / (2 ^ k : Int))
def specSext (w : Nat) (a amount : BV) : BV :=
  let n := (specUint amount).toNat
  if n = 0 then specMk w 0
  else if n > a.width then specMk w (specUint a)
  else
    let u := specUint a % (2 ^ n : Int)
    if u < (2 ^ (n - 1) : Int) then specMk w u else specMk w (u - (2 ^ n : Int))
def specFold (w : Nat) (f : Bool → Bool → Bool) (init : BV) (vs : List BV) : BV :=
  vs.foldl (fun acc b => specBitwise w f acc b) init

/-- `BV` equality as the simulator sees it: width and value. -/
def same (a b : BV) : Bool := a.width == b.width && a.value == b.value

/-! ## Inputs -/

def widths : List Nat := [0, 1, 2, 7, 31, 32, 33, 61, 62, 63, 64, 65, 100, 127, 128, 129, 130, 255, 505, 1024, 4096]

/-- Values of width `w`: reduced patterns, and unreduced ones (negative, or wider). -/
def vals (w : Nat) : List BV :=
  let top := 2 ^ w
  let pats : List Nat := [0, 1, top - 1, top / 2, (top - 1) / 3, (top - 1) / 5 * 2, top / 2 + 12345 % (top + 1),
                          (top - 1) / 7]
  (pats.map fun p => ({ width := w, value := ((p % (top + 1) : Nat) : Int) } : BV))
    ++ [{ width := w, value := -1 }, { width := w, value := -((top / 3 : Nat) : Int) - 5 },
        { width := w, value := ((top * 3 + 7 : Nat) : Int) }]

def amounts (w : Nat) : List Nat :=
  ([0, 1, 2, w / 2, w - 1, w, w + 1, 63, 64, 129, 2 ^ 33 + 1].filter fun k => k ≤ 2 ^ 33 + 1).eraseDups

def amt (k : Nat) : BV := { width := 40, value := (k : Int) }

def main : IO UInt32 := do
  let mut n := 0
  let mut fails : Array String := #[]
  -- `modPow2` on its own, on both sides of `2 ^ w`
  for w in widths ++ [4097, 9000] do
    for x in [0, 1, 2 ^ w - 1, 2 ^ w, 2 ^ w + 1, 2 ^ (w + 5) + 3, (2 ^ w - 1) / 3] do
      n := n + 1
      if modPow2 w x != x % 2 ^ w then fails := fails.push s!"modPow2 w={w} x={x}"
  for wa in widths do
    let xs := vals wa
    for x in xs do
      -- the signed value (SLT, SGT, SDiv, SRA)
      n := n + 1
      if bv_sint x != specSint x then fails := fails.push s!"bv_sint wa={wa} x={x.value}"
      for w in [1, wa, wa + 3, max 1 (wa / 2), 64, 200].eraseDups do
        -- resize (MuxBool's arms, And's first operand)
        n := n + 1
        if !same (bv_resize w x) (specMk w (specUint x)) then
          fails := fails.push s!"bv_resize wa={wa} w={w} x={x.value}"
        for k in amounts wa do
          -- SRA: ir (eval_op) and vc/tv (rsraV)
          n := n + 2
          let ref := specSra w x (amt k)
          if !same (eval_op .Op_SRA w [x, amt k]) ref then
            fails := fails.push s!"SRA ir wa={wa} w={w} k={k} x={x.value}"
          if !same (rsraV w x (amt k)) ref then
            fails := fails.push s!"SRA vc wa={wa} w={w} k={k} x={x.value}"
          -- Sext: ir and vc (vc's own spelling, `rsextV`)
          n := n + 2
          let refS := specSext w x (amt k)
          if !same (eval_op .Op_Sext w [x, amt k]) refS then
            fails := fails.push s!"Sext ir wa={wa} w={w} n={k} x={x.value}"
          if !same (rsextV w x (amt k)) refS then
            fails := fails.push s!"Sext vc wa={wa} w={w} n={k} x={x.value}"
          -- SHL with one amount, and with two (the XOR fold)
          if w ≤ 1024 || k ≤ 1 then
            n := n + 3
            let refH := specShl w [x, amt k]
            if !same (eval_op .Op_SHL w [x, amt k]) refH then
              fails := fails.push s!"SHL ir wa={wa} w={w} k={k} x={x.value}"
            if !same (rshlV w [x, amt k]) refH then
              fails := fails.push s!"SHL vc wa={wa} w={w} k={k} x={x.value}"
            if !same (rshlV w [x, amt k, amt (k / 2 + 1)]) (specShl w [x, amt k, amt (k / 2 + 1)]) then
              fails := fails.push s!"SHL2 vc wa={wa} w={w} k={k} x={x.value}"
      -- bitwise operators against a second operand of another width
      for y in [xs[2]!, xs[4]!, xs[8]!, xs[10]!] ++ (vals (wa + 1)).take 6 do
        for w in [wa, wa + 1, max 1 (wa / 2)].eraseDups do
          n := n + 8
          let chk (nm : String) (got ref : BV) (fs : Array String) : Array String :=
            if same got ref then fs else fs.push s!"{nm} wa={wa} w={w} x={x.value} y={y.value}"
          fails := chk "And ir" (eval_op .Op_And w [x, y]) (specFold w (· && ·) (specMk w (specUint x)) [y]) fails
          fails := chk "Or ir" (eval_op .Op_Or w [x, y]) (specFold w (· || ·) (specMk w 0) [x, y]) fails
          fails := chk "Xor ir" (eval_op .Op_Xor w [x, y]) (specFold w xor (specMk w 0) [x, y]) fails
          fails := chk "And vc" (randV w [x, y]) (specFold w (· && ·) (specMk w (specUint x)) [y]) fails
          fails := chk "Or vc" (rorBitsV w [x, y]) (specFold w (· || ·) (specMk w 0) [x, y]) fails
          fails := chk "Xor vc" (rxorV w [x, y]) (specFold w xor (specMk w 0) [x, y]) fails
          fails := chk "Not ir" (eval_op .Op_Not w [x]) (specNot w x) fails
          fails := chk "Not vc" (rnotV w x) (specNot w x) fails
          -- the other truth tables, through `bv_bitwise` itself
          for f in [fun a b => a && !b, fun a b => !a && b, fun a _ => a, fun _ b => b,
                    fun _ _ => false, fun a b => !(a || b), fun a b => a == b, fun _ _ => true] do
            n := n + 1
            fails := chk "bv_bitwise" (bv_bitwise w f x y) (specBitwise w f x y) fails
          -- signed comparisons
          n := n + 2
          fails := chk "SLT" (eval_op .Op_SLT 1 [x, y]) (specMk 1 (if specSint x < specSint y then 1 else 0)) fails
          fails := chk "SGT vc" (rsgtV 1 x y) (specMk 1 (if specSint y < specSint x then 1 else 0)) fails
  for f in fails.toList.take 20 do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS wide_fast: {n} cases"
    return 0
  IO.println s!"FAIL wide_fast: {fails.size} of {n} cases"
  return 1
