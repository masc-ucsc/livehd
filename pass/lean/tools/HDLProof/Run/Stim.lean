/-
# Stimulus and edge files, as ticks

The `stim v1` and `edges v1` formats that `cert_trace` reads (and the legacy driver, and
`dcert2_memroute` for `stim v1`), as a library, so a driver can turn a file into the `Tick` list
`Compiler.Direct.refTrace` takes. The readers and the per-cycle rules are `cert_trace`'s, copied
unchanged (cert_trace keeps its own copy):

* `stim v1`: `in <ordinal> <width> <value>` (every cycle) and `at <cycle> in <ordinal> <width>
  <value>` (that cycle wins); an unlisted input is 0 at its declared width.
* `edges v1`: `at <cycle> <b0> <b1> ...`, one bit per clock ordinal; a cycle not listed fires
  every clock. Without a file, `all` fires every clock on every cycle and `third` makes every
  clock quiet on cycles `k % 3 = 2`.

Cycle numbers are absolute: the tick of cycle `k` is the same whether a run starts at 0 or
resumes at a checkpoint before `k`.
-/
import LeanSemanticPrimitives.Compiler.DirectTrace
import Std.Data.HashMap

namespace HDLProof
namespace Run

open Compiler Compiler.Direct

structure Stim where
  every : List (Nat × Nat × Int) := []
  at_   : List (Nat × Nat × Nat × Int) := []
deriving Inhabited

def parseStim (txt : String) : Except String Stim := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \"stim v1\", got {repr hdr}"
    -- in file order, pushed to arrays (appending to a list copied it: quadratic)
    let mut ats : Array (Nat × Nat × Nat × Int) := #[]
    let mut evs : Array (Nat × Nat × Int) := #[]
    for l in rest do
      match (l.splitOn " ").filter (· != "") with
      | ["at", c, "in", i, w, v] =>
          match c.toNat?, i.toNat?, w.toNat?, v.toInt? with
          | some c, some i, some w, some v => ats := ats.push (c, i, w, v)
          | _, _, _, _ => throw s!"unparseable {repr l}"
      | ["in", i, w, v] =>
          match i.toNat?, w.toNat?, v.toInt? with
          | some i, some w, some v => evs := evs.push (i, w, v)
          | _, _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"
    pure { every := evs.toList, at_ := ats.toList }

/-- The declared width of input ordinal `i` (from its `SourceDesc.input`), or 1. -/
def inputWidth (D : DesignCert) (i : Nat) : Nat :=
  (D.sources.toList.findSome? fun
    | .input idx w => if idx == i then some w else none
    | _ => none).getD 1

def maxInput (D : DesignCert) : Nat :=
  D.sources.foldl (fun m s => match s with
    | .input idx _ => max m (idx + 1)
    | .flopQAsync _ _ ri _ _ => max m (ri + 1)
    | _ => m) 0

def inputAt (D : DesignCert) (st : Stim) (k : Nat) : RuntimeInput :=
  (List.range (maxInput D)).toArray.map fun i =>
    match st.at_.find? (fun t => t.1 == k && t.2.1 == i) with
    | some (_, _, w, v) => mk_bv w v
    | none =>
      match st.every.find? (fun t => t.1 == i) with
      | some (_, w, v) => mk_bv w v
      | none           => mk_bv (inputWidth D i) 0

/-- The stimulus as tables, built once per run: each input's value on a cycle no `at` line
names it (its first `in` line, else 0 at its declared width), and the `at` lines grouped by
cycle, in file order. `StimTab.inputAt` gives `inputAt`'s inputs (the same first-match rules)
without scanning the stimulus and the design's sources on every cycle
(`tests/stim_tab_check.lean` checks the two against each other). cert_trace keeps a copy. -/
structure StimTab where
  base : RuntimeInput
  at_  : Std.HashMap Nat (Array (Nat × Nat × Int))

def StimTab.of (D : DesignCert) (st : Stim) : StimTab := Id.run do
  let n := maxInput D
  -- each ordinal's width: its first `SourceDesc.input` (else 1)
  let mut width : Array (Option Nat) := Array.replicate n none
  for s in D.sources do
    match s with
    | .input idx w => if idx < n && width[idx]!.isNone then width := width.set! idx (some w)
    | _ => pure ()
  -- each ordinal's value when no `at` line names it: its first `in` line
  let mut every : Array (Option (Nat × Int)) := Array.replicate n none
  for (i, w, v) in st.every do
    if i < n && every[i]!.isNone then every := every.set! i (some (w, v))
  let base := (Array.range n).map fun i =>
    match every[i]! with
    | some (w, v) => mk_bv w v
    | none => mk_bv (width[i]!.getD 1) 0
  let mut at_ : Std.HashMap Nat (Array (Nat × Nat × Int)) := {}
  for (c, i, w, v) in st.at_ do
    at_ := at_.insert c ((at_.getD c #[]).push (i, w, v))
  return { base, at_ }

/-- Cycle `k`'s inputs: an input's first `at` line for cycle `k` wins, else its base value. -/
def StimTab.inputAt (t : StimTab) (k : Nat) : RuntimeInput :=
  match t.at_.get? k with
  | none => t.base
  | some ls => ls.foldr (fun (i, w, v) acc => if i < acc.size then acc.set! i (mk_bv w v) else acc) t.base

/-- An `edges v1` schedule: `at <cycle> <b0> <b1> ...`, one bit per clock ordinal. -/
def parseEdges (txt : String) : Except String (List (Nat × Array Bool)) := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty edges file"
  | hdr :: rest =>
    if hdr != "edges v1" then .error s!"line 1: expected \"edges v1\", got {repr hdr}"
    rest.mapM fun l =>
      match (l.splitOn " ").filter (· != "") with
      | "at" :: c :: bits =>
          match c.toNat?, bits.mapM (fun b => match b with | "0" => some false | "1" => some true | _ => none) with
          | some c, some bs => pure (c, bs.toArray)
          | _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"

/-- Which clocks fire on a cycle: a schedule file, else `third` or every clock. -/
inductive EdgeSpec where
  | all
  | third
  | file (rows : List (Nat × Array Bool))
deriving Inhabited

def edgesAt (D : DesignCert) (e : EdgeSpec) (k : Nat) : ClockEdges :=
  match e with
  | .file rows =>
    match rows.find? (fun r => r.1 == k) with
    | some (_, bs) => (Array.range D.clocks.size).map (fun c => bs.getD c true)
    | none => Array.replicate D.clocks.size true
  | .third =>
    if k % 3 == 2 then Array.replicate D.clocks.size false
    else Array.replicate D.clocks.size true
  | .all => Array.replicate D.clocks.size true

/-- The ticks of cycles `start .. start + n - 1`. -/
def ticks (D : DesignCert) (st : Stim) (e : EdgeSpec) (start n : Nat) : List Tick :=
  (List.range' start n).map fun k => { edges := edgesAt D e k, input := inputAt D st k }

/-- `edges:all`, `edges:third` or `edges:file:<path>`, read. -/
def readEdgeSpec (opt : String) : IO (Except String EdgeSpec) := do
  if opt == "edges:all" then return .ok .all
  if opt == "edges:third" then return .ok .third
  if opt.startsWith "edges:file:" then
    let path := opt.replace "edges:file:" ""
    match parseEdges (← IO.FS.readFile path) with
    | .ok rows => return .ok (.file rows)
    | .error e => return .error s!"{path}: {e}"
  return .error s!"unknown edge option {repr opt}"

end Run
end HDLProof
