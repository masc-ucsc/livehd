/-
  The two-level language: `L` with every construct annotated static or dynamic.

  `mix` consumes THIS, not `Term`.  That is the whole point of choosing an
  OFFLINE specializer: the binding times are part of the program, so `mix`'s
  "compute now or emit code" decision is a dispatch on static data.  Under
  self-application -- `mix` specializing `mix` -- the annotated program is the
  static input, so that dispatch unrolls away and the generated compiler
  contains no binding-time tests at all.  An online specializer decides by
  inspecting values at specialization time, which is strictly more precise and
  does not unroll; the second projection would produce a compiler that still
  carries the whole specializer.  (Online self-applicable specializers do exist;
  offline is the tractable choice here, not the only possible one.)

  ANNOTATION MEANS: the binding time of the value the construct produces.
  `var` carries none -- its binding time comes from the division `Δ`.

  THE CONGRUENCE RULE for value-producing nodes (`prim`, `ctorT`, calls) is
  uniform: a `stat` node requires all its operands `stat`, a `dyn` node requires
  all its operands `dyn`.  Any mismatch is bridged by an explicit `lift`, whose
  only job is to turn a static value into residual code.

  CONTROL FLOW IS THE EXCEPTION, AND IT IS THE WHOLE POINT.  For `ite` and
  `caseT` the binding time of the CONDITION is independent of the binding time
  of the RESULT.  A static condition with dynamic branches means "decide now,
  emit the chosen branch as code" -- which is exactly how specializing an
  interpreter to a program erases the interpreter's dispatch, the single most
  important thing a partial evaluator does.  The only constraint is the
  unavoidable one: a dynamic condition forces a dynamic result, because a value
  cannot be known if the choice that produces it is not.

  CALLS COME IN TWO FLAVOURS.  `call` residualizes -- the driver generates a
  specialized copy of the callee and the residual program calls it.  `ucall`
  unfolds -- the callee's body is specialized right here.  Both have the same
  binding-time rule; the choice is a termination and code-size judgement, which
  is why it is an annotation rather than something `wfA` decides.  Without
  `ucall` a static loop over dynamic data (an index walk down a dynamic list)
  cannot unroll, because its RESULT is dynamic and so the call could never be
  annotated static -- which is precisely the specialization an interpreter needs.

  `letIn` is the single deliberate exception, and it is the one that matters:
  in `letIn b e body` the BOUND VARIABLE takes `e`'s binding time while `b` is
  the binding time of the whole expression.  A static binding inside dynamic
  code -- `b = dyn` with `e` static -- is exactly the shape that makes
  specialization scale, because `mix` binds `e`'s value in its own environment
  and emits nothing.  Forbid it and every static intermediate is recomputed at
  each use in the residual program.
-/

import LeanSemanticPrimitives.Projection.ObjectLanguage

namespace Projection

inductive BT where
  | stat | dyn
  deriving DecidableEq, Inhabited, Repr

instance : BEq BT := ⟨fun a b => decide (a = b)⟩

/-- A division: the binding time of each variable in scope, de Bruijn order. -/
abbrev Div := List BT

/-! ## Annotated terms -/

inductive ATerm where
  | lit   : Val → ATerm                                  -- inherently static
  | var   : Nat → ATerm                                  -- BT read from `Δ`
  | letIn : BT → ATerm → ATerm → ATerm
  | ite   : BT → ATerm → ATerm → ATerm → ATerm
  | prim  : BT → Prim → List ATerm → ATerm
  | ctorT : BT → Nat → List ATerm → ATerm
  | caseT : BT → ATerm → List (Nat × Nat × ATerm) → ATerm
  | call  : BT → Nat → List ATerm → ATerm                -- residualize
  | ucall : BT → Nat → List ATerm → ATerm                -- unfold in place
  | lift  : ATerm → ATerm                                -- static value → code
  deriving Inhabited, Repr

abbrev AAlt := Nat × Nat × ATerm

@[inline] def AAlt.tag   (a : AAlt) : Nat   := a.1
@[inline] def AAlt.arity (a : AAlt) : Nat   := a.2.1
@[inline] def AAlt.body  (a : AAlt) : ATerm := a.2.2

/-- Parameter and return binding times are stored, not inferred on demand.

`ret` is what lets `btOf` stay program-independent: a call's binding time is
read off the call node instead of by looking the callee up, so nothing that
queries a binding time needs the program in scope.  `wfA` checks the stored
value against the callee, so the redundancy is verified rather than trusted. -/
structure AFunDef where
  params : Div
  ret    : BT
  body   : ATerm
  deriving Inhabited, Repr

structure AProgram where
  funs  : List AFunDef
  entry : Nat
  deriving Inhabited, Repr

@[inline] def AProgram.fn (P : AProgram) (i : Nat) : Option AFunDef := P.funs[i]?

/-! ## Binding time of a term

Non-recursive by construction: every case is either a stored annotation or a
lookup in `Δ`.  That matters twice -- `wfA` calls it on every operand, so a
recursive `btOf` would make well-formedness checking quadratic, and the
object-level `mix` has to compute it too. -/
def btOf (Δ : Div) : ATerm → BT
  | .lit _        => .stat
  | .var i        => (Δ[i]?).getD .dyn        -- unbound: pessimistically dynamic
  | .letIn b _ _  => b
  | .ite b _ _ _  => b
  | .prim b _ _   => b
  | .ctorT b _ _  => b
  | .caseT b _ _  => b
  | .call b _ _   => b
  | .ucall b _ _  => b
  | .lift _       => .dyn

