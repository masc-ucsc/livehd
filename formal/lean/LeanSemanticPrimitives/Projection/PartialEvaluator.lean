/-
  `mix`: the specializer, as a Lean function.

  Given an annotated program `A` and values for its static parameters, produce a
  residual `Program` that computes what `A` computes once the static arguments
  are fixed.  This is the artifact the first projection applies to an
  interpreter and the second projection applies to itself.

  Three design decisions carry the file.

  1. NO LET-INSERTION, AND NO NEED FOR IT.  The plan called for a `StateM Binds`
  with residual references, the standard defence against a dynamic expression
  being duplicated at each of its uses.  It is unnecessary here because the
  program being specialized -- an LGraph interpreter -- is already in let-normal
  form: one `letIn` per graph node, every use a `var`.  So a dynamic value is
  bound exactly once by construction, and `mix` never duplicates it.  A source
  program that is NOT let-normal should be let-normalized first: that is a
  source-to-source transform with a small independent correctness proof, which
  is much cheaper than threading a binding store through `mix` and through
  `mix_sound`.

  2. ARITY REDUCTION HAPPENS ONLY AT FUNCTION BOUNDARIES.  A specialized
  function keeps just its dynamic parameters, and the `j`-th dynamic parameter
  becomes residual index `j`.  Everywhere else `mix` preserves binder structure,
  so residual indices track source indices with a single shift when -- and only
  when -- a residual binder is created.  This is why `PVal.dyn` can hold a plain
  index rather than a level plus a depth: the only place the two index spaces
  are related non-trivially is the one place a fresh environment is built
  anyway.

  3. `mixTerm` DOES NOT OWN THE MEMO TABLE.  It takes `idx : SpecRequest →
  Option Nat` and *emits* the requests it made.  The reason is
  self-application: when `mix` specializes `mix`, the inner static arguments
  come from the outer dynamic input, so the memo table is dynamic; tangling it
  into the term walk makes the whole traversal dynamic and the generated
  compiler degenerates back into the specializer.  The driver runs discovery
  and generation as two passes over the same pure `mixTerm`.

  4. EVERY DISPATCH IS ON THE DIVISION OR AN ANNOTATION, NEVER ON A COMPUTED
  RESULT.  `mixTerm` carries `Δ : Div` alongside `env : PEnv`, and decides
  "select the branch now" from `btOf Δ c` rather than from whether the
  specialized condition came back a value.  The two agree on well-annotated
  input, so this is not about correctness -- it is what makes the specializer
  self-applicable.  A test on a computed result is a test on the static VALUES,
  which are dynamic at the outer level when `mix` specializes `mix`; it would
  survive into the generated compiler and drag the whole specializer with it.
  `Δ` and the annotations come from the annotated program, which is static
  there, so those dispatches unroll away.

  SOUNDNESS DOES NOT DEPEND ON DISCOVERY BEING COMPLETE.  If the discovery pass
  misses a request, generation asks `idx` for an index it does not have and
  `mixTerm` fails with `noSpec`.  A discovery bug is therefore a loud failure,
  never a residual program that calls a function that is not there.
-/

import LeanSemanticPrimitives.Projection.BindingTime
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

namespace Projection

/-! ## Requests, partial values, errors -/

/-- A specialization request: which function, and the values of its static
parameters in order.  The *pattern* of static positions is not stored because
it is fixed by the callee's division -- binding-time analysis here is
monovariant, so `funIdx` determines it. -/
structure SpecRequest where
  funIdx     : Nat
  staticArgs : List Val
  deriving Inhabited, Repr

def SpecRequest.beq (a b : SpecRequest) : Bool :=
  a.funIdx == b.funIdx && Val.beqList a.staticArgs b.staticArgs

instance : BEq SpecRequest := ⟨SpecRequest.beq⟩

/-- What a source variable is bound to during specialization: either a value
`mix` knows, or a residual de Bruijn index. -/
inductive PVal where
  | stat : Val → PVal
  | dyn  : Nat → PVal
  deriving Inhabited, Repr

abbrev PEnv := List PVal

/-- Entering a residual binder shifts every residual index in scope.  Static
entries are untouched -- they name no residual variable. -/
def PEnv.shiftBy (k : Nat) : PEnv → PEnv
  | []              => []
  | .stat v :: rest => .stat v :: PEnv.shiftBy k rest
  | .dyn i  :: rest => .dyn (i + k) :: PEnv.shiftBy k rest

