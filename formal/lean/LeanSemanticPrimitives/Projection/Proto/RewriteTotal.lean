/-
# B2's prerequisite: traversals the KERNEL can see

B1 got away with saying nothing about `main`'s body, so it never had to reduce
one of `InterpreterVariant.lean`'s traversals.  B2 cannot: its whole content is
a fact about the ACTUAL rewritten body.

Every traversal there -- `countRef`, `substRef`, `noBranch`, `beforeHoleTotal`,
`goInline`, `goAlts` -- is a `partial def`.  `partial` definitions are OPAQUE
to the kernel: they have no computational equations, so
`goInline "env0" mainBody = some (…)` cannot be proved by `rfl`, by `simp`, or
by `decide`.  Not "is hard to"; cannot.

This file supplies STRUCTURALLY RECURSIVE versions of exactly those traversals
and nothing else.  They are ordinary definitions, so the kernel reduces them,
and the fact B2 needs becomes a `rfl`.

Three things this file deliberately does NOT do:

* it does not axiomatise, `native_decide`, or assume any equation about the
  `partial` definitions.  They stay opaque; these are separate definitions that
  happen to be written the same way;
* it does not change the side conditions.  The refusal behaviour and the
  capture restriction are reproduced exactly, including `beforeHoleTotal`;
* it does not quietly prove things about a DIFFERENT transform.  That the total
  versions agree with the executable ones on the real input is checked by
  `proto_probe --rewrite-agree` as REGRESSION EVIDENCE, which is a run and not
  a theorem, and this file says so rather than implying otherwise.
-/
import LeanSemanticPrimitives.Projection.Proto.VariantTransport

namespace Projection
namespace ProtoVar

open Projection.Surface

-- Needed only to COMPARE the total traversals against the executable `partial`
-- ones in `proto_probe --rewrite-agree`.  No proof below uses it.
deriving instance BEq for Projection.Surface.SExp

/-! ## Total traversals

Mutual structural recursion through `List SExp` and `List SAlt`, the same shape
`noMainCallB` already uses in `VariantTransport.lean`. -/

mutual

def countRefT (x : String) : SExp → Nat
  | .lit _        => 0
  | .ref y        => if y == x then 1 else 0
  | .letN y e b   => countRefT x e + (if y == x then 0 else countRefT x b)
  | .ite c t f    => countRefT x c + countRefT x t + countRefT x f
  | .prim _ es    => countRefL x es
  | .mk _ es      => countRefL x es
  | .call _ es    => countRefL x es
  | .switch s as  => countRefT x s + countRefA x as

def countRefL (x : String) : List SExp → Nat
  | []      => 0
  | e :: es => countRefT x e + countRefL x es

def countRefA (x : String) : List SAlt → Nat
  | []              => 0
  | (_, bs, b) :: as =>
      (if bs.contains x then 0 else countRefT x b) + countRefA x as

end

mutual

def substRefT (x : String) (v : SExp) : SExp → SExp
  | .lit l        => .lit l
  | .ref y        => if y == x then v else .ref y
  | .letN y e b   => .letN y (substRefT x v e) (if y == x then b else substRefT x v b)
  | .ite c t f    => .ite (substRefT x v c) (substRefT x v t) (substRefT x v f)
  | .prim p es    => .prim p (substRefL x v es)
  | .mk t es      => .mk t (substRefL x v es)
  | .call f es    => .call f (substRefL x v es)
  | .switch s as  => .switch (substRefT x v s) (substRefA x v as)

def substRefL (x : String) (v : SExp) : List SExp → List SExp
  | []      => []
  | e :: es => substRefT x v e :: substRefL x v es

def substRefA (x : String) (v : SExp) : List SAlt → List SAlt
  | []               => []
  | (k, bs, b) :: as =>
      (k, bs, if bs.contains x then b else substRefT x v b) :: substRefA x v as

end

mutual

def noBranchT : SExp → Bool
  | .lit _      => true
  | .ref _      => true
  | .letN _ _ _ => false
  | .ite _ _ _  => false
  | .switch _ _ => false
  | .prim _ es  => noBranchL es
  | .mk _ es    => noBranchL es
  | .call _ es  => noBranchL es

def noBranchL : List SExp → Bool
  | []      => true
  | e :: es => noBranchT e && noBranchL es

end

mutual

