/-
Differential test of FastBV's GetMask fast form against GetMask's definition.

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/getmask_fast_check.lean)

`bv_get_mask_eq_fast` already PROVES `bv_get_mask = bvGetMaskFast` for every input; this is a
check that the compiled code agrees too. `bv_get_mask` below runs the fast form (the `@[csimp]`
swap applies to code compiled after FastBV); `spec` is the definition's own body, the per-bit
pack of `Translation/LGraphModel.lean`. Inputs: masks that are zero, single bits, runs of
ones (below, across and past bit 63, up to the mask's width), two runs, and random bits;
masks narrower and wider than the value; result widths narrower and wider than the run.

Prints `PASS getmask_fast: <n> cases` or one `FAIL` line per disagreement.
-/
import LeanSemanticPrimitives.Translation.FastBV

/-- GetMask's definition, word for word. -/
def spec (w : Nat) (x m : BV) : BV := mk_bv w (pack_low_bv x (mask_indices_bv m).reverse)

/-- A value of width `wx` with a varied bit pattern up to the top bit. -/
def val (wx j : Nat) : BV := mk_bv wx ((((2 ^ wx - 1) / (j + 2)) ^^^ (j * 0x9E3779B97F4A7C15) : Nat) : Int)

def run (s L : Nat) : Nat := (2 ^ L - 1) * 2 ^ s

def masks (wm : Nat) : List Nat :=
  let cuts := [0, 1, 2, 31, 32, 62, 63, 64, 65, 127, 128, 129, wm / 2, wm - 2, wm - 1].filter (· < wm)
  let lens := [1, 2, 3, 62, 63, 64, 65, 130, wm]
  [0, 2 ^ wm - 1, run 0 1 ||| run 5 2, run 1 1 ||| run (wm - 1) 1, (2 ^ wm - 1) / 3]
    ++ cuts.map (fun s => 2 ^ s)
    ++ (cuts.flatMap fun s => lens.filterMap fun L => if s + L ≤ wm then some (run s L) else none)
    ++ (cuts.flatMap fun s => [run s 3 ||| run (s + 5) 2])

def main : IO UInt32 := do
  let mut n := 0
  let mut fails : Array String := #[]
  for wx in [1, 2, 7, 31, 33, 62, 63, 64, 65, 127, 129, 256, 505, 1024] do
    for wm in [wx, wx + 3, max 1 (wx / 2)] do
      for M in masks wm do
        let m := mk_bv wm (M : Int)
        for w in [1, 8, 64, 200] do
          for j in [0, 1] do
            let x := val wx j
            n := n + 1
            let fast := bv_get_mask w x m
            let ref := spec w x m
            if fast != ref then
              fails := fails.push s!"wx={wx} wm={wm} mask={M} w={w} j={j}: fast {repr fast} spec {repr ref}"
  for f in fails.toList.take 20 do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS getmask_fast: {n} cases"
    return 0
  IO.println s!"FAIL getmask_fast: {fails.size} of {n} cases"
  return 1