/-- The result of specializing one term. -/
inductive PRes where
  | stat : Val → PRes
  | code : Term → PRes
  deriving Inhabited, Repr

/-- Turn any result into residual code.  On a static value this is the `lift`
of the two-level language, and it is why lifting is always available: every
`Val` is a legal `Term.lit`. -/
def PRes.toCode : PRes → Term
  | .stat v => .lit v
  | .code t => t

inductive MixError where
  | outOfFuel
  | unboundVar  : Nat → MixError
  | unknownFun  : Nat → MixError
  | notStatic   : String → MixError
  | notCode     : String → MixError
  | illAnnotated : String → MixError
  | primFailed  : String → MixError
  | noSpec      : Nat → MixError
  | badArity    : String → MixError
  deriving Inhabited, Repr

/-- What `mixTerm` returns: a partial result plus every specialization the walk
asked for. -/
abbrev MixOut := PRes × List SpecRequest

/-! ## Splitting a call's arguments

At a residual call the static operands are consumed by `mix` and the dynamic
ones survive into the residual program.  The callee's division says which is
which; a mismatch between the division and what `mixTerm` produced is an
annotation error, not a recoverable case. -/

def splitArgs : Div → List PRes → Except MixError (List Val × List Term)
  | [], [] => .ok ([], [])
  | .stat :: bs, .stat v :: rs =>
      match splitArgs bs rs with
      | .ok (vs, ts) => .ok (v :: vs, ts)
      | .error e     => .error e
  | .dyn :: bs, r :: rs =>
      match splitArgs bs rs with
      | .ok (vs, ts) => .ok (vs, r.toCode :: ts)
      | .error e     => .error e
  | .stat :: _, .code _ :: _ => .error (.notStatic "call: static parameter got residual code")
  | _, _ => .error (.badArity "call: argument count does not match the division")

/-- The environment a specialized function body is specialized under: static
parameters hold their values, and the `j`-th dynamic parameter becomes residual
index `j`. -/
def buildEnv : Div → List Val → Nat → Except MixError PEnv
  | [], [], _ => .ok []
  | .stat :: bs, v :: vs, j =>
      match buildEnv bs vs j with
      | .ok rest => .ok (.stat v :: rest)
      | .error e => .error e
  | .dyn :: bs, vs, j =>
      match buildEnv bs vs (j + 1) with
      | .ok rest => .ok (.dyn j :: rest)
      | .error e => .error e
  | _, _, _ => .error (.badArity "specialize: static argument count does not match the division")

/-- How many parameters survive into the residual function. -/
def dynCount : Div → Nat
  | []          => 0
  | .dyn :: bs  => dynCount bs + 1
  | .stat :: bs => dynCount bs

/-- Every result must be a value; used by the static cases, where the
congruence rule guarantees it and a violation is an annotation error. -/
def allStatic : List PRes → Except MixError (List Val)
  | []            => .ok []
  | .stat v :: rs =>
      match allStatic rs with
      | .ok vs   => .ok (v :: vs)
      | .error e => .error e
  | .code _ :: _  => .error (.notStatic "static node has a residual operand")

/-- The residual code of each DYNAMIC argument, in source order.  These become
the `let`s an unfold wraps around the inlined body. -/
def dynArgCodes : Div → List PRes → Except MixError (List Term)
  | [], [] => .ok []
  | .stat :: bs, _ :: rs => dynArgCodes bs rs
  | .dyn :: bs, r :: rs =>
      match dynArgCodes bs rs with
      | .ok ts   => .ok (r.toCode :: ts)
      | .error e => .error e
  | _, _ => .error (.badArity "unfold: argument count does not match the division")

/-- The environment the inlined body is specialized under.

`k` is the number of dynamic arguments.  Wrapping the body in `let e₀ in let e₁
in … let e_{k-1} in ·` puts `e_{k-1}` at residual index 0, so the `j`-th dynamic
argument lands at index `k-1-j`. -/
def inlineEnv : Div → List PRes → Nat → Nat → Except MixError PEnv
  | [], [], _, _ => .ok []
  | .stat :: bs, .stat v :: rs, k, j =>
      match inlineEnv bs rs k j with
      | .ok rest => .ok (.stat v :: rest)
      | .error e => .error e
  | .stat :: _, .code _ :: _, _, _ =>
      .error (.notStatic "unfold: static parameter got residual code")
  | .dyn :: bs, _ :: rs, k, j =>
      match inlineEnv bs rs k (j + 1) with
      | .ok rest => .ok (.dyn (k - 1 - j) :: rest)
      | .error e => .error e
  | _, _, _, _ => .error (.badArity "unfold: argument count does not match the division")

