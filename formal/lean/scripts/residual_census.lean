/-
`residual_census` -- M3 stage S0: what a projection residual is made of.

    lean --run scripts/residual_census.lean <cert> <stepFuel> <workFuel> [route:scalar|mem]

Specializes the certificate on the chosen route (scalar: `mixDriver` on
`hwAPVarT`; mem: `projectMemDesignAt`) and walks the residual ITERATIVELY
(an explicit stack: residuals reach millions of terms). Reports what a
symbolic interpreter of the residual has to handle:

  * term kinds (lit, var, letIn, ite, prim, ctorT, caseT, call);
  * the primitive histogram, with the ones that act on SYMBOLIC operands
    singled out (bit-vector and integer arithmetic, comparisons) from the
    environment plumbing (consP/hd/tl/isNil);
  * dynamic `ite`s (every residual `ite` is dynamic: static ones were decided
    during specialization), `caseT` tags, the deepest `let` spine and the
    largest de Bruijn index;
  * `eqV` and `divI`/`modI`, which a bit-blaster must bound.

It runs the SPECIALIZER only; nothing is executed.
-/
import LeanSemanticPrimitives.Projection.CertLoadAny
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport

open Compiler Projection Projection.Hw Projection.MemProjection Projection.ProtoVar

structure Census where
  lit : Nat := 0
  var : Nat := 0
  letIn : Nat := 0
  ite : Nat := 0
  prim : Nat := 0
  ctorT : Nat := 0
  caseT : Nat := 0
  call : Nat := 0
  prims : Std.HashMap String Nat := {}
  caseTags : Std.HashMap Nat Nat := {}
  ctorTags : Std.HashMap Nat Nat := {}
  maxVar : Nat := 0
  maxLetSpine : Nat := 0
  maxCaseAlts : Nat := 0

def bump {α} [BEq α] [Hashable α] (m : Std.HashMap α Nat) (k : α) : Std.HashMap α Nat :=
  m.insert k (m.getD k 0 + 1)

/-- The `let` spine: how many `letIn`s are chained through BODY position. -/
partial def letSpine (t : Term) (acc : Nat := 0) : Nat :=
  match t with
  | .letIn _ b => letSpine b (acc + 1)
  | _          => acc

def plumbing : List String := ["consP", "hd", "tl", "isNil"]

/-- `Projection.Prim.bvUint` -> `bvUint`. -/
def primName (p : Prim) : String := ((reprStr p).splitOn ".").getLastD (reprStr p)

def census (body : Term) : Census := Id.run do
  let mut c : Census := {}
  let mut stack : Array Term := #[body]
  while h : stack.size > 0 do
    let t := stack[stack.size - 1]
    stack := stack.pop
    match t with
    | .lit _ => c := { c with lit := c.lit + 1 }
    | .var i => c := { c with var := c.var + 1, maxVar := max c.maxVar i }
    | .letIn e b =>
        c := { c with letIn := c.letIn + 1, maxLetSpine := max c.maxLetSpine (letSpine t) }
        stack := (stack.push e).push b
    | .ite a b d =>
        c := { c with ite := c.ite + 1 }
        stack := ((stack.push a).push b).push d
    | .prim p ts =>
        c := { c with prim := c.prim + 1, prims := bump c.prims (primName p) }
        for x in ts do stack := stack.push x
    | .ctorT k ts =>
        c := { c with ctorT := c.ctorT + 1, ctorTags := bump c.ctorTags k }
        for x in ts do stack := stack.push x
    | .caseT s alts =>
        c := { c with caseT := c.caseT + 1, maxCaseAlts := max c.maxCaseAlts alts.length }
        for a in alts do c := { c with caseTags := bump c.caseTags a.1 }
        stack := stack.push s
        for a in alts do stack := stack.push a.2.2
    | .call _ ts =>
        c := { c with call := c.call + 1 }
        for x in ts do stack := stack.push x
  return c


def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: sfS :: wfS :: rest =>
    let some sf := sfS.toNat? | IO.eprintln "stepFuel"; return 2
    let some wf := wfS.toNat? | IO.eprintln "workFuel"; return 2
    let mem := rest.contains "route:mem"
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    IO.println s!"{cert}"
    IO.println s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
    let t0 ← IO.monoMsNow
    let res ← IO.lazyPure (fun _ =>
      if mem then projectMemDesignAt sf wf D else mixDriver sf wf hwAPVarT [encDesign D])
    let t1 ← IO.monoMsNow
    match res with
    | .error e => IO.eprintln s!"  specialization failed: {repr e}"; return 3
    | .ok R =>
      IO.println s!"  route {if mem then "mem (hwMemAP)" else "scalar (hwAPVarT)"}, specialize {t1 - t0} ms, \
{R.funs.length} function(s), arities {R.funs.map (·.arity)}"
      for (fd, k) in R.funs.zipIdx do
        let c := census fd.body
        let total := c.lit + c.var + c.letIn + c.ite + c.prim + c.ctorT + c.caseT + c.call
        IO.println s!"  fun {k}: {total} terms -- lit {c.lit} var {c.var} letIn {c.letIn} ite {c.ite} \
prim {c.prim} ctorT {c.ctorT} caseT {c.caseT} call {c.call}"
        IO.println s!"    let spine {c.maxLetSpine}, max de Bruijn index {c.maxVar}, widest caseT {c.maxCaseAlts}"
        let ps := c.prims.toList.toArray.qsort (fun a b => a.2 > b.2)
        let (plumb, symb) := ps.partition (fun p => plumbing.contains p.1)
        IO.println s!"    symbolic-operand prims: {symb.toList.map (fun p => s!"{p.1}={p.2}")}"
        IO.println s!"    plumbing prims:         {plumb.toList.map (fun p => s!"{p.1}={p.2}")}"
        IO.println s!"    caseT tags {c.caseTags.toList}  ctorT tags {c.ctorTags.toList}"
      return 0
  | _ =>
    IO.eprintln "usage: residual_census <cert> <stepFuel> <workFuel> [route:scalar|mem]"
    return 2
