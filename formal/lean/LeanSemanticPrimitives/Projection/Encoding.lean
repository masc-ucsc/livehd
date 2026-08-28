/-
  The Gödel encoding: `Term` and `Program` as ordinary `Val` data.

  This is the file that makes self-application possible.  `mix` is a program IN
  the object language, so the program it specializes cannot reach it as a Lean
  inductive -- it has to arrive as `Val`.  `encProgram` is that bridge, and
  `decProgram` is the inverse used to state what `mix`'s output means.

  THE ONLY THEOREM THAT MATTERS HERE is the round trip `dec (enc x) = some x`.
  Everything downstream needs exactly one thing from the encoding: that no two
  distinct programs encode alike, so "mix returned `encProgram r`" pins down `r`.
  Nothing needs `enc (dec v) = v` -- `Val` has junk (an arbitrary `ctor` with the
  wrong shape) and there is no reason to rule it out.

  LITERALS ARE NOT QUOTED.  `Term.lit v` encodes as `ctor tagLit [v]` with the
  payload stored RAW, not as `enc v`.  A separate value encoder would be pure
  cost: `Val` is already the data domain, the payload is never recursed into by
  the decoder or by `mix`, and round-tripping it is trivial precisely because it
  is the identity.  This is the standard "quotation is identity on data" that a
  closed first-order data domain buys.

  TAGS LIVE IN ONE PLACE.  Encoder, decoder and (later) `mix` written in L all
  refer to `tagLit`/`tagVar`/... below.  The decoder therefore dispatches with an
  `if t = tagLit then ...` chain rather than by matching numeric literals: a
  literal pattern would be a second copy of the table and could drift from it.
-/

import LeanSemanticPrimitives.Projection.ObjectLanguage

namespace Projection

/-! ## The tag table

Small numbers, deliberately far below `bvTag = 1000`, so encoded syntax and
encoded bit vectors can never be confused. -/

def tagLit     : Nat := 10
def tagVar     : Nat := 11
def tagLetIn   : Nat := 12
def tagIte     : Nat := 13
def tagPrim    : Nat := 14
def tagCtorT   : Nat := 15
def tagCaseT   : Nat := 16
def tagCall    : Nat := 17
def tagAlt     : Nat := 18
def tagFunDef  : Nat := 19
def tagProgram : Nat := 20

/-! ## Naturals

`Val` has `Int`, not `Nat`.  Decoding therefore has a real failure case (a
negative literal where an index was expected), which is why `decNat` is
`Option`-valued rather than a `toNat` coercion that would silently clamp. -/

@[inline] def encNat (n : Nat) : Val := .int (Int.ofNat n)

def decNat : Val → Option Nat
  | .int i => if 0 ≤ i then some i.toNat else none
  | _      => none

@[simp] theorem decNat_encNat (n : Nat) : decNat (encNat n) = some n := by
  simp [encNat, decNat]

/-! ## Primitives

A flat enum, so it encodes as its index.  Both directions are written out and
the round trip is `cases <;> rfl`; a `find?`-over-a-list definition would be
shorter to write and worse to reason about. -/

def primCode : Prim → Nat
  | .addI => 0  | .subI => 1  | .mulI => 2  | .divI => 3  | .modI => 4
  | .ltI  => 5  | .leI  => 6  | .eqI  => 7
  | .andB => 8  | .orB  => 9  | .notB => 10
  | .isNil => 11 | .hd => 12 | .tl => 13
  | .bvMk => 14 | .bvWidth => 15 | .bvUint => 16 | .bvBit => 17
  | .bvAnd => 18 | .bvOr => 19 | .bvXor => 20 | .bvNot => 21
  | .bvResize => 22
  | .consP => 23 | .eqV => 24