/-- `let e₀ in let e₁ in … let e_{k-1} in body`. -/
def wrapLets : List Term → Term → Term
  | [],      body => body
  | e :: es, body => .letIn e (wrapLets es body)

def findAAlt : List AAlt → Nat → Option AAlt
  | [],      _ => none
  | a :: as, t => if a.tag = t then some a else findAAlt as t

/-- `[dyn 0, dyn 1, …, dyn (k-1)]` — a `caseT` alternative binds its fields with
field `j` at index `j`, matching `Eval.caseT`'s `vs ++ ρ`. -/
def freshDyns (k : Nat) : PEnv := (List.range k).map PVal.dyn

/-! ## The specializer

Fuel is a step budget, decremented on every recursive call, so this is
structural recursion on `Nat`.  It is genuinely needed: unfolding a static call
is unbounded, and a static loop that fails to terminate at specialization time
is a real failure mode of offline partial evaluation, not a Lean artifact. -/

mutual

def mixTerm : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → ATerm →
    Except MixError MixOut
  | 0, _, _, _, _, _ => .error .outOfFuel
  | n + 1, A, idx, Δ, env, t =>
    match t with
    | .lit v => .ok (.stat v, [])
    | .var i =>
      match Δ[i]?, env[i]? with
      | some .stat, some (.stat v) => .ok (.stat v, [])
      | some .dyn,  some (.dyn k)  => .ok (.code (.var k), [])
      | some _,     some _         => .error (.illAnnotated "var: division disagrees with the environment")
      | _,          _              => .error (.unboundVar i)
    | .lift e => do
      let (re, rq) ← mixTerm n A idx Δ env e
      match re with
      | .stat v => .ok (.code (.lit v), rq)
      | .code _ => .error (.notStatic "lift: operand is not static")
    | .letIn _ e body => do
      let (re, rq₁) ← mixTerm n A idx Δ env e
      match btOf Δ e, re with
      -- static binding: `mix` keeps the value and emits NO residual binder, so
      -- the residual indices already in scope do not move
      | .stat, .stat v => do
          let (rb, rq₂) ← mixTerm n A idx (.stat :: Δ) (.stat v :: env) body
          .ok (rb, rq₁ ++ rq₂)
      -- dynamic binding: one residual binder is created, so everything in scope
      -- shifts by one and the bound variable becomes index 0
      | .dyn, _ => do
          let (rb, rq₂) ← mixTerm n A idx (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) body
          match rb with
          | .code b' => .ok (.code (.letIn re.toCode b'), rq₁ ++ rq₂)
          | .stat _  => .error (.illAnnotated "letIn: dynamic binding with a static body")
      | .stat, .code _ => .error (.notStatic "letIn: static binding produced code")
    | .ite _ c a e => do
      let (rc, rq₁) ← mixTerm n A idx Δ env c
      match btOf Δ c, rc with
      -- static condition: the untaken branch is never walked, so neither its
      -- code nor its specialization requests are emitted.  This is where a
      -- specialized interpreter loses its dispatch.
      | .stat, .stat (.bool true)  => do
          let (r, rq₂) ← mixTerm n A idx Δ env a
          .ok (r, rq₁ ++ rq₂)
      | .stat, .stat (.bool false) => do
          let (r, rq₂) ← mixTerm n A idx Δ env e
          .ok (r, rq₁ ++ rq₂)
      | .stat, _ => .error (.notStatic "ite: static condition is not a Bool")
      | .dyn, _ => do
          let (ra, rq₂) ← mixTerm n A idx Δ env a
          let (re, rq₃) ← mixTerm n A idx Δ env e
          .ok (.code (.ite rc.toCode ra.toCode re.toCode), rq₁ ++ rq₂ ++ rq₃)
    | .prim b p ts => do
      let (rs, rq) ← mixTerms n A idx Δ env ts
      match b with
      | .stat => do
          let vs ← allStatic rs
          match evalPrim p vs with
          | .ok v    => .ok (.stat v, rq)
          | .error m => .error (.primFailed m)
      | .dyn => .ok (.code (.prim p (rs.map PRes.toCode)), rq)
    | .ctorT b k ts => do
      let (rs, rq) ← mixTerms n A idx Δ env ts
      match b with
      | .stat => do
          let vs ← allStatic rs
          .ok (.stat (.ctor k vs), rq)
      | .dyn => .ok (.code (.ctorT k (rs.map PRes.toCode)), rq)
    | .caseT _ s alts => do
      let (rsc, rq₁) ← mixTerm n A idx Δ env s
      match btOf Δ s, rsc with
      -- static scrutinee: select the alternative now and bind its fields as
      -- static values.  No residual `caseT` survives.
      | .stat, .stat (.ctor tag vs) =>
        match findAAlt alts tag with
        | none => .error (.illAnnotated s!"caseT: no alternative for tag {tag}")
        | some a =>
          if a.arity = vs.length then do
            let (r, rq₂) ← mixTerm n A idx
              (List.replicate a.arity .stat ++ Δ) (vs.map PVal.stat ++ env) a.body
            .ok (r, rq₁ ++ rq₂)
          else .error (.badArity "caseT: alternative arity does not match the value")
      | .stat, _ => .error (.notStatic "caseT: static scrutinee is not a constructor")
      | .dyn, _ => do
          let (alts', rq₂) ← mixAlts n A idx Δ env alts
          .ok (.code (.caseT rsc.toCode alts'), rq₁ ++ rq₂)
    | .call b f ts => do
      let (rs, rq₁) ← mixTerms n A idx Δ env ts
      match A.fn f with
      | none => .error (.unknownFun f)
      | some fd =>
        match b with
        -- a static result cannot come out of a residual call, so unfold
        | .stat => do
            let vs ← allStatic rs
            let (r, rq₂) ← mixTerm n A idx fd.params (vs.map PVal.stat) fd.body
            .ok (r, rq₁ ++ rq₂)
        -- ask the driver for a specialized copy and emit a call to it
        | .dyn => do
            let (svs, dts) ← splitArgs fd.params rs
            let req : SpecRequest := ⟨f, svs⟩
            match idx req with
            | none   => .error (.noSpec f)
            | some k => .ok (.code (.call k dts), rq₁ ++ [req])
    | .ucall b f ts => do
      let (rs, rq₁) ← mixTerms n A idx Δ env ts
      match A.fn f with
      | none => .error (.unknownFun f)
      | some fd =>
        match b with
        | .stat => do
            let vs ← allStatic rs
            let (r, rq₂) ← mixTerm n A idx fd.params (vs.map PVal.stat) fd.body
            .ok (r, rq₁ ++ rq₂)
        -- inline into residual code.  Each dynamic argument is `let`-bound
        -- once, which is both what keeps `PVal.dyn` a plain index -- the body
        -- is specialized in a scope whose shape we chose -- and what stops an
        -- argument expression being duplicated at each of its uses.
        | .dyn => do
            let dts ← dynArgCodes fd.params rs
            let env' ← inlineEnv fd.params rs dts.length 0
            let (r, rq₂) ← mixTerm n A idx fd.params env' fd.body
            match r with
            | .code b' => .ok (.code (wrapLets dts b'), rq₁ ++ rq₂)
            | .stat _  => .error (.illAnnotated "ucall: dynamic unfold with a static body")

def mixTerms : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → List ATerm →
    Except MixError (List PRes × List SpecRequest)
  | _, _, _, _, _, [] => .ok ([], [])
  | n, A, idx, Δ, env, t :: ts => do
      let (r, rq₁)  ← mixTerm n A idx Δ env t
      let (rs, rq₂) ← mixTerms n A idx Δ env ts
      .ok (r :: rs, rq₁ ++ rq₂)

def mixAlts : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → List AAlt →
    Except MixError (List Alt × List SpecRequest)
  | _, _, _, _, _, [] => .ok ([], [])
  | n, A, idx, Δ, env, a :: as => do
      -- the alternative binds `a.arity` residual fields, so the enclosing scope
      -- shifts by that much
      let Δ'   := List.replicate a.arity .dyn ++ Δ
      let env' := freshDyns a.arity ++ env.shiftBy a.arity
      let (r, rq₁)   ← mixTerm n A idx Δ' env' a.body
      let (as', rq₂) ← mixAlts n A idx Δ env as
      match r with
      | .code b' => .ok ((a.tag, a.arity, b') :: as', rq₁ ++ rq₂)
      | .stat v  => .ok ((a.tag, a.arity, .lit v) :: as', rq₁ ++ rq₂)

end

/-! ## Specializing one function -/

/-- Specialize `req.funIdx` with respect to `req.staticArgs`.

The residual function keeps only the dynamic parameters (`dynCount`), and
`buildEnv` is what maps the `j`-th of them to residual index `j`.  A static
return value becomes a constant function -- correct, and the case that makes
the first projection collapse an interpreter's dispatch. -/
def mixFun (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat)
    (req : SpecRequest) : Except MixError (FunDef × List SpecRequest) :=
  match A.fn req.funIdx with
  | none    => .error (.unknownFun req.funIdx)
  | some fd =>
    match buildEnv fd.params req.staticArgs 0 with
    | .error e => .error e
    | .ok env =>
      match mixTerm stepFuel A idx fd.params env fd.body with
      | .error e     => .error e
      | .ok (r, rq)  => .ok (⟨dynCount fd.params, r.toCode⟩, rq)

/-! ## The driver

Two passes over the same pure `mixTerm`.  The first discovers which
specializations are reachable; the second generates them, now that every
request has an index.

The dummy `fun _ => some 0` in discovery is sound because an index appears only
inside emitted code, never in a control-flow decision, so the two passes explore
the same requests.  Nothing below relies on that: if discovery misses a request,
generation reports `noSpec` rather than emitting a dangling call. -/

def memberReq (r : SpecRequest) : List SpecRequest → Bool
  | []      => false
  | q :: qs => (r == q) || memberReq r qs

def indexOfReqFrom (r : SpecRequest) (i : Nat) : List SpecRequest → Option Nat
  | []      => none
  | q :: qs => if r == q then some i else indexOfReqFrom r (i + 1) qs

@[inline] def indexOfReq (rs : List SpecRequest) (r : SpecRequest) : Option Nat :=
  indexOfReqFrom r 0 rs

/-- Append the requests of `rq` that are not already known, preserving order and
without introducing duplicates within `rq` itself. -/
def addNew (seen : List SpecRequest) : List SpecRequest → List SpecRequest
  | []      => []
  | r :: rs =>
      if memberReq r seen then addNew seen rs
      else r :: addNew (seen ++ [r]) rs

/-- Worklist closure.  `wlFuel` bounds the number of specializations, which is
the termination condition offline partial evaluation genuinely lacks: an
infinite family of static argument tuples is a real (and useful to detect)
outcome, not a Lean limitation. -/
def discover (stepFuel : Nat) : Nat → AProgram → List SpecRequest → List SpecRequest →
    Except MixError (List SpecRequest)
  | _,     _, [],          seen => .ok seen
  | 0,     _, _ :: _,      _    => .error .outOfFuel
  | k + 1, A, req :: work, seen => do
      let (_, rq) ← mixFun stepFuel A (fun _ => some 0) req
      let fresh := addNew seen rq
      discover stepFuel k A (work ++ fresh) (seen ++ fresh)

/-- Explicit recursion rather than `mapM`: `generate_spec` has to say that
residual function `i` is the specialization of request `i`, and `List.mapM` over
`Except` does not expose a structure to induct on. -/
def generateFrom (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) :
    List SpecRequest → Except MixError (List FunDef)
  | []      => .ok []
  | r :: rs =>
    match mixFun stepFuel A idx r, generateFrom stepFuel A idx rs with
    | .ok (fd, _), .ok fds  => .ok (fd :: fds)
    | .error e,    _        => .error e
    | _,           .error e => .error e

@[inline] def generate (stepFuel : Nat) (A : AProgram) (reqs : List SpecRequest) :
    Except MixError (List FunDef) :=
  generateFrom stepFuel A (indexOfReq reqs) reqs

/-- `mix`.  The entry request is discovered first, so it is residual function 0. -/
def mixDriver (stepFuel wlFuel : Nat) (A : AProgram) (statics : List Val) :
    Except MixError Program := do
  let entryReq : SpecRequest := ⟨A.entry, statics⟩
  let reqs ← discover stepFuel wlFuel A [entryReq] [entryReq]
  let funs ← generate stepFuel A reqs
  .ok ⟨funs, 0⟩

end Projection