/-! ## Erasure

The annotated program and the program it annotates must be the same program.
`erase` is how that is said, and `bta_sound` is where it is proved.  Note
`lift` erases to its operand: a lift is semantically the identity, which is
precisely why inserting lifts cannot change what a program computes. -/

mutual

def erase : ATerm → Term
  | .lit v        => .lit v
  | .var i        => .var i
  | .letIn _ e b  => .letIn (erase e) (erase b)
  | .ite _ c a b  => .ite (erase c) (erase a) (erase b)
  | .prim _ p ts  => .prim p (eraseList ts)
  | .ctorT _ k ts => .ctorT k (eraseList ts)
  | .caseT _ s as => .caseT (erase s) (eraseAlts as)
  | .call _ f ts  => .call f (eraseList ts)
  | .ucall _ f ts => .call f (eraseList ts)
  | .lift t       => erase t

def eraseList : List ATerm → List Term
  | []      => []
  | t :: ts => erase t :: eraseList ts

def eraseAlts : List AAlt → List Alt
  | []      => []
  | a :: as => (a.tag, a.arity, erase a.body) :: eraseAlts as

end

@[inline] def eraseFunDef (fd : AFunDef) : FunDef :=
  ⟨fd.params.length, erase fd.body⟩

def eraseFunDefs : List AFunDef → List FunDef
  | []        => []
  | fd :: fds => eraseFunDef fd :: eraseFunDefs fds

def eraseProgram (P : AProgram) : Program := ⟨eraseFunDefs P.funs, P.entry⟩

/-! ## Well-annotatedness

Decidable and `Bool`-valued: `bta` must be able to CHECK its own output, and a
`Prop` would leave the checker as a second thing to trust. -/

/-- Every element of `ts` has binding time `b` under `Δ`. -/
def allBT (Δ : Div) (b : BT) : List ATerm → Bool
  | []      => true
  | t :: ts => (btOf Δ t == b) && allBT Δ b ts

/-- The binding times a call site offers, in order. -/
def btsOf (Δ : Div) : List ATerm → Div
  | []      => []
  | t :: ts => btOf Δ t :: btsOf Δ ts

mutual

def wfA (P : AProgram) (Δ : Div) : ATerm → Bool
  | .lit _ => true
  | .var i => i < Δ.length
  | .letIn b e body =>
      -- the exception: the bound variable takes `e`'s binding time, and `b` is
      -- the binding time of the whole `letIn`
      wfA P Δ e && wfA P (btOf Δ e :: Δ) body && (b == btOf (btOf Δ e :: Δ) body)
      -- a DYNAMIC bound expression forces a dynamic `letIn`.  Without this a
      -- static body could discard a dynamic `e`, and dropping `e` from the
      -- residual program is unsound in the reverse direction: `e` may fail to
      -- evaluate, in which case the source has no value and the residual has
      -- one.  BTA never produces such an annotation (`dyn` only propagates
      -- outward), so this rules out ill-formed input rather than useful input.
      && ((btOf Δ e == .stat) || (b == .dyn))
  | .ite b c a e =>
      wfA P Δ c && wfA P Δ a && wfA P Δ e
      && (btOf Δ a == b) && (btOf Δ e == b)
      -- a static condition may carry dynamic branches; the reverse cannot hold
      && ((btOf Δ c == .stat) || (b == .dyn))
  | .prim b _ ts  => wfAs P Δ ts && allBT Δ b ts
  | .ctorT b _ ts => wfAs P Δ ts && allBT Δ b ts
  | .caseT b s as =>
      wfA P Δ s && ((btOf Δ s == .stat) || (b == .dyn))
      && wfAAlts P Δ (btOf Δ s) b as
  -- `call` and `ucall` have the SAME well-formedness rule; they differ only in
  -- what `mix` does with them (generate a specialized function and call it, vs
  -- inline the body here).  Which is right is a termination/size judgement, not
  -- a binding-time one, so it belongs in the annotation and not in `wfA`.
  | .call b f ts =>
      match P.fn f with
      | none    => false
      | some fd => wfAs P Δ ts && (btsOf Δ ts == fd.params) && (b == fd.ret)
  | .ucall b f ts =>
      match P.fn f with
      | none    => false
      | some fd => wfAs P Δ ts && (btsOf Δ ts == fd.params) && (b == fd.ret)
  | .lift t => wfA P Δ t && (btOf Δ t == .stat)

def wfAs (P : AProgram) (Δ : Div) : List ATerm → Bool
  | []      => true
  | t :: ts => wfA P Δ t && wfAs P Δ ts

/-- `fieldBT` is the binding time of the scrutinee, so the fields an alternative
binds are static exactly when the value being taken apart is. -/
def wfAAlts (P : AProgram) (Δ : Div) (fieldBT : BT) (bodyBT : BT) : List AAlt → Bool
  | []      => true
  | a :: as =>
      let Δ' := List.replicate a.arity fieldBT ++ Δ
      wfA P Δ' a.body && (btOf Δ' a.body == bodyBT) && wfAAlts P Δ fieldBT bodyBT as

end

def wfAFunDef (P : AProgram) (fd : AFunDef) : Bool :=
  wfA P fd.params fd.body && (btOf fd.params fd.body == fd.ret)

def wfAProgram (P : AProgram) : Bool :=
  P.funs.all (wfAFunDef P) && P.entry < P.funs.length

end Projection
