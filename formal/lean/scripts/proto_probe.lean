/-
Bounded A/B of the REAL `projectDesign` pipeline against the diagnostic fork.

  reference   Projection.Hw.projectDesign      (the proved specializer)
  fast        Projection.ProtoFast.mixDriver   (same driver, PRes.val added)

Both produce the SHARED `Program`, so the residuals are compared directly with
`==`, and then run through `evalFuel` on the same stimulus so the comparison is
semantic and not only syntactic.

TIMING: use the `--ref` / `--fast` single-side modes and time the PROCESS from
outside.  In-process timestamps are not trustworthy here -- Lean reorders pure
work across `IO.monoMsNow`, which is what made the earlier microbenchmark read
0 ms while the work was really being done outside the interval (verified in the
generated C).
-/
import LeanSemanticPrimitives.Projection.ProjectedStep
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast

open Compiler Projection

def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b      => 1 + tsize a + tsize b
  | .ite a b c      => 1 + tsize a + tsize b + tsize c
  | .caseT s as     => 1 + tsize s + (as.map (fun a => tsize a.2.2)).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts => 1 + (ts.map tsize).foldl (·+·) 0

def szOf (R : Program) : Nat := (R.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0

@[inline] def refOf  (n : Nat) : Except MixError Program := Hw.projectDesign (chainD n)
@[inline] def fastOf (n : Nat) : Except ProtoFast.MixError Program :=
  ProtoFast.mixDriver 20000 200 Hw.hwAP [encDesign (chainD n)]

/-- Run a residual on one cycle of stimulus and decode, so the A/B comparison
is semantic.  Fuel is generous and explicit: this is a diagnostic. -/
def runOne (R : Program) (D : DesignCert) : Option RuntimeResult :=
  let e := allEdges D
  let i : RuntimeInput := #[mk_bv 4 5]
  let s : RuntimeState := { flops := #[], mems := #[] }
  match evalFuel 2000000 R []
      (.call R.entry [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | .value v => decResult v
  | _        => none

def sameResult : Option RuntimeResult → Option RuntimeResult → Bool
  | some a, some b => encResult a == encResult b
  | _,      _      => false

def compare (n : Nat) : IO Unit := do
  match refOf n, fastOf n with
  | .ok R, .ok F =>
      let syn := R == F
      let sem := sameResult (runOne R (chainD n)) (runOne F (chainD n))
      IO.println s!"n={n}  ref size {szOf R}  fast size {szOf F}  \
identical {syn}  same-result {sem}  entry-arity ref {(R.fn R.entry).map FunDef.arity} \
fast {(F.fn F.entry).map FunDef.arity}"
  | .error _, _ => IO.println s!"n={n}  REFERENCE failed"
  | _, .error _ => IO.println s!"n={n}  FAST failed"

def main (args : List String) : IO UInt32 := do
  match args with
  | "--ref" :: a :: _ =>
      match a.toNat? with
      | some n => do
          match refOf n with
          | .ok R    => IO.println s!"ref  n={n} size {szOf R}"
          | .error _ => IO.println s!"ref  n={n} FAILED"
          return 0
      | none => return 64
  | "--fast" :: a :: _ =>
      match a.toNat? with
      | some n => do
          match fastOf n with
          | .ok R    => IO.println s!"fast n={n} size {szOf R}"
          | .error _ => IO.println s!"fast n={n} FAILED"
          return 0
      | none => return 64
  | _ => do
      for a in args do
        match a.toNat? with | some n => compare n | none => pure ()
      return 0
