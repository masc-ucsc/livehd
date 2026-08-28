/-
  Semantics of the object language: a fuelled evaluator for execution, and an
  inductive relation for the denotation.

  WHY BOTH.  Object programs may diverge (`call` is unrestricted recursion) and
  Lean requires definitions to terminate, so the executable evaluator takes fuel.
  But fuel must not appear in the projection theorems -- a fuel-indexed statement
  would leak an arbitrary constant into the end-to-end correctness claim.  So the
  DENOTATION is the inductive relation `Eval`, every downstream theorem is stated
  with it, and `evalFuel_sound` connects the two.

  `outOfFuel` and `typeError` stay distinct.  Collapsing them into `none` turns a
  specializer bug into "needs more fuel" and hides it.
-/

import LeanSemanticPrimitives.Projection.ObjectLanguage

namespace Projection

/-! ## Bit vectors, as encoded values

`BV` is not a primitive type of `L`.  A bit vector is `ctor bvTag [int w, int v]`,
mirroring `LGraphModel.BV = ⟨width, value⟩`.  Keeping it an ordinary encoded value
is what lets the object specializer carry bit vectors with no special support. -/

/-- Reserved tag for encoded bit vectors.  High, to stay clear of the small tags
`Encoding.lean` gives to `Term`/`Program` constructors. -/
def bvTag : Nat := 1000

@[inline] def mkBV (w v : Int) : Val := .ctor bvTag [.int w, .int v]

/-- Value modulo `2^w`, matching `LGraphModel.mk_bv`'s normalisation. -/
@[inline] def bvNorm (w v : Int) : Val :=
  if w ≤ 0 then mkBV 0 0 else mkBV w (v % (2 ^ w.toNat))

@[inline] def asBV : Val → Option (Int × Int)
  | .ctor t [.int w, .int v] => if t = bvTag then some (w, v) else none
  | _                        => none

/-- Bit `i` of a bit vector, by its unsigned value. -/
@[inline] def bvBitAt (w v : Int) (i : Int) : Bool :=
  if w ≤ 0 ∨ i < 0 then false
  else ((v % (2 ^ w.toNat)).toNat >>> i.toNat) % 2 == 1

/-- Bitwise combine at width `w`, one bit at a time.  First-order: the operation
is chosen by the caller's `Prim`, never passed as a function. -/
def bvCombine (f : Bool → Bool → Bool) (w a b : Int) : Val :=
  if w ≤ 0 then mkBV 0 0
  else
    let n := w.toNat
    let bits : Nat := (List.range n).foldl
      (fun acc (i : Nat) =>
        if f (bvBitAt w a (Int.ofNat i)) (bvBitAt w b (Int.ofNat i)) then acc + 2 ^ i else acc) 0
    mkBV w (Int.ofNat bits)

/-! ## Primitive evaluation

The ONLY place a primitive is interpreted.  Adding a primitive touches this
function and its correctness lemma -- never the specializer, whose `prim` rule is
generic over `p` (see `PartialEvaluator.lean`). -/

