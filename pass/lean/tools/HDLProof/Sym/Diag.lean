/-
`Sym.Diag` -- diagnostics for the symbolic routes (untrusted: no theorems, never part of a
verdict).

* `programCensus`: every primitive, `ite`, `caseT`, `ctorT` and `call` in an fp residual, by kind.
* `nodeCensus`: every operator of a certificate's nodes (what the ir and vc steps walk), and for
  `Op_GetMask` / `Op_SetMask` whether the mask is a constant source.
* `diagStepFP`: the fp symbolic step again, counting how each primitive, `ite` and `caseT` was
  taken -- all operands concrete, or symbolic (with the operand kinds), or refused. It mirrors
  `symEvalA`'s control flow and calls the same verified helpers (`symPrim`, `merge`, `cutLet`,
  `ctorView`), so its result must equal the real step's; `sym_probe` checks that (drift check).
-/
import HDLProof.Sym.Step

open Projection Compiler
namespace HDLProof.Sym.Diag
open Std.Tactic.BVDecide

/-- A counter table: key ↦ count. -/
abbrev Counts := Std.HashMap String Nat

def bump (c : Counts) (k : String) (n : Nat := 1) : Counts := c.insert k (c.getD k 0 + n)

/-- A primitive's short name (`mulI`, `bvGetMask`, ...). -/
def primName (p : Prim) : String :=
  let s := reprStr p
  match s.splitOn "." with
  | [] => s
  | parts => parts.getLast!

/-! ## Static census -/

partial def census (c : Counts) : Term → Counts
  | .lit _ => c
  | .var _ => c
  | .letIn a b => census (census c a) b
  | .ite x a b => census (census (census (bump c "ite") x) a) b
  | .prim p ts => ts.foldl census (bump c s!"prim {primName p}")
  | .ctorT _ ts => ts.foldl census (bump c "ctorT")
  | .caseT s as => as.foldl (fun c a => census c a.2.2) (census (bump c "caseT") s)
  | .call _ ts => ts.foldl census (bump c "call")

def programCensus (R : Program) : Counts := R.funs.foldl (fun c f => census c f.body) {}

/-- An operator's short name. -/
def opName (op : LGraphOp) : String :=
  let s := reprStr op
  match (s.splitOn " ").head?.map (·.splitOn ".") with
  | some parts => parts.getLast!
  | none => s

/-- Whether slot `s` holds a constant: a `const` source (constants are sources, never nodes). -/
def slotConst (D : DesignCert) (s : Nat) : Bool :=
  match D.sources[s]? with
  | some (.const _ _) => true
  | _ => false

def nodeCensus (D : DesignCert) : Counts := Id.run do
  let mut c : Counts := {}
  for n in D.nodes do
    c := bump c s!"op {opName n.op}"
    match n.op with
    | .Op_GetMask | .Op_SetMask =>
        let m := n.deps[1]?
        let k := match m with
          | some s => if slotConst D s then "const" else "nonconst"
          | none => "missing"
        c := bump c s!"op {opName n.op} mask:{k}"
    | _ => pure ()
  return c

/-! ## The counting step (fp) -/

/-- The kind of a symbolic value, for the counters. -/
def kindOf : SVal → String
  | .conc _ => "conc"
  | .int _ => "int"
  | .bool _ => "bool"
  | .bv _ _ => "bv"
  | .cons _ _ => "cons"
  | .ctor _ _ => "ctor"
  | .nbv _ _ => "nbv"
  | .alt _ _ _ => "alt"

def kindsOf (ss : List SVal) : String := ",".intercalate (ss.map kindOf)

/-- The primitives whose symbolic support is reported operand by operand. -/
def detailed : Prim → Bool
  | .mulI | .divI | .modI | .eqV | .mkCtorP | .ctorTagP | .ctorFieldsP | .bvBit | .bvGetMask
  | .bvMk | .bvResize => true
  | _ => false

mutual

partial def evalD (cnt : IO.Ref Counts) : Nat → SymSt → Array SVal → Term →
    IO (Except String (SVal × SymSt × Array SVal))
  | 0, _, _, _ => pure (.error "symbolic evaluation: out of fuel")
  | n + 1, st, env, t => do
    match t with
    | .lit v => pure (.ok (.conc v, st, env))
    | .var i =>
        match envGet env i with
        | some s => pure (.ok (s, st, env))
        | none   => pure (.error s!"unbound de Bruijn index {i}")
    | .letIn e b =>
        match ← evalD cnt n st env e with
        | .ok (s, st1, env1) =>
            match ← evalD cnt n (cutLet st1 e s).2 (env1.push (cutLet st1 e s).1) b with
            | .ok (r, st2, env2) => pure (.ok (r, st2, env2.pop))
            | .error m => pure (.error m)
        | .error m => pure (.error m)
    | .ite c a b =>
        match ← evalD cnt n st env c with
        | .ok (.conc (.bool true), st1, env1)  => cnt.modify (bump · "ite conc"); evalD cnt n st1 env1 a
        | .ok (.conc (.bool false), st1, env1) => cnt.modify (bump · "ite conc"); evalD cnt n st1 env1 b
        | .ok (.bool ce, st1, env1) =>
            match ← evalD cnt n st1 env1 a with
            | .ok (sa, st2, env2) =>
                match ← evalD cnt n st2 env2 b with
                | .ok (sb, st3, env3) =>
                    match mergeA ce sa sb with
                    | some m =>
                        let k := match m with
                          | .alt _ _ _ => s!"ite sym union {kindOf sa}|{kindOf sb}"
                          | _ => "ite sym merged"
                        cnt.modify (bump · k); pure (.ok (m, st3, env3))
                    | none   =>
                        cnt.modify (bump · s!"ite sym REFUSED {kindOf sa}|{kindOf sb}")
                        pure (.error "ite: resource-limit: guarded union exceeds 64 leaves")
                | .error m => pure (.error m)
            | .error m => pure (.error m)
        | .ok (s, _, _) =>
            cnt.modify (bump · s!"ite cond REFUSED {kindOf s}")
            pure (.error "ite: the condition is not a Boolean")
        | .error m => pure (.error m)
    | .prim p ts =>
        match ← evalListD cnt n st env ts with
        | .ok (ss, st1, env1) =>
            let r := symPrim p ss
            let nm := primName p
            let how := if (allConc ss).isSome then "conc" else "sym"
            let res := match r with | .ok _ => "ok" | .error _ => "REFUSED"
            cnt.modify (bump · s!"prim {nm} {how} {res}")
            if how == "sym" && detailed p then
              cnt.modify (bump · s!"prim {nm} sym {res} [{kindsOf ss}]")
            if ss.any (fun | .alt _ _ _ => true | _ => false) then
              cnt.modify (bump · s!"prim {nm} union-operand {res}")
            match r with
            | .ok s    => pure (.ok (s, st1, env1))
            | .error m => pure (.error m)
        | .error m => pure (.error m)
    | .ctorT k ts =>
        match ← evalListD cnt n st env ts with
        | .ok (ss, st1, env1) => pure (.ok (mkCtorS k ss, st1, env1))
        | .error m           => pure (.error m)
    | .caseT s alts =>
        match ← evalD cnt n st env s with
        | .ok (sv, st1, env1) => caseAltD cnt n alts 6 st1 env1 sv
        | .error m => pure (.error m)
    | .call _ _ => pure (.error "call: a residual is call-free")

