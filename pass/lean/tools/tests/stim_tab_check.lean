/-
Tests for the stimulus readers' speed changes (`HDLProof/Run/Stim.lean`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/stim_tab_check.lean)

`StimTab.inputAt` (a table built once) must give `inputAt`'s inputs on every cycle, and
`parseStim` (lines pushed to arrays) must give the lists the earlier reader built by appending.
Both are checked on pseudo-random stimuli over designs whose input ordinals have gaps, repeats
with other widths (the first declaration wins), and a reset ordinal no `input` source declares
(width 1); the stimuli repeat inputs within a cycle and across `in` lines (the first line
wins), name ordinals past the last input, and use negative and wide values. Every trace driver
(cert_trace, cert_run, the per-design TV and legacy `--trace` drivers, dcert2_memroute) reads
stimuli through these; the legacy generator's older `--stim` mode writes its own copy.

Prints `PASS stim_tab: <n> cases` or one `FAIL` line per case that went wrong.
-/
import HDLProof.Run.Stim

open Compiler HDLProof.Run

/-- The reader before the change: each line appended to its list. -/
def parseStimOld (txt : String) : Except String Stim := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \"stim v1\", got {repr hdr}"
    let mut st : Stim := {}
    for l in rest do
      match (l.splitOn " ").filter (· != "") with
      | ["at", c, "in", i, w, v] =>
          match c.toNat?, i.toNat?, w.toNat?, v.toInt? with
          | some c, some i, some w, some v => st := { st with at_ := st.at_ ++ [(c, i, w, v)] }
          | _, _, _, _ => throw s!"unparseable {repr l}"
      | ["in", i, w, v] =>
          match i.toNat?, w.toNat?, v.toInt? with
          | some i, some w, some v => st := { st with every := st.every ++ [(i, w, v)] }
          | _, _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"
    pure st

def design (srcs : Array SourceDesc) : DesignCert :=
  { sources := srcs, nodes := #[], outputs := #[], flops := #[], memories := #[] }

/-- Ordinals 0, 2 (declared twice: 8 bits first), 5; ordinal 6 only as an async reset. -/
def dGaps : DesignCert := design #[.input 0 4, .const 8 3, .input 2 8, .input 5 70, .input 2 3,
  .flopQAsync 0 4 6 0 false]
/-- One wide input. -/
def dWide : DesignCert := design #[.input 0 130]
/-- No inputs at all. -/
def dNone : DesignCert := design #[.const 1 0]

/-- A pseudo-random generator (64-bit LCG). -/
def next (x : Nat) : Nat := (x * 6364136223846793005 + 1442695040888963407) % 2 ^ 64

/-- A stimulus of `n` lines: `at` and `in` lines over ordinals `0 .. ords - 1` (past the
design's last input too), cycles `0 .. cyc - 1`, widths 1 .. 140, values of either sign. -/
def genStim (seed n ords cyc : Nat) : String := Id.run do
  let mut x := seed
  let mut out := "stim v1\n# generated\n\n"
  for _ in [0:n] do
    x := next x; let kind := x % 5
    x := next x; let c := x % cyc
    x := next x; let i := x % ords
    x := next x; let w := 1 + x % 140
    x := next x; let mag := x % 2 ^ (x % 150)
    x := next x; let v : Int := if x % 3 == 0 then -(mag : Int) else mag
    if kind == 0 then out := out ++ s!"in {i} {w} {v}\r\n"
    else out := out ++ s!"at {c} in {i} {w} {v}\n"
  return out

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  for (dn, D) in [("gaps", dGaps), ("wide", dWide), ("none", dNone)] do
    for seed in [1, 2, 3, 4, 5, 6, 7, 8] do
      for (lines, ords) in [(0, 3), (5, 3), (60, 9), (400, 9), (2000, 4)] do
        let txt := genStim (seed * 7919 + lines) lines ords 40
        n := n + 1
        match parseStim txt, parseStimOld txt with
        | .ok st, .ok st0 =>
          if !(st.every == st0.every && st.at_ == st0.at_) then
            fails := fails.push s!"{dn} seed {seed} lines {lines}: parseStim differs from the old reader"
          let t := StimTab.of D st
          for k in [0:45] do
            n := n + 1
            if t.inputAt k != inputAt D st k then
              fails := fails.push s!"{dn} seed {seed} lines {lines} cycle {k}: StimTab.inputAt differs"
        | .error e, .error e0 =>
          if e != e0 then fails := fails.push s!"{dn} seed {seed}: errors differ: {e} / {e0}"
        | _, _ => fails := fails.push s!"{dn} seed {seed} lines {lines}: one reader refused"
  -- the readers refuse the same malformed files with the same message
  for bad in ["", "stim v2\n", "stim v1\nat x in 0 1 1\n", "stim v1\nin 0 1\n", "stim v1\nfoo\n"] do
    n := n + 1
    match parseStim bad, parseStimOld bad with
    | .error e, .error e0 => if e != e0 then fails := fails.push s!"refusal differs: {e} / {e0}"
    | _, _ => fails := fails.push s!"malformed {repr bad} not refused by both"
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS stim_tab: {n} cases"
    return 0
  return 1