def primOfCode : Nat → Option Prim
  | 0 => some .addI  | 1 => some .subI  | 2 => some .mulI
  | 3 => some .divI  | 4 => some .modI
  | 5 => some .ltI   | 6 => some .leI   | 7 => some .eqI
  | 8 => some .andB  | 9 => some .orB   | 10 => some .notB
  | 11 => some .isNil | 12 => some .hd  | 13 => some .tl
  | 14 => some .bvMk | 15 => some .bvWidth | 16 => some .bvUint | 17 => some .bvBit
  | 18 => some .bvAnd | 19 => some .bvOr | 20 => some .bvXor | 21 => some .bvNot
  | 22 => some .bvResize
  | 23 => some .consP | 24 => some .eqV
  | _ => none

@[simp] theorem primOfCode_primCode (p : Prim) : primOfCode (primCode p) = some p := by
  cases p <;> rfl

@[inline] def encPrim (p : Prim) : Val := encNat (primCode p)

def decPrim (v : Val) : Option Prim :=
  match decNat v with
  | some n => primOfCode n
  | none   => none

@[simp] theorem decPrim_encPrim (p : Prim) : decPrim (encPrim p) = some p := by
  simp [encPrim, decPrim]

/-! ## Terms

Variable-length children (`prim` arguments, `caseT` alternatives, a function
table) become `cons`/`nil` chains rather than extra `ctor` fields: that is the
shape `mix` can walk with `isNil`/`hd`/`tl`, which are primitives, whereas
indexing into a `ctor`'s field list is not. -/

mutual

def encTerm : Term → Val
  | .lit v      => .ctor tagLit   [v]                       -- payload RAW
  | .var i      => .ctor tagVar   [encNat i]
  | .letIn e b  => .ctor tagLetIn [encTerm e, encTerm b]
  | .ite c a b  => .ctor tagIte   [encTerm c, encTerm a, encTerm b]
  | .prim p ts  => .ctor tagPrim  [encPrim p, encTerms ts]
  | .ctorT k ts => .ctor tagCtorT [encNat k, encTerms ts]
  | .caseT s as => .ctor tagCaseT [encTerm s, encAlts as]
  | .call f ts  => .ctor tagCall  [encNat f, encTerms ts]

def encTerms : List Term → Val
  | []      => .nil
  | t :: ts => .cons (encTerm t) (encTerms ts)

def encAlts : List Alt → Val
  | []      => .nil
  | a :: as => .cons (.ctor tagAlt [encNat a.tag, encNat a.arity, encTerm a.body])
                     (encAlts as)

end

mutual

/-- Dispatch on the FIELD COUNT first, then on the tag.

The obvious shape -- one `if` per tag, each re-matching `fs` -- is equivalent but
much worse to prove about: the retraction below has to descend an eight-deep
`if` chain with a nested `match` at every level.  Grouping by arity makes each
chain at most five long and puts the field names in scope once. -/
def decTerm : Val → Option Term
  | .ctor tg [a] =>
    if tg = tagLit then some (.lit a)
    else if tg = tagVar then (decNat a).map .var
    else none
  | .ctor tg [a, b] =>
    if tg = tagLetIn then
      match decTerm a, decTerm b with
      | some a', some b' => some (.letIn a' b')
      | _, _ => none
    else if tg = tagPrim then
      match decPrim a, decTerms b with
      | some p', some ts' => some (.prim p' ts')
      | _, _ => none
    else if tg = tagCtorT then
      match decNat a, decTerms b with
      | some k', some ts' => some (.ctorT k' ts')
      | _, _ => none
    else if tg = tagCaseT then
      match decTerm a, decAlts b with
      | some s', some as' => some (.caseT s' as')
      | _, _ => none
    else if tg = tagCall then
      match decNat a, decTerms b with
      | some f', some ts' => some (.call f' ts')
      | _, _ => none
    else none
  | .ctor tg [a, b, c] =>
    if tg = tagIte then
      match decTerm a, decTerm b, decTerm c with
      | some a', some b', some c' => some (.ite a' b' c')
      | _, _, _ => none
    else none
  | _ => none