def evalPrim (p : Prim) (vs : List Val) : Except String Val :=
  match p, vs with
  | .addI, [.int a, .int b] => .ok (.int (a + b))
  | .subI, [.int a, .int b] => .ok (.int (a - b))
  | .mulI, [.int a, .int b] => .ok (.int (a * b))
  | .divI, [.int a, .int b] => .ok (.int (if b = 0 then 0 else a / b))
  | .modI, [.int a, .int b] => .ok (.int (if b = 0 then 0 else a % b))
  | .ltI,  [.int a, .int b] => .ok (.bool (a < b))
  | .leI,  [.int a, .int b] => .ok (.bool (a ≤ b))
  | .eqI,  [.int a, .int b] => .ok (.bool (a = b))
  | .andB, [.bool a, .bool b] => .ok (.bool (a && b))
  | .orB,  [.bool a, .bool b] => .ok (.bool (a || b))
  | .notB, [.bool a]          => .ok (.bool (!a))
  | .isNil, [v] => .ok (.bool (match v with | .nil => true | _ => false))
  | .hd, [.cons a _] => .ok a
  | .tl, [.cons _ d] => .ok d
  | .bvMk,    [.int w, .int v] => .ok (bvNorm w v)
  | .bvWidth, [v] => match asBV v with | some (w, _) => .ok (.int w) | none => .error "bvWidth: not a BV"
  | .bvUint,  [v] => match asBV v with
                     | some (w, x) => .ok (.int (if w ≤ 0 then 0 else x % (2 ^ w.toNat)))
                     | none => .error "bvUint: not a BV"
  | .bvBit,   [v, .int i] => match asBV v with
                             | some (w, x) => .ok (.bool (bvBitAt w x i))
                             | none => .error "bvBit: not a BV"
  | .bvAnd, [.int w, a, b] => match asBV a, asBV b with
                              | some (_, x), some (_, y) => .ok (bvCombine (· && ·) w x y)
                              | _, _ => .error "bvAnd: not a BV"
  | .bvOr,  [.int w, a, b] => match asBV a, asBV b with
                              | some (_, x), some (_, y) => .ok (bvCombine (· || ·) w x y)
                              | _, _ => .error "bvOr: not a BV"
  | .bvXor, [.int w, a, b] => match asBV a, asBV b with
                              | some (_, x), some (_, y) => .ok (bvCombine xor w x y)
                              | _, _ => .error "bvXor: not a BV"
  | .bvNot, [.int w, a] => match asBV a with
                           | some (_, x) => .ok (bvCombine (fun p _ => !p) w x 0)
                           | none => .error "bvNot: not a BV"
  | .bvResize, [.int w, a] => match asBV a with
                              | some (aw, x) => .ok (bvNorm w (if aw ≤ 0 then 0 else x % (2 ^ aw.toNat)))
                              | none => .error "bvResize: not a BV"
  | p, vs => .error s!"primitive arity/type error: expected {p.arity} operands, got {vs.length}"

/-! ## `caseT` alternative selection

Pinned down rather than left implicit, because each choice is a place a
specializer and an evaluator can silently disagree:

* **duplicate tags** -- the FIRST matching alternative wins;
* **missing branch** -- a `typeError`, never a silent default;
* **arity mismatch** between the scrutinee's field count and the alternative's
  `arity` -- a `typeError`.
-/

def findAlt (alts : List Alt) (tag : Nat) : Option Alt :=
  alts.find? (fun a => a.tag = tag)

/-! ## The fuelled evaluator -/

inductive EvalResult where
  | value     : Val → EvalResult
  | outOfFuel : EvalResult
  | typeError : String → EvalResult
  deriving Inhabited, Repr

mutual

/-- Evaluate a term.  Each `call`, and each recursive descent, consumes fuel.

