/-
  Binding-time analysis: infer a division, then annotate.

  Offline and monovariant -- one division per function, computed once by fixed
  point and shared by every call site.  Polyvariant BTA (a division per call
  site) is more precise and is the standard next step if precision turns out to
  bind; it is not needed to establish the projections.

  TWO THINGS ARE CHECKED RATHER THAN PROVED, deliberately.

  The FIXED POINT.  `btaFix` iterates a monotone step under a fuel bound and
  then verifies the result really is a fixed point.  Binding times only ever
  move `stat → dyn`, so the iteration does terminate -- but proving that means a
  decreasing measure over a list of lists of `BT`, and it buys nothing: if the
  check fails `bta` reports it, and whether that was possible is then moot.

  The ANNOTATION.  `bta` runs `wfAProgram` on its own output and refuses if it
  does not hold.  That makes `bta_sound` immediate instead of a second large
  induction, and turns a bug in `annotate` into a refusal rather than an
  ill-annotated program handed to `mix`.

  WHAT IS ACTUALLY PROVED is the half no check can supply: `eraseProgram (bta P
  …) = P`.  An annotation that is well-formed but describes a DIFFERENT program
  would pass every check in this file, and `mix_sound` relates the residual to
  the erasure -- so without this theorem the whole chain is anchored to nothing.
-/

import LeanSemanticPrimitives.Projection.BindingTime

namespace Projection

/-- `dyn` absorbs: a result is static only if everything it depends on is. -/
@[inline] def BT.join : BT → BT → BT
  | .stat, .stat => .stat
  | _,     _     => .dyn

/-- Parameter and return binding times of every function, in table order. -/
abbrev Divs := List (Div × BT)

@[inline] def Divs.params (D : Divs) (f : Nat) : Div := (D[f]?).elim [] Prod.fst
@[inline] def Divs.ret    (D : Divs) (f : Nat) : BT  := (D[f]?).elim .dyn Prod.snd

def Divs.update (D : Divs) (f : Nat) (g : Div × BT → Div × BT) : Divs :=
  match D, f with
  | [],      _     => []
  | d :: ds, 0     => g d :: ds
  | d :: ds, n + 1 => d :: Divs.update ds n g

/-! ## The binding time a term would have under a division -/

mutual

def btT (D : Divs) (Δ : Div) : Term → BT
  | .lit _      => .stat
  | .var i      => (Δ[i]?).getD .dyn
  -- a dynamic bound expression forces a dynamic `letIn`: the residual program
  -- must still evaluate it, so a static body may not drop it
  | .letIn e b  => BT.join (btT D Δ e) (btT D (btT D Δ e :: Δ) b)
  | .ite c a b  => BT.join (btT D Δ c) (BT.join (btT D Δ a) (btT D Δ b))
  | .prim _ ts  => btTs D Δ ts
  | .ctorT _ ts => btTs D Δ ts
  | .caseT s as => BT.join (btT D Δ s) (btAlts D Δ (btT D Δ s) as)
  | .call f _   => D.ret f

def btTs (D : Divs) (Δ : Div) : List Term → BT
  | []      => .stat
  | t :: ts => BT.join (btT D Δ t) (btTs D Δ ts)

def btAlts (D : Divs) (Δ : Div) (fieldBT : BT) : List Alt → BT
  | []      => .stat
  | a :: as => BT.join (btT D (List.replicate a.arity fieldBT ++ Δ) a.body)
                       (btAlts D Δ fieldBT as)

end

/-! ## Raising callee parameters

The only constraint a call site imposes on the division: an argument that is
dynamic here makes the corresponding parameter dynamic there.  Every update
joins, so the step only ever moves binding times upward. -/

def raiseAt : Div → List BT → Div
  | [],      _       => []
  | b :: bs, []      => b :: bs
  | b :: bs, c :: cs => BT.join b c :: raiseAt bs cs

mutual

def raiseT (D : Divs) (Δ : Div) : Term → Divs
  | .lit _     => D
  | .var _     => D
  | .letIn e b =>
      let D₁ := raiseT D Δ e
      raiseT D₁ (btT D₁ Δ e :: Δ) b
  | .ite c a b  => raiseT (raiseT (raiseT D Δ c) Δ a) Δ b
  | .prim _ ts  => raiseTs D Δ ts
  | .ctorT _ ts => raiseTs D Δ ts
  | .caseT s as =>
      let D₁ := raiseT D Δ s
      raiseAlts D₁ Δ (btT D₁ Δ s) as
  | .call f ts =>
      let D₁ := raiseTs D Δ ts
      D₁.update f fun fd => (raiseAt fd.1 (ts.map (btT D₁ Δ)), fd.2)

def raiseTs (D : Divs) (Δ : Div) : List Term → Divs
  | []      => D
  | t :: ts => raiseTs (raiseT D Δ t) Δ ts