def decTerms : Val → Option (List Term)
  | .nil      => some []
  | .cons a b => match decTerm a, decTerms b with
                 | some t, some ts => some (t :: ts)
                 | _, _ => none
  | _         => none

def decAlts : Val → Option (List Alt)
  | .nil => some []
  | .cons (.ctor tg [k, n, b]) rest =>
    if tg = tagAlt then
      match decNat k, decNat n, decTerm b, decAlts rest with
      | some k', some n', some b', some rest' => some ((k', n', b') :: rest')
      | _, _, _, _ => none
    else none
  | _ => none

end

/-! ### The round trip

Mutual, because the encoders are.  Each case is "unfold both sides, discharge
the tag comparison, appeal to the recursive results" -- so each is a `simp` with
the recursive calls supplied as rewrite rules. -/

mutual

theorem decTerm_encTerm : ∀ t : Term, decTerm (encTerm t) = some t
  | .lit v      => by simp [encTerm, decTerm]
  | .var i      => by simp [encTerm, decTerm, tagLit, tagVar]
  | .letIn e b  => by
      simp [encTerm, decTerm, tagLetIn, decTerm_encTerm e, decTerm_encTerm b]
  | .ite c a b  => by
      simp [encTerm, decTerm, tagIte,
            decTerm_encTerm c, decTerm_encTerm a, decTerm_encTerm b]
  | .prim p ts  => by
      simp [encTerm, decTerm, tagLetIn, tagPrim, decTerms_encTerms ts]
  | .ctorT k ts => by
      simp [encTerm, decTerm, tagLetIn, tagPrim, tagCtorT, decTerms_encTerms ts]
  | .caseT s as => by
      simp [encTerm, decTerm, tagLetIn, tagPrim, tagCtorT, tagCaseT,
            decTerm_encTerm s, decAlts_encAlts as]
  | .call f ts  => by
      simp [encTerm, decTerm, tagLetIn, tagPrim, tagCtorT, tagCaseT, tagCall,
            decTerms_encTerms ts]

theorem decTerms_encTerms : ∀ ts : List Term, decTerms (encTerms ts) = some ts
  | []      => by simp [encTerms, decTerms]
  | t :: ts => by
      simp [encTerms, decTerms, decTerm_encTerm t, decTerms_encTerms ts]

theorem decAlts_encAlts : ∀ as : List Alt, decAlts (encAlts as) = some as
  | []              => by simp [encAlts, decAlts]
  | (k, n, b) :: as => by
      simp [encAlts, decAlts, tagAlt, Alt.tag, Alt.arity, Alt.body,
            decTerm_encTerm b, decAlts_encAlts as]

end

/-! ### The retraction

`dec` is a left inverse of `enc` above; here it is also a RIGHT inverse on the
values it accepts.  `Val` has junk -- a `ctor` with the wrong tag or the wrong
field count -- and the point of this direction is that the junk is exactly what
`dec` rejects: anything it accepts is literally an encoded term.

This is what lets a `Val` produced by the OBJECT-level specializer be treated as
an encoded program without going back through the Lean-level one, which is the
composition the second projection needs. -/

theorem encNat_decNat : ∀ {v : Val} {n : Nat}, decNat v = some n → encNat n = v
  | .int i, n, h => by
      simp only [decNat] at h
      split at h
      · rename_i hi
        cases h
        simp only [encNat]
        congr 1
        exact Int.toNat_of_nonneg hi
      · contradiction
  | .bool _, _, h => by simp [decNat] at h
  | .nil,    _, h => by simp [decNat] at h
  | .cons _ _, _, h => by simp [decNat] at h
  | .ctor _ _, _, h => by simp [decNat] at h

theorem primCode_primOfCode : ∀ {n : Nat} {p : Prim}, primOfCode n = some p → primCode p = n := by
  intro n p h
  unfold primOfCode at h
  split at h <;> first | (cases h; rfl) | contradiction

theorem encPrim_decPrim {v : Val} {p : Prim} (h : decPrim v = some p) : encPrim p = v := by
  simp only [decPrim] at h
  split at h
  · rename_i n hn
    rw [encPrim, primCode_primOfCode h]
    exact encNat_decNat hn
  · contradiction

mutual

theorem encTerm_decTerm : ∀ (v : Val) (t : Term), decTerm v = some t → encTerm t = v
  | .ctor tg [a], t, h => by
      simp only [decTerm] at h
      split at h
      · rename_i htg; cases h; subst htg; simp [encTerm]
      · split at h
        · rename_i htg
          -- `Option.map` is not a `match`, so `split` does not apply here
          cases hn : decNat a with
          | none   => rw [hn] at h; simp at h
          | some n =>
            rw [hn] at h; simp at h
            subst h; subst htg
            simp [encTerm, encNat_decNat hn]
        · contradiction
  | .ctor tg [a, b], t, h => by
      simp only [decTerm] at h
      split at h
      · rename_i htg; split at h
        · rename_i a' b' ha hb
          cases h; subst htg
          simp [encTerm, encTerm_decTerm a a' ha, encTerm_decTerm b b' hb]
        · contradiction
      · split at h
        · rename_i htg; split at h
          · rename_i p' ts' hp hts
            cases h; subst htg
            simp [encTerm, encPrim_decPrim hp, encTerms_decTerms b ts' hts]
          · contradiction
        · split at h
          · rename_i htg; split at h
            · rename_i k' ts' hk hts
              cases h; subst htg
              simp [encTerm, encNat_decNat hk, encTerms_decTerms b ts' hts]
            · contradiction
          · split at h
            · rename_i htg; split at h
              · rename_i s' as' hs has
                cases h; subst htg
                simp [encTerm, encTerm_decTerm a s' hs, encAlts_decAlts b as' has]
              · contradiction
            · split at h
              · rename_i htg; split at h
                · rename_i f' ts' hf hts
                  cases h; subst htg
                  simp [encTerm, encNat_decNat hf, encTerms_decTerms b ts' hts]
                · contradiction
              · contradiction
  | .ctor tg [a, b, c], t, h => by
      simp only [decTerm] at h
      split at h
      · rename_i htg; split at h
        · rename_i a' b' c' ha hb hc
          cases h; subst htg
          simp [encTerm, encTerm_decTerm a a' ha, encTerm_decTerm b b' hb,
                encTerm_decTerm c c' hc]
        · contradiction
      · contradiction
  | .int _,    _, h => by simp [decTerm] at h
  | .bool _,   _, h => by simp [decTerm] at h
  | .nil,      _, h => by simp [decTerm] at h
  | .cons _ _, _, h => by simp [decTerm] at h
  | .ctor _ [], _, h => by simp [decTerm] at h
  | .ctor _ (_ :: _ :: _ :: _ :: _), _, h => by simp [decTerm] at h

theorem encTerms_decTerms : ∀ (v : Val) (ts : List Term), decTerms v = some ts → encTerms ts = v
  | .nil, ts, h => by simp only [decTerms] at h; cases h; simp [encTerms]
  | .cons a b, ts, h => by
      simp only [decTerms] at h
      split at h
      · rename_i t' ts' ht hts
        cases h
        simp [encTerms, encTerm_decTerm a t' ht, encTerms_decTerms b ts' hts]
      · contradiction
  | .int _,  _, h => by simp [decTerms] at h
  | .bool _, _, h => by simp [decTerms] at h
  | .ctor _ _, _, h => by simp [decTerms] at h

theorem encAlts_decAlts : ∀ (v : Val) (as : List Alt), decAlts v = some as → encAlts as = v
  | .nil, as, h => by simp only [decAlts] at h; cases h; simp [encAlts]
  | .cons (.ctor tg [k, n, b]) rest, as, h => by
      simp only [decAlts] at h
      split at h
      · rename_i htg; split at h
        · rename_i k' n' b' rest' hk hn hb hrest
          cases h; subst htg
          simp [encAlts, Alt.tag, Alt.arity, Alt.body, encNat_decNat hk,
                encNat_decNat hn, encTerm_decTerm b b' hb, encAlts_decAlts rest rest' hrest]
        · contradiction
      · contradiction
  | .int _,  _, h => by simp [decAlts] at h
  | .bool _, _, h => by simp [decAlts] at h
  | .ctor _ _, _, h => by simp [decAlts] at h
  | .cons (.int _) _,    _, h => by simp [decAlts] at h
  | .cons (.bool _) _,   _, h => by simp [decAlts] at h
  | .cons .nil _,        _, h => by simp [decAlts] at h
  | .cons (.cons _ _) _, _, h => by simp [decAlts] at h
  | .cons (.ctor _ []) _, _, h => by simp [decAlts] at h
  | .cons (.ctor _ [_]) _, _, h => by simp [decAlts] at h
  | .cons (.ctor _ [_, _]) _, _, h => by simp [decAlts] at h
  | .cons (.ctor _ (_ :: _ :: _ :: _ :: _)) _, _, h => by simp [decAlts] at h

end

/-! ## Programs -/

@[inline] def encFunDef (fd : FunDef) : Val :=
  .ctor tagFunDef [encNat fd.arity, encTerm fd.body]

def encFunDefs : List FunDef → Val
  | []        => .nil
  | fd :: fds => .cons (encFunDef fd) (encFunDefs fds)

def encProgram (P : Program) : Val :=
  .ctor tagProgram [encFunDefs P.funs, encNat P.entry]

def decFunDef : Val → Option FunDef
  | .ctor t [a, b] =>
    if t = tagFunDef then
      match decNat a, decTerm b with
      | some a', some b' => some ⟨a', b'⟩
      | _, _ => none
    else none
  | _ => none

def decFunDefs : Val → Option (List FunDef)
  | .nil      => some []
  | .cons a b => match decFunDef a, decFunDefs b with
                 | some fd, some fds => some (fd :: fds)
                 | _, _ => none
  | _         => none

def decProgram : Val → Option Program
  | .ctor t [fs, e] =>
    if t = tagProgram then
      match decFunDefs fs, decNat e with
      | some fs', some e' => some ⟨fs', e'⟩
      | _, _ => none
    else none
  | _ => none

@[simp] theorem decFunDef_encFunDef (fd : FunDef) : decFunDef (encFunDef fd) = some fd := by
  simp [encFunDef, decFunDef, tagFunDef, decTerm_encTerm]

@[simp] theorem decFunDefs_encFunDefs : ∀ fds : List FunDef,
    decFunDefs (encFunDefs fds) = some fds
  | []        => by simp [encFunDefs, decFunDefs]
  | fd :: fds => by simp [encFunDefs, decFunDefs, decFunDefs_encFunDefs fds]

@[simp] theorem decProgram_encProgram (P : Program) : decProgram (encProgram P) = some P := by
  simp [encProgram, decProgram, tagProgram]

theorem encFunDef_decFunDef : ∀ (v : Val) (fd : FunDef), decFunDef v = some fd → encFunDef fd = v
  | .ctor tg [a, b], fd, h => by
      simp only [decFunDef] at h
      split at h
      · rename_i htg; split at h
        · rename_i a' b' ha hb
          cases h; subst htg
          simp [encFunDef, encNat_decNat ha, encTerm_decTerm b b' hb]
        · contradiction
      · contradiction
  | .int _,  _, h => by simp [decFunDef] at h
  | .bool _, _, h => by simp [decFunDef] at h
  | .nil,    _, h => by simp [decFunDef] at h
  | .cons _ _, _, h => by simp [decFunDef] at h
  | .ctor _ [], _, h => by simp [decFunDef] at h
  | .ctor _ [_], _, h => by simp [decFunDef] at h
  | .ctor _ (_ :: _ :: _ :: _), _, h => by simp [decFunDef] at h

theorem encFunDefs_decFunDefs : ∀ (v : Val) (fds : List FunDef),
    decFunDefs v = some fds → encFunDefs fds = v
  | .nil, fds, h => by simp only [decFunDefs] at h; cases h; simp [encFunDefs]
  | .cons a b, fds, h => by
      simp only [decFunDefs] at h
      split at h
      · rename_i fd' fds' hfd hfds
        cases h
        simp [encFunDefs, encFunDef_decFunDef a fd' hfd, encFunDefs_decFunDefs b fds' hfds]
      · contradiction
  | .int _,  _, h => by simp [decFunDefs] at h
  | .bool _, _, h => by simp [decFunDefs] at h
  | .ctor _ _, _, h => by simp [decFunDefs] at h

theorem encProgram_decProgram : ∀ (v : Val) (P : Program), decProgram v = some P → encProgram P = v
  | .ctor tg [a, b], P, h => by
      simp only [decProgram] at h
      split at h
      · rename_i htg; split at h
        · rename_i fs' e' hfs he
          cases h; subst htg
          simp [encProgram, encFunDefs_decFunDefs a fs' hfs, encNat_decNat he]
        · contradiction
      · contradiction
  | .int _,  _, h => by simp [decProgram] at h
  | .bool _, _, h => by simp [decProgram] at h
  | .nil,    _, h => by simp [decProgram] at h
  | .cons _ _, _, h => by simp [decProgram] at h
  | .ctor _ [], _, h => by simp [decProgram] at h
  | .ctor _ [_], _, h => by simp [decProgram] at h
  | .ctor _ (_ :: _ :: _ :: _), _, h => by simp [decProgram] at h

/-! ## Environments

An `Env` is a `List Val`, and its elements are already data, so this is the
`cons`-chain packaging and nothing more -- the same "quotation is identity"
as literals. -/

def encEnv : Env → Val
  | []      => .nil
  | v :: vs => .cons v (encEnv vs)

def decEnv : Val → Option Env
  | .nil      => some []
  | .cons a b => (decEnv b).map (a :: ·)
  | _         => none

@[simp] theorem decEnv_encEnv : ∀ ρ : Env, decEnv (encEnv ρ) = some ρ
  | []      => by simp [encEnv, decEnv]
  | v :: vs => by simp [encEnv, decEnv, decEnv_encEnv vs]

theorem encEnv_decEnv : ∀ (v : Val) (ρ : Env), decEnv v = some ρ → encEnv ρ = v
  | .nil, ρ, h => by simp only [decEnv] at h; cases h; simp [encEnv]
  | .cons a b, ρ, h => by
      simp only [decEnv] at h
      cases hb : decEnv b with
      | none    => rw [hb] at h; simp at h
      | some ρ' =>
        rw [hb] at h; simp at h
        subst h
        simp [encEnv, encEnv_decEnv b ρ' hb]
  | .int _,  _, h => by simp [decEnv] at h
  | .bool _, _, h => by simp [decEnv] at h
  | .ctor _ _, _, h => by simp [decEnv] at h

/-! ## Injectivity

The property everything downstream actually uses: `mix` returning `encProgram r`
determines `r`. -/

theorem encTerm_inj {t u : Term} (h : encTerm t = encTerm u) : t = u := by
  have := decTerm_encTerm t
  rw [h, decTerm_encTerm u] at this
  exact (Option.some.inj this).symm

theorem encProgram_inj {P Q : Program} (h : encProgram P = encProgram Q) : P = Q := by
  have := decProgram_encProgram P
  rw [h, decProgram_encProgram Q] at this
  exact (Option.some.inj this).symm

end Projection
