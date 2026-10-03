/-
Bounded A/B of the REAL `projectDesign` pipeline against the diagnostic fork.

  reference   Projection.Hw.projectDesign      (the proved specializer)
  fast        Projection.ProtoFast.mixDriver   (same driver, two changes)

Both produce the SHARED `Program`, so residuals are compared with `==`, and
then run through `evalFuel` on the same stimulus so the comparison is semantic
and not only syntactic.

EXIT STATUS IS MEANINGFUL: 0 only when every case compared equal.  1 = a
residual or result mismatch; 2 = one side failed to specialize.  An earlier
version printed FAILED and still returned 0.

TIMING: use `--ref` / `--fast` single-side modes and time the PROCESS from
outside.  In-process timestamps are not trustworthy here -- Lean reorders pure
work across `IO.monoMsNow`, verified in the generated C.
-/
import LeanSemanticPrimitives.Projection.ProjectedStep
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast
import LeanSemanticPrimitives.Compiler.CertIO

open Compiler Projection

/-- Node `i` reads its predecessor (depth 0) and source 0 (deep). -/
def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- EVERY dep at maximum depth -- the worst case for slot reads. -/
def fanD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun _ =>
                { op := .Op_And, width := 4, deps := #[0, 1] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- SEQUENTIAL: a flop on every node, so the edge vector and the flop-commit
path are exercised too, not only the combinational environment. -/
def flopD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := (List.range n).toArray.map (fun i =>
                { width := 4, din := 2 + i, enable := none, resetPin := none
                , resetValue := 0, resetActiveLow := false })
  memories := #[]

def shapeOf : String → Nat → Option DesignCert
  | "chain", n => some (chainD n)
  | "fan",   n => some (fanD n)
  | "flop",  n => some (flopD n)
  | _,       _ => none

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b      => 1 + tsize a + tsize b
  | .ite a b c      => 1 + tsize a + tsize b + tsize c
  | .caseT s as     => 1 + tsize s + (as.map (fun a => tsize a.2.2)).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts => 1 + (ts.map tsize).foldl (·+·) 0

def szOf (R : Program) : Nat := (R.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0

@[inline] def refOf  (D : DesignCert) : Except MixError Program := Hw.projectDesign D
@[inline] def fastOf (D : DesignCert) : Except ProtoFast.MixError Program :=
  ProtoFast.mixDriver 20000 200 Hw.hwAP [encDesign D]

/-- Same, with the fuel overridden -- a DIAGNOSTIC knob, to tell "ran out of
the hardcoded fuel" apart from any other failure.  `projectDesign` itself is
unchanged. -/
@[inline] def fastOfFuel (sf wf : Nat) (D : DesignCert) : Except ProtoFast.MixError Program :=
  ProtoFast.mixDriver sf wf Hw.hwAP [encDesign D]

/-- One cycle, decoded, so the A/B comparison is semantic.  A sequential design
needs a correctly sized state, so the state is built from the design. -/
def runOne (R : Program) (D : DesignCert) : Option RuntimeResult :=
  let e := allEdges D
  let i : RuntimeInput := #[mk_bv 4 5, mk_bv 4 9]
  let s : RuntimeState := { flops := D.flops.map (fun f => mk_bv f.width 3), mems := #[] }
  match evalFuel 4000000 R []
      (.call R.entry [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | .value v => decResult v
  | _        => none

def sameResult : Option RuntimeResult → Option RuntimeResult → Bool
  | some a, some b => encResult a == encResult b
  | none,   none   => false          -- neither produced a value: NOT agreement
  | _,      _      => false

/-- returns 0 ok, 1 mismatch, 2 a side failed -/
def compare (shape : String) (D : DesignCert) (n : Nat) : IO UInt32 := do
  match refOf D, fastOf D with
  | .ok R, .ok F =>
      let syn := R == F
      let ra := runOne R D
      let fa := runOne F D
      let sem := sameResult ra fa
      IO.println s!"{shape} n={n}  ref size {szOf R}  fast size {szOf F}  \
identical {syn}  same-result {sem}  ref-ran {ra.isSome}  fast-ran {fa.isSome}"
      if syn && sem then return 0 else do
        IO.eprintln s!"MISMATCH on {shape} n={n}: identical={syn} same-result={sem}"
        return 1
  | .error _, _ => do IO.eprintln s!"FAILED: reference on {shape} n={n}"; return 2
  | _, .error _ => do IO.eprintln s!"FAILED: fast on {shape} n={n}"; return 2

def one (shape : String) (n : Nat) (side : String) : IO UInt32 := do
  match shapeOf shape n with
  | none => do IO.eprintln s!"unknown shape {shape}"; return 64
  | some D =>
      if side == "ref" then
        match refOf D with
        | .ok R    => do IO.println s!"ref  {shape} n={n} size {szOf R}"; return 0
        | .error _ => do IO.eprintln s!"ref  {shape} n={n} FAILED"; return 2
      else if side == "fast" then
        match fastOf D with
        | .ok R    => do IO.println s!"fast {shape} n={n} size {szOf R}"; return 0
        | .error _ => do IO.eprintln s!"fast {shape} n={n} FAILED"; return 2
      else compare shape D n

/-- Load a real DCERT1 certificate and specialize it, one side or both. -/
def onFile (path : String) (side : String) : IO UInt32 := do
  let D ← CertIO.loadCert path
  IO.println s!"{path}: sources {D.sources.size} nodes {D.nodes.size} \
outputs {D.outputs.size} flops {D.flops.size} memories {D.memories.size}"
  if side == "ref" then
    match refOf D with
    | .ok R    => do IO.println s!"ref  size {szOf R}"; return 0
    | .error _ => do IO.eprintln "ref  FAILED"; return 2
  else if side == "fast" then
    match fastOf D with
    | .ok R    => do IO.println s!"fast size {szOf R}"; return 0
    | .error e => do IO.eprintln s!"fast FAILED ({repr e})"; return 2
  else compare path D D.nodes.size

def onFileFuel (path : String) (sf wf : Nat) : IO UInt32 := do
  let D ← CertIO.loadCert path
  IO.println s!"{path}: nodes {D.nodes.size}  (fast, stepFuel {sf} wlFuel {wf})"
  match fastOfFuel sf wf D with
  | .ok R    => do IO.println s!"fast size {szOf R}"; return 0
  | .error e => do IO.eprintln s!"fast FAILED ({repr e})"; return 2

def main (args : List String) : IO UInt32 := do
  match args with
  | "--file-fuel" :: p :: a :: b :: _ =>
      onFileFuel p ((a.toNat?).getD 20000) ((b.toNat?).getD 200)
  | "--file"      :: p :: _  => onFile p "cmp"
  | "--file-ref"  :: p :: _  => onFile p "ref"
  | "--file-fast" :: p :: _  => onFile p "fast"
  | "--ref"  :: sh :: a :: _ => one sh ((a.toNat?).getD 0) "ref"
  | "--fast" :: sh :: a :: _ => one sh ((a.toNat?).getD 0) "fast"
  | sh :: rest => do
      let mut rc : UInt32 := 0
      for a in rest do
        match a.toNat? with
        | some n => do let r ← one sh n "cmp"; if r != 0 then rc := r
        | none   => pure ()
      if rc != 0 then IO.eprintln s!"EXIT {rc}: at least one case did not match"
      return rc
  | [] => do IO.eprintln "usage: proto_probe [--ref|--fast] <chain|fan|flop> <n>..."; return 64