def raiseAlts (D : Divs) (Δ : Div) (fieldBT : BT) : List Alt → Divs
  | []      => D
  | a :: as =>
      raiseAlts (raiseT D (List.replicate a.arity fieldBT ++ Δ) a.body) Δ fieldBT as

end

def raiseAll : Nat → List FunDef → Divs → Divs
  | _, [],       D => D
  | i, fd :: fs, D => raiseAll (i + 1) fs (raiseT D (D.params i) fd.body)

def recomputeRets : Nat → List FunDef → Divs → Divs
  | _, [],       D => D
  | i, fd :: fs, D =>
      recomputeRets (i + 1) fs (D.update i fun fd' => (fd'.1, BT.join fd'.2 (btT D fd'.1 fd.body)))

@[inline] def btaStep (P : Program) (D : Divs) : Divs :=
  recomputeRets 0 P.funs (raiseAll 0 P.funs D)

def divEq : Div → Div → Bool
  | [], []           => true
  | a :: as, b :: bs => (a == b) && divEq as bs
  | _, _             => false

def divsEq : Divs → Divs → Bool
  | [], []                         => true
  | (p₁, r₁) :: d₁, (p₂, r₂) :: d₂ => divEq p₁ p₂ && (r₁ == r₂) && divsEq d₁ d₂
  | _, _                           => false

inductive BTAError where
  | noFixpoint
  | notWellAnnotated
  | cannotLower       : String → BTAError
  | arityMismatch     : String → BTAError
  deriving Inhabited, Repr

def btaFix (P : Program) : Nat → Divs → Except BTAError Divs
  | 0,     _ => .error .noFixpoint
  | n + 1, D =>
      let D' := btaStep P D
      if divsEq D D' then .ok D else btaFix P n D'

/-- Everything starts as static except the entry's declared division; the fixed
point only ever raises. -/
def initDivs (P : Program) (entryDiv : Div) : Divs :=
  go 0 P.funs
where
  go : Nat → List FunDef → Divs
    | _, []       => []
    | i, fd :: fs =>
        (if i = P.entry then entryDiv else List.replicate fd.arity .stat, .stat) :: go (i + 1) fs

/-! ## Annotation

`coerce` is where every `lift` in the language comes from, and it is the only
place one is ever introduced.  Lowering is refused rather than approximated: a
term whose value genuinely depends on dynamic data cannot be made static, and
silently pretending otherwise is how a specializer computes with a value it does
not have. -/

def coerce (natural target : BT) (t : ATerm) : Except BTAError ATerm :=
  match natural, target with
  | .stat, .stat => .ok t
  | .dyn,  .dyn  => .ok t
  | .stat, .dyn  => .ok (.lift t)
  | .dyn,  .stat => .error (.cannotLower "a dynamic term cannot be made static")

mutual