Function bodies are evaluated in a FRESH environment holding exactly the
arguments (`var i` = argument `i`).  The language is first order: there are no
closures, so nothing is captured from the caller. -/
def evalFuel : Nat → Program → Env → Term → EvalResult
  | 0,     _, _, _ => .outOfFuel
  | n + 1, P, ρ, t =>
    match t with
    | .lit v   => .value v
    | .var i   => match ρ[i]? with
                  | some v => .value v
                  | none   => .typeError s!"unbound de Bruijn index {i}"
    | .letIn e b =>
      match evalFuel n P ρ e with
      | .value v     => evalFuel n P (v :: ρ) b
      | .outOfFuel   => .outOfFuel
      | .typeError m => .typeError m
    | .ite c a b =>
      match evalFuel n P ρ c with
      | .value (.bool true)  => evalFuel n P ρ a
      | .value (.bool false) => evalFuel n P ρ b
      | .value _             => .typeError "ite: condition is not a Bool"
      | .outOfFuel           => .outOfFuel
      | .typeError m         => .typeError m
    | .prim p ts =>
      match evalFuelList n P ρ ts with
      | .inl vs => match evalPrim p vs with
                   | .ok v    => .value v
                   | .error m => .typeError m
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"
    | .ctorT k ts =>
      match evalFuelList n P ρ ts with
      | .inl vs             => .value (.ctor k vs)
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"
    | .caseT s alts =>
      match evalFuel n P ρ s with
      | .value (.ctor tag vs) =>
        match findAlt alts tag with
        | none   => .typeError s!"caseT: no alternative for tag {tag}"
        | some a =>
          if a.arity = vs.length then evalFuel n P (vs ++ ρ) a.body
          else .typeError s!"caseT: tag {tag} arity {a.arity} but {vs.length} fields"
      | .value _     => .typeError "caseT: scrutinee is not a constructor"
      | .outOfFuel   => .outOfFuel
      | .typeError m => .typeError m
    | .call f ts =>
      match evalFuelList n P ρ ts with
      | .inl vs =>
        match P.fn f with
        | none    => .typeError s!"call: no function {f}"
        | some fd =>
          if fd.arity = vs.length then evalFuel n P vs fd.body
          else .typeError s!"call: function {f} arity {fd.arity} but {vs.length} args"
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"

/-- Evaluate an argument list left to right, short-circuiting on the first
non-value result.

Fuel discipline: `evalFuel (n+1)` hands `n` to this function, and this function
hands the SAME `n` to each element.  So everything reachable below fuel `n+1`
runs at fuel `≤ n`, which is what makes strong induction on fuel work in
`evalFuel_sound`.  (Passing `n+1` back to `evalFuel` here would still terminate,
but the soundness induction would have no decreasing measure to hang on.) -/
def evalFuelList : Nat → Program → Env → List Term → Sum (List Val) EvalResult
  | _, _, _, []      => .inl []
  | n, P, ρ, t :: ts =>
    match evalFuel n P ρ t with
    | .value v     => match evalFuelList n P ρ ts with
                      | .inl vs => .inl (v :: vs)
                      | .inr r  => .inr r
    | .outOfFuel   => .inr .outOfFuel
    | .typeError m => .inr (.typeError m)

end

/-! ## The denotation

The relation every projection theorem is stated against.  No fuel appears here. -/

mutual

inductive Eval (P : Program) : Env → Term → Val → Prop where
  | lit   : Eval P ρ (.lit v) v
  | var   : ρ[i]? = some v → Eval P ρ (.var i) v
  | letIn : Eval P ρ e v₁ → Eval P (v₁ :: ρ) b v → Eval P ρ (.letIn e b) v
  | iteT  : Eval P ρ c (.bool true)  → Eval P ρ a v → Eval P ρ (.ite c a b) v
  | iteF  : Eval P ρ c (.bool false) → Eval P ρ b v → Eval P ρ (.ite c a b) v
  | prim  : EvalList P ρ ts vs → evalPrim p vs = .ok v → Eval P ρ (.prim p ts) v
  | ctorT : EvalList P ρ ts vs → Eval P ρ (.ctorT k ts) (.ctor k vs)
  | caseT : Eval P ρ s (.ctor tag vs) → findAlt alts tag = some a →
            a.arity = vs.length → Eval P (vs ++ ρ) a.body v →
            Eval P ρ (.caseT s alts) v
  | call  : EvalList P ρ ts vs → P.fn f = some fd → fd.arity = vs.length →
            Eval P vs fd.body v → Eval P ρ (.call f ts) v

inductive EvalList (P : Program) : Env → List Term → List Val → Prop where
  | nil  : EvalList P ρ [] []
  | cons : Eval P ρ t v → EvalList P ρ ts vs → EvalList P ρ (t :: ts) (v :: vs)

end

/-! ## Soundness of the executable evaluator against the denotation -/

