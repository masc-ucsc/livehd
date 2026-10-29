/-
# `evalFuel` with an array environment (simulation speed)

`evalFuel` keeps the environment as a `List` and reads `var i` with `ρ[i]?`, which walks
`i` cells. A projected simulator's residual binds one variable per netlist node, so its
environment grows to thousands of entries and variable lookup dominates the run (about
45% of the Futamura route's time on PicoRV32).

`evalFuelArr` is the same evaluator over an `Array` indexed by binding level: entry `j`
is the `j`-th binding made, a binding pushes, and `var i` reads entry `size - 1 - i` in
O(1). `evalFuelArr_eq` proves it equal to `evalFuel` on the list's array image
(`envArr ρ`, the list reversed), for every fuel, program, environment and term, and the
`@[csimp]` theorems make every call site compiled after this module run it.
`MemProjection` imports this module, so `runMemSim` does.

Mathlib-free.
-/
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

namespace Projection

/-- An environment as an array by binding level: the list reversed. -/
def envArr (ρ : Env) : Array Val := ρ.reverse.toArray

theorem envArr_size (ρ : Env) : (envArr ρ).size = ρ.length := by simp [envArr]

theorem envArr_cons (v : Val) (ρ : Env) : envArr (v :: ρ) = (envArr ρ).push v := by
  simp [envArr]

theorem envArr_append (vs ρ : Env) : envArr (vs ++ ρ) = envArr ρ ++ envArr vs := by
  simp [envArr]

theorem envArr_get (ρ : Env) (i : Nat) (h : i < ρ.length) :
    (envArr ρ)[ρ.length - 1 - i]'(by rw [envArr_size]; omega) = ρ[i] := by
  simp only [envArr, List.getElem_toArray, List.getElem_reverse]
  congr 1
  omega

mutual

/-- `evalFuel` over an array environment by binding level. Same cases, same fuel. -/
def evalFuelArr : Nat → Program → Array Val → Term → EvalResult
  | 0,     _, _, _ => .outOfFuel
  | n + 1, P, ρ, t =>
    match t with
    | .lit v   => .value v
    | .var i   => if h : i < ρ.size then .value (ρ[ρ.size - 1 - i]'(by omega))
                  else .typeError s!"unbound de Bruijn index {i}"
    | .letIn e b =>
      match evalFuelArr n P ρ e with
      | .value v     => evalFuelArr n P (ρ.push v) b
      | .outOfFuel   => .outOfFuel
      | .typeError m => .typeError m
    | .ite c a b =>
      match evalFuelArr n P ρ c with
      | .value (.bool true)  => evalFuelArr n P ρ a
      | .value (.bool false) => evalFuelArr n P ρ b
      | .value _             => .typeError "ite: condition is not a Bool"
      | .outOfFuel           => .outOfFuel
      | .typeError m         => .typeError m
    | .prim p ts =>
      match evalFuelListArr n P ρ ts with
      | .inl vs => match evalPrim p vs with
                   | .ok v    => .value v
                   | .error m => .typeError m
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"
    | .ctorT k ts =>
      match evalFuelListArr n P ρ ts with
      | .inl vs             => .value (.ctor k vs)
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"
    | .caseT s alts =>
      match evalFuelArr n P ρ s with
      | .value (.ctor tag vs) =>
        match findAlt alts tag with
        | none   => .typeError s!"caseT: no alternative for tag {tag}"
        | some a =>
          if a.arity = vs.length then evalFuelArr n P (ρ ++ envArr vs) a.body
          else .typeError s!"caseT: tag {tag} arity {a.arity} but {vs.length} fields"
      | .value _     => .typeError "caseT: scrutinee is not a constructor"
      | .outOfFuel   => .outOfFuel
      | .typeError m => .typeError m
    | .call f ts =>
      match evalFuelListArr n P ρ ts with
      | .inl vs =>
        match P.fn f with
        | none    => .typeError s!"call: no function {f}"
        | some fd =>
          if fd.arity = vs.length then evalFuelArr n P (envArr vs) fd.body
          else .typeError s!"call: function {f} arity {fd.arity} but {vs.length} args"
      | .inr .outOfFuel     => .outOfFuel
      | .inr (.typeError m) => .typeError m
      | .inr (.value _)     => .typeError "internal: list result was a value"

def evalFuelListArr : Nat → Program → Array Val → List Term → Sum (List Val) EvalResult
  | _, _, _, []      => .inl []
  | n, P, ρ, t :: ts =>
    match evalFuelArr n P ρ t with
    | .value v     => match evalFuelListArr n P ρ ts with
                      | .inl vs => .inl (v :: vs)
                      | .inr r  => .inr r
    | .outOfFuel   => .inr .outOfFuel
    | .typeError m => .inr (.typeError m)

end

theorem var_arr (ρ : Env) (i : Nat) :
    (if h : i < (envArr ρ).size then EvalResult.value ((envArr ρ)[(envArr ρ).size - 1 - i]'(by omega))
     else .typeError s!"unbound de Bruijn index {i}")
    = (match ρ[i]? with
       | some v => EvalResult.value v
       | none   => .typeError s!"unbound de Bruijn index {i}") := by
  by_cases h : i < ρ.length
  · have hs : i < (envArr ρ).size := by rw [envArr_size]; exact h
    rw [dif_pos hs, List.getElem?_eq_getElem h]
    simp only [envArr_size] at *
    have := envArr_get ρ i h
    simp only [this]
  · have hs : ¬ i < (envArr ρ).size := by rw [envArr_size]; exact h
    rw [dif_neg hs, List.getElem?_eq_none (by omega)]

/-- The array evaluator equals `evalFuel` on the array image of the environment, at every
fuel; and likewise for argument lists. -/
theorem evalFuelArr_eq (n : Nat) :
    (∀ P ρ t, evalFuelArr n P (envArr ρ) t = evalFuel n P ρ t) ∧
    (∀ P ρ ts, evalFuelListArr n P (envArr ρ) ts = evalFuelList n P ρ ts) := by
  induction n with
  | zero =>
    have ht : ∀ P ρ t, evalFuelArr 0 P (envArr ρ) t = evalFuel 0 P ρ t := by
      intro P ρ t; simp [evalFuelArr, evalFuel]
    refine ⟨ht, ?_⟩
    intro P ρ ts
    induction ts with
    | nil => simp [evalFuelListArr, evalFuelList]
    | cons t ts ih => (simp only [evalFuelListArr, evalFuelList, ht, ih]) <;> rfl
  | succ n ih =>
    obtain ⟨iht, ihl⟩ := ih
    have ht : ∀ P ρ t, evalFuelArr (n + 1) P (envArr ρ) t = evalFuel (n + 1) P ρ t := by
      intro P ρ t
      cases t with
      | lit v => simp [evalFuelArr, evalFuel]
      | var i => simp only [evalFuelArr, evalFuel]; exact var_arr ρ i
      | letIn e b => (simp only [evalFuelArr, evalFuel, iht, ← envArr_cons]) <;> rfl
      | ite c a b => (simp only [evalFuelArr, evalFuel, iht]) <;> rfl
      | prim p ts => (simp only [evalFuelArr, evalFuel, ihl]) <;> rfl
      | ctorT k ts => (simp only [evalFuelArr, evalFuel, ihl]) <;> rfl
      | caseT s alts => (simp only [evalFuelArr, evalFuel, iht, ← envArr_append]) <;> rfl
      | call f ts => (simp only [evalFuelArr, evalFuel, ihl, iht]) <;> rfl
    refine ⟨ht, ?_⟩
    intro P ρ ts
    induction ts with
    | nil => simp [evalFuelListArr, evalFuelList]
    | cons t ts ih => (simp only [evalFuelListArr, evalFuelList, ht, ih]) <;> rfl

/-- `evalFuel` through the array evaluator. -/
def evalFuelViaArr (n : Nat) (P : Program) (ρ : Env) (t : Term) : EvalResult :=
  evalFuelArr n P (envArr ρ) t

def evalFuelListViaArr (n : Nat) (P : Program) (ρ : Env) (ts : List Term) : Sum (List Val) EvalResult :=
  evalFuelListArr n P (envArr ρ) ts

@[csimp] theorem evalFuel_eq_arr : @evalFuel = @evalFuelViaArr := by
  funext n P ρ t
  exact ((evalFuelArr_eq n).1 P ρ t).symm

@[csimp] theorem evalFuelList_eq_arr : @evalFuelList = @evalFuelListViaArr := by
  funext n P ρ ts
  exact ((evalFuelArr_eq n).2 P ρ ts).symm

end Projection