/-- Everything evaluated STRICTLY BEFORE the single occurrence of `x` is
`ref` or `lit`.  Reproduced exactly: this is the condition that failure-order
preservation needs, and dropping it here would make the kernel-visible
transform a different one from the checked transform. -/
def beforeHoleTotalT (x : String) : SExp → Bool
  | .ref _     => true
  | .lit _     => true
  | .prim _ es => argsBeforeHoleT x es
  | .mk _ es   => argsBeforeHoleT x es
  | .call _ es => argsBeforeHoleT x es
  | _          => false

def argsBeforeHoleT (x : String) : List SExp → Bool
  | []        => true
  | e :: rest =>
      if countRefT x e > 0 then beforeHoleTotalT x e
      else (match e with | .ref _ => true | .lit _ => true | _ => false)
           && argsBeforeHoleT x rest

end

/-! ## The rewrite, total

Same four side conditions, same refusal behaviour. -/

def inlineNextLetT (x : String) : SExp → Option (SExp × InlineReport)
  | t@(.letN y e (.letN z e2 body)) =>
      if y == x && z != x then
        let r : InlineReport :=
          { found := true, selfRef := countRefT x e, useInNext := countRefT x e2
          , useLater := countRefT x body, nextStrict := noBranchT e2
          , beforeTotal := beforeHoleTotalT x e2 }
        if r.selfRef == 0 && r.useInNext == 1 && r.useLater == 0 && r.nextStrict
             && r.beforeTotal
        then some (.letN z (substRefT x e e2) body, { r with applied := true })
        else some (t, r)
      else none
  | _ => none

mutual

def goInlineT (x : String) (fuel : Nat) : SExp → Option (SExp × InlineReport)
  | t@(.letN y e b) =>
      match inlineNextLetT x t with
      | some r => some r
      | none   => match fuel with
                  | 0      => none
                  | n + 1  => (goInlineT x n b).map (fun p => (.letN y e p.1, p.2))
  | .switch s as => match fuel with
                    | 0     => none
                    | n + 1 => (goAltsT x n as).map (fun p => (.switch s p.1, p.2))
  | _ => none

def goAltsT (x : String) (fuel : Nat) : List SAlt → Option (List SAlt × InlineReport)
  | []      => none
  | a :: rest =>
      match fuel with
      | 0     => none
      | n + 1 =>
        match goInlineT x n a.2.2 with
        | some p => some ((a.1, a.2.1, p.1) :: rest, p.2)
        | none   => (goAltsT x n rest).map (fun p => (a :: p.1, p.2))

end

/-- `main`'s body nests two `switch`es around the binding block, so a depth of
8 is ample; the bound only has to be big enough, and a too-small one shows up
as `none` rather than as a wrong answer. -/
def rewriteDepth : Nat := 8

/-! ## The kernel-visible fact about the ACTUAL body

Everything below reduces: `Hw.hwS` is a closed term and every function here is
structurally recursive. -/

def mainBodyOf (P : SProgram) : Option SExp :=
  (P.funs.find? (fun f => f.name == "main")).map SFun.body

def rewrittenT : Option (SExp × InlineReport) :=
  (mainBodyOf Hw.hwS).bind (goInlineT "env0" rewriteDepth)

/-- The side conditions hold on the real body, CHECKED BY THE KERNEL. -/
theorem rewrittenT_applied :
    (rewrittenT.map (fun p => p.2.ok)) = some true := by rfl

/-! ### The rewrite really fired

`SExp` has no `BEq`, and a syntactic inequality would be the weaker statement
anyway.  The meaningful witness is that the `letN "env0"` BINDER is there
before and gone after -- which is what removing a single-use binding means.

(Note `countRefT "env0"` of the whole body is 0 both before and after, and
correctly so: `countRefT` counts FREE occurrences, and inside `main` the name
is bound.  Using it here would have looked like a passing check while
measuring nothing.) -/

mutual

def countBindT (x : String) : SExp → Nat
  | .lit _        => 0
  | .ref _        => 0
  | .letN y e b   => (if y == x then 1 else 0) + countBindT x e + countBindT x b
  | .ite c t f    => countBindT x c + countBindT x t + countBindT x f
  | .prim _ es    => countBindL x es
  | .mk _ es      => countBindL x es
  | .call _ es    => countBindL x es
  | .switch s as  => countBindT x s + countBindA x as