/-- The list case is *derived* at each fuel level rather than co-inducted with the
term case.  `evalFuelList n` calls `evalFuel n` at the SAME fuel, so a joint
induction on fuel has no decreasing measure; but given the term property at `n`,
the list property at `n` follows by ordinary induction on the list.

The recurring `split at h <;> try contradiction` idiom discharges every arm that
returned a non-`value` result: there `h` equates two distinct constructors.
`contradiction` is the right closer because it fires on exactly that and nothing
else.  `simp` is not -- in the surviving arm it half-solves (`Sum.inl xs =
Sum.inl ys` becomes `xs = ys`) instead of failing, so `try` cannot back it out;
and `cases` is not, because in that arm `h` is a real hypothesis it would
consume.  (`Sum.noConfusion` is unusable here for an unrelated reason: `Sum`
lives in `Type u`, so its `noConfusion` cannot be applied at `Eq.{1}`.) -/
theorem evalFuelList_sound_of (n : Nat) (P : Program)
    (h : ∀ ρ t v, evalFuel n P ρ t = .value v → Eval P ρ t v) :
    ∀ ρ ts vs, evalFuelList n P ρ ts = .inl vs → EvalList P ρ ts vs := by
  intro ρ ts
  induction ts with
  | nil =>
    intro vs hv
    simp only [evalFuelList] at hv
    cases hv
    exact .nil
  | cons t ts ih =>
    intro vs hv
    simp only [evalFuelList] at hv
    split at hv <;> try contradiction
    rename_i v' hev
    split at hv <;> try contradiction
    rename_i vs' hvs
    cases hv
    exact .cons (h ρ t v' hev) (ih vs' hvs)

/-- Soundness of the executable evaluator against the denotation.

Note the shape: this is only *partial* correctness -- it says nothing about
programs that run out of fuel or fail.  That is sufficient here because the
hardware interpreter is total on a well-formed design, which is discharged
separately, and because every projection theorem is conditioned on the
specializer having produced a value. -/
theorem evalFuel_sound :
    ∀ (n : Nat) (P : Program) (ρ : Env) (t : Term) (v : Val),
      evalFuel n P ρ t = .value v → Eval P ρ t v := by
  intro n
  induction n with
  | zero =>
    intro P ρ t v h
    simp only [evalFuel] at h
    contradiction
  | succ n ih =>
    intro P ρ t v h
    -- the list property at THIS fuel, from the term property at this fuel
    have hlist := evalFuelList_sound_of n P (ih P)
    cases t with
    | lit v' =>
      simp only [evalFuel] at h
      cases h
      exact .lit
    | var i =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i v' hlk
      cases h
      exact .var hlk
    | letIn e b =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i v₁ he
      exact .letIn (ih P ρ e v₁ he) (ih P (v₁ :: ρ) b v h)
    | ite c a b =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      · rename_i hc; exact .iteT (ih P ρ c _ hc) (ih P ρ a v h)
      · rename_i hc; exact .iteF (ih P ρ c _ hc) (ih P ρ b v h)
    | prim p ts =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i vs hvs
      split at h <;> try contradiction
      rename_i v' hp
      cases h
      exact .prim (hlist ρ ts vs hvs) hp
    | ctorT k ts =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i vs hvs
      cases h
      exact .ctorT (hlist ρ ts vs hvs)
    | caseT sc alts =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i tag vs hs
      split at h <;> try contradiction
      rename_i a ha
      split at h <;> try contradiction
      rename_i har
      exact .caseT (ih P ρ sc _ hs) ha har (ih P (vs ++ ρ) a.body v h)
    | call f ts =>
      simp only [evalFuel] at h
      split at h <;> try contradiction
      rename_i vs hvs
      split at h <;> try contradiction
      rename_i fd hfd
      split at h <;> try contradiction
      rename_i har
      exact .call (hlist ρ ts vs hvs) hfd har (ih P vs fd.body v h)

end Projection