/-- `caseLeaf`, counting. -/
partial def caseLeafD (cnt : IO.Ref Counts) (n : Nat) (alts : List Alt) (st : SymSt)
    (env : Array SVal) (sv : SVal) : IO (Except String (SVal × SymSt × Array SVal)) := do
  match ctorView sv with
  | some (tag, fs) =>
      cnt.modify (bump · s!"caseT tag known ({kindOf sv})")
      match findAlt alts tag with
      | some a =>
          if a.arity = fs.length then
            match ← evalD cnt n st (envPushL env fs.reverse) a.body with
            | .ok (r, st2, env2) => pure (.ok (r, st2, envPopN env2 fs.length))
            | .error m => pure (.error m)
          else pure (.error "caseT: arity mismatch")
      | none => pure (.error "caseT: no alternative")
  | none =>
      cnt.modify (bump · s!"caseT REFUSED {kindOf sv}")
      pure (.error "caseT: symbolic-tag: the scrutinee's tag is symbolic")

/-- `caseAlt`, counting the guarded unions it splits. -/
partial def caseAltD (cnt : IO.Ref Counts) (n : Nat) (alts : List Alt) : Nat → SymSt → Array SVal → SVal →
    IO (Except String (SVal × SymSt × Array SVal))
  | 0, st, env, sv => caseLeafD cnt n alts st env sv
  | d + 1, st, env, sv => do
    match sv with
    | .alt c a b =>
        cnt.modify (bump · "caseT union split")
        match ← caseAltD cnt n alts d st env a with
        | .ok (ra, st1, env1) =>
            match ← caseAltD cnt n alts d st1 env1 b with
            | .ok (rb, st2, env2) =>
                match mergeA c ra rb with
                | some m => pure (.ok (m, st2, env2))
                | none => pure (.error "caseT: resource-limit: guarded union exceeds 64 leaves")
            | .error m => pure (.error m)
        | .error m => pure (.error m)
    | _ => caseLeafD cnt n alts st env sv

partial def evalListD (cnt : IO.Ref Counts) : Nat → SymSt → Array SVal → List Term →
    IO (Except String (List SVal × SymSt × Array SVal))
  | _, st, env, [] => pure (.ok ([], st, env))
  | n, st, env, t :: ts => do
      match ← evalD cnt n st env t with
      | .ok (s, st1, env1) =>
          match ← evalListD cnt n st1 env1 ts with
          | .ok (ss, st2, env2) => pure (.ok (s :: ss, st2, env2))
          | .error m           => pure (.error m)
      | .error m => pure (.error m)

end

/-- `symStepFP`, counting. -/
def diagStepFP (R : Program) (fuel : Nat) (st : SymSt) (sE sI sS : SVal) :
    IO (Except String (SVal × SymSt) × Counts) := do
  let cnt ← IO.mkRef ({} : Counts)
  let r ← match R.fn R.entry with
    | some fd =>
        if fd.arity = 3 then do
          match ← evalD cnt fuel st [sE, sI, sS].reverse.toArray fd.body with
          | .ok (s, st', _) => pure (.ok (s, st'))
          | .error m => pure (.error m)
        else pure (.error "the residual entry does not take three arguments")
    | none => pure (.error "the residual has no entry function")
  return (r, ← cnt.get)

/-- A refusal message's class, for surveys. -/
def refusalClass (m : String) : String :=
  let has (s : String) := (m.splitOn s).length > 1
  if has "resource-limit" then "resource-limit"
  else if has "unnormalized-bv" then "unnormalized-bv"
  else if has "data-dependent-width" then "data-dependent-width"
  else if has "unsupported operand shapes" || has "native symbolic operation refused" then "unsupported"
  else if has "differ in shape" then "shape"
  else if has "tag is symbolic" || has "symbolic-tag" then "symbolic-tag"
  else if has "condition is not a Boolean" then "shape"
  else if has "out of fuel" then "fuel"
  else if has "compileDesign refused" || has "specialization failed" || has "checkResidual"
      || has "does not admit" then "setup"
  else "other"

end HDLProof.Sym.Diag