def countBindL (x : String) : List SExp → Nat
  | []      => 0
  | e :: es => countBindT x e + countBindL x es

def countBindA (x : String) : List SAlt → Nat
  | []               => 0
  | (_, _, b) :: as  => countBindT x b + countBindA x as

end

theorem env0_bound_once_before :
    (mainBodyOf Hw.hwS).map (countBindT "env0") = some 1 := by rfl

theorem env0_binder_gone_after :
    (rewrittenT.map (fun p => countBindT "env0" p.1)) = some 0 := by rfl

/-! ## The TOTAL variant program, and B1 instantiated at IT

`hwSVar` in `InterpreterVariant.lean` is built from the `partial` `goInline`.
Everything proved about the body above is about `rewrittenT`, the TOTAL one.
Those are different definitions, and a regression showing they agree on this
input is evidence, not a transfer of theorems -- there is nothing to transfer
to, an opaque definition having no equations.

So the proof target needs its OWN program.  That is `hwSVarT`, and it is what
`main_agree_varT` and everything after it must be stated about. -/

def hwSVarT : SProgram :=
  match rewrittenT with
  | none        => Hw.hwS
  | some (b, r) =>
      if r.ok then
        { Hw.hwS with
          funs := Hw.hwS.funs.map
            (fun g => if g.name == "main" then { g with body := b } else g) }
      else Hw.hwS

/-- The replacement is name-preserving and the identity off `main`, which is
what B1's table lemma needs. -/
theorem sFn_hwSVarT (f : String) (hne : f ≠ "main") : sFn hwSVarT f = sFn Hw.hwS f := by
  unfold hwSVarT
  cases hp : rewrittenT with
  | none   => simp only [hp]
  | some p =>
      simp only [hp]
      by_cases hok : p.2.ok = true
      · rw [if_pos hok]
        refine sFn_mapF (fun h => ?_) (fun h hh => ?_) f hne
        · by_cases hn : h.name == "main" <;> simp [hn]
        · have : ¬(h.name = "main") := hh
          simp [this]
      · rw [if_neg hok]

/-- B1, for the program the proof target actually uses. -/
theorem SEval_hwSVarT_of_hwS {σ : SEnv} {e : SExp} {v : Val}
    (hnm : noMainCallB e = true) (h : SEval Hw.hwS σ e v) : SEval hwSVarT σ e v :=
  SEval_congr (fun f hne => sFn_hwSVarT f hne) MainFreeFuns_hwS _ _ _ hnm h

theorem SEvalList_hwSVarT_of_hwS {σ : SEnv} {es : List SExp} {vs : List Val}
    (hnm : noMainCallL es = true) (h : SEvalList Hw.hwS σ es vs) :
    SEvalList hwSVarT σ es vs :=
  SEvalList_congr (fun f hne => sFn_hwSVarT f hne) MainFreeFuns_hwS _ _ _ hnm h

/-- `hwSVarT` really is the rewritten program, not a fallback to `hwS`:
`main`'s binder count dropped, kernel-checked. -/
theorem hwSVarT_rewritten :
    (mainBodyOf hwSVarT).map (countBindT "env0") = some 0 := by rfl

theorem hwS_not_rewritten :
    (mainBodyOf Hw.hwS).map (countBindT "env0") = some 1 := by rfl

/-! ### Resolution and BTA for the total variant

CHECKS, not theorems.  `#guard` runs the compiler's evaluator; turning these
into kernel-reduced theorems is separate work and is NOT done here, so nothing
below may be cited as a proved fact. -/

def hwResolvedVarT : Except String (Program × List Bool) := resolveProgram hwSVarT
def hwPVarT   : Program   := match hwResolvedVarT with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwInlVarT : List Bool := match hwResolvedVarT with | .ok (_, i) => i | .error _ => []
def hwAVarT   : Except BTAError AProgram :=
  bta hwPVarT hwInlVarT [.stat, .dyn, .dyn, .dyn] 200
def hwAPVarT  : AProgram := match hwAVarT with | .ok a => a | .error _ => ⟨[], 0⟩

#guard hwResolvedVarT.toOption.isSome
#guard hwAVarT.toOption.isSome
#guard wfAProgram hwAPVarT
#guard eraseProgram hwAPVarT == hwPVarT

end ProtoVar
end Projection