/-- Explicit `match` rather than `do`: the erasure proof below splits on every
one of these, and `Except`'s bind does not expose a splittable match. -/
def annT (D : Divs) (inl : List Bool) (Δ : Div) (target : BT) :
    Term → Except BTAError ATerm
  | .lit v => coerce .stat target (.lit v)
  | .var i => coerce ((Δ[i]?).getD .dyn) target (.var i)
  | .letIn e b =>
      let be  := btT D Δ e
      let Δ'  := be :: Δ
      let nat := BT.join be (btT D Δ' b)
      match annT D inl Δ be e, annT D inl Δ' nat b with
      | .ok e',   .ok b' => coerce nat target (.letIn nat e' b')
      | .error x, _      => .error x
      | _,        .error x => .error x
  | .ite c a b =>
      let bc  := btT D Δ c
      let nat := BT.join bc (BT.join (btT D Δ a) (btT D Δ b))
      match annT D inl Δ bc c, annT D inl Δ nat a, annT D inl Δ nat b with
      | .ok c',   .ok a',   .ok b' => coerce nat target (.ite nat c' a' b')
      | .error x, _,        _      => .error x
      | _,        .error x, _      => .error x
      | _,        _,        .error x => .error x
  | .prim p ts =>
      let nat := btTs D Δ ts
      match annTs D inl Δ nat ts with
      | .ok ts'   => coerce nat target (.prim nat p ts')
      | .error x  => .error x
  | .ctorT k ts =>
      let nat := btTs D Δ ts
      match annTs D inl Δ nat ts with
      | .ok ts'   => coerce nat target (.ctorT nat k ts')
      | .error x  => .error x
  | .caseT s as =>
      let bs  := btT D Δ s
      let nat := BT.join bs (btAlts D Δ bs as)
      match annT D inl Δ bs s, annAlts D inl Δ bs nat as with
      | .ok s',   .ok as' => coerce nat target (.caseT nat s' as')
      | .error x, _       => .error x
      | _,        .error x => .error x
  | .call f ts =>
      match annArgs D inl Δ (D.params f) ts with
      | .error x => .error x
      | .ok ts'  =>
        let r := D.ret f
        -- `inline` is the surface's unfold-versus-residualize choice, carried
        -- through untouched: BTA infers binding times and nothing else
        if (inl[f]?).getD false then coerce r target (.ucall r f ts')
        else coerce r target (.call r f ts')

def annTs (D : Divs) (inl : List Bool) (Δ : Div) (target : BT) :
    List Term → Except BTAError (List ATerm)
  | []      => .ok []
  | t :: ts =>
      match annT D inl Δ target t, annTs D inl Δ target ts with
      | .ok t',   .ok ts' => .ok (t' :: ts')
      | .error x, _       => .error x
      | _,        .error x => .error x

/-- Each argument is annotated at its own parameter's binding time, which is
what makes `btsOf Δ ts' = fd.params` hold by construction. -/
def annArgs (D : Divs) (inl : List Bool) (Δ : Div) :
    Div → List Term → Except BTAError (List ATerm)
  | [],      []      => .ok []
  | b :: bs, t :: ts =>
      match annT D inl Δ b t, annArgs D inl Δ bs ts with
      | .ok t',   .ok ts' => .ok (t' :: ts')
      | .error x, _       => .error x
      | _,        .error x => .error x
  | [],      _ :: _  => .error (.arityMismatch "call: too many arguments for the division")
  | _ :: _,  []      => .error (.arityMismatch "call: too few arguments for the division")

def annAlts (D : Divs) (inl : List Bool) (Δ : Div) (fieldBT bodyBT : BT) :
    List Alt → Except BTAError (List AAlt)
  | []      => .ok []
  | a :: as =>
      match annT D inl (List.replicate a.arity fieldBT ++ Δ) bodyBT a.body,
            annAlts D inl Δ fieldBT bodyBT as with
      | .ok b',   .ok as' => .ok ((a.tag, a.arity, b') :: as')
      | .error x, _       => .error x
      | _,        .error x => .error x

end

def annFuns (D : Divs) (inl : List Bool) : Nat → List FunDef →
    Except BTAError (List AFunDef)
  | _, []       => .ok []
  | i, fd :: fs =>
      let ps := D.params i
      if ps.length = fd.arity then
        match annT D inl ps (D.ret i) fd.body, annFuns D inl (i + 1) fs with
        | .ok b',   .ok fs' => .ok (⟨ps, D.ret i, b'⟩ :: fs')
        | .error x, _       => .error x
        | _,        .error x => .error x
      else .error (.arityMismatch "function: division and body disagree on arity")

/-- Binding-time analysis.  `entryDiv` says which of the entry's parameters are
static; everything else is inferred. -/
def bta (P : Program) (inl : List Bool) (entryDiv : Div) (fuel : Nat) :
    Except BTAError AProgram :=
  match btaFix P fuel (initDivs P entryDiv) with
  | .error e => .error e
  | .ok D =>
    match annFuns D inl 0 P.funs with
    | .error e => .error e
    | .ok funs =>
      let A : AProgram := ⟨funs, P.entry⟩
      -- the checked half: `mix` is never handed an annotation this file could
      -- not verify
      if wfAProgram A then .ok A else .error .notWellAnnotated

/-! ## Annotation does not change the program

The half no check supplies.  `mix_sound` relates a residual program to the
ERASURE of the annotated program, so if `annotate` could quietly produce a
well-formed annotation of some other program, the chain would prove nothing
about `P`.  Every case is the same shape: erasure ignores annotations, `lift`
erases to its operand, so `coerce` is invisible. -/

theorem erase_coerce {nat target x y} (h : coerce nat target x = .ok y) :
    erase y = erase x := by
  unfold coerce at h
  split at h <;> first | (cases h; rfl) | (cases h; simp [erase]) | contradiction

mutual

theorem erase_annT : ∀ (D : Divs) (inl : List Bool) (Δ : Div) (target : BT) (t : Term)
    (t' : ATerm), annT D inl Δ target t = .ok t' → erase t' = t
  | D, inl, Δ, target, .lit v, t', h => by
      rw [erase_coerce h]; rfl
  | D, inl, Δ, target, .var i, t', h => by
      rw [erase_coerce h]; rfl
  | D, inl, Δ, target, .letIn e b, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i e' b' he hb
      rw [erase_coerce h, erase]
      rw [erase_annT D inl _ _ e e' he, erase_annT D inl _ _ b b' hb]
  | D, inl, Δ, target, .ite c a b, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i c' a' b' hc ha hb
      rw [erase_coerce h, erase]
      rw [erase_annT D inl _ _ c c' hc, erase_annT D inl _ _ a a' ha,
          erase_annT D inl _ _ b b' hb]
  | D, inl, Δ, target, .prim p ts, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i ts' hts
      rw [erase_coerce h, erase, erase_annTs D inl _ _ ts ts' hts]
  | D, inl, Δ, target, .ctorT k ts, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i ts' hts
      rw [erase_coerce h, erase, erase_annTs D inl _ _ ts ts' hts]
  | D, inl, Δ, target, .caseT s as, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i s' as' hs has
      rw [erase_coerce h, erase]
      rw [erase_annT D inl _ _ s s' hs, erase_annAlts D inl _ _ _ as as' has]
  | D, inl, Δ, target, .call f ts, t', h => by
      simp only [annT] at h
      split at h <;> try contradiction
      rename_i ts' hts
      -- the two branches differ only in `call` vs `ucall`, which erase alike
      split at h
      · rw [erase_coerce h, erase, erase_annArgs D inl _ _ ts ts' hts]
      · rw [erase_coerce h, erase, erase_annArgs D inl _ _ ts ts' hts]

theorem erase_annTs : ∀ (D : Divs) (inl : List Bool) (Δ : Div) (target : BT)
    (ts : List Term) (ts' : List ATerm), annTs D inl Δ target ts = .ok ts' →
    eraseList ts' = ts
  | _, _, _, _, [], ts', h => by simp only [annTs] at h; cases h; rfl
  | D, inl, Δ, target, t :: ts, ts', h => by
      simp only [annTs] at h
      split at h <;> try contradiction
      rename_i t' rs ht hts
      cases h
      rw [eraseList, erase_annT D inl _ _ t t' ht, erase_annTs D inl _ _ ts rs hts]

theorem erase_annArgs : ∀ (D : Divs) (inl : List Bool) (Δ : Div) (ps : Div)
    (ts : List Term) (ts' : List ATerm), annArgs D inl Δ ps ts = .ok ts' →
    eraseList ts' = ts
  | _, _, _, [],      [],      ts', h => by simp only [annArgs] at h; cases h; rfl
  | D, inl, Δ, b :: bs, t :: ts, ts', h => by
      simp only [annArgs] at h
      split at h <;> try contradiction
      rename_i t' rs ht hts
      cases h
      rw [eraseList, erase_annT D inl _ _ t t' ht, erase_annArgs D inl _ bs ts rs hts]
  | _, _, _, [],      _ :: _,  _,   h => by simp only [annArgs] at h; contradiction
  | _, _, _, _ :: _,  [],      _,   h => by simp only [annArgs] at h; contradiction

theorem erase_annAlts : ∀ (D : Divs) (inl : List Bool) (Δ : Div) (fieldBT bodyBT : BT)
    (as : List Alt) (as' : List AAlt), annAlts D inl Δ fieldBT bodyBT as = .ok as' →
    eraseAlts as' = as
  | _, _, _, _, _, [], as', h => by simp only [annAlts] at h; cases h; rfl
  | D, inl, Δ, fb, bb, a :: as, as', h => by
      simp only [annAlts] at h
      split at h <;> try contradiction
      rename_i b' rs hb has
      cases h
      rw [eraseAlts, AAlt.tag, AAlt.arity, AAlt.body,
          erase_annT D inl _ _ a.body b' hb, erase_annAlts D inl _ _ _ as rs has]
      rfl

end

theorem erase_annFuns : ∀ (D : Divs) (inl : List Bool) (i : Nat) (fs : List FunDef)
    (fs' : List AFunDef), annFuns D inl i fs = .ok fs' → eraseFunDefs fs' = fs
  | _, _, _, [], fs', h => by simp only [annFuns] at h; cases h; rfl
  | D, inl, i, fd :: fs, fs', h => by
      simp only [annFuns] at h
      split at h <;> try contradiction
      · rename_i hlen
        split at h <;> try contradiction
        rename_i b' rs hb hfs
        cases h
        rw [eraseFunDefs, eraseFunDef, erase_annT D inl _ _ fd.body b' hb,
            erase_annFuns D inl (i + 1) fs rs hfs, hlen]

theorem bta_erases {P inl d f A} (h : bta P inl d f = .ok A) : eraseProgram A = P := by
  simp only [bta] at h
  split at h <;> try contradiction
  split at h <;> try contradiction
  rename_i funs hfuns
  split at h <;> try contradiction
  cases h
  simp only [eraseProgram, erase_annFuns _ inl 0 P.funs funs hfuns]

theorem bta_sound {P inl d f A} (h : bta P inl d f = .ok A) : wfAProgram A = true := by
  simp only [bta] at h
  split at h <;> try contradiction
  split at h <;> try contradiction
  split at h <;> try contradiction
  rename_i hw
  cases h
  exact hw

end Projection
