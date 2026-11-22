/-
# Shared list tails in the FP residual

The memory route's state keeps the flops in one list, and the specializer reads flop `k` as
`k` nested `tl` steps from the head of that list, inside the read itself:
`letIn (tl x) (letIn (tl (var 0)) ... (hd (var 0)))`. Each read repeats every step before it,
so a residual holds O(F²) `tl` bindings for F flops: about 28M of CVA6's 29.2M terms.

`shareProgram` is a pass on the specializer's output; the specializer is unchanged. Right after
a list is bound (a `let`, the fields of a `caseT` alternative, a function's parameters), the
rewritten program binds the list's tails `tl¹ … tlᴷ` once, and every read `let x := tl y`
whose tail is stored becomes a reference to the stored one (the binding is dropped and the
de Bruijn indices renumbered). A read deeper than the stored chain keeps its `tl` binding.
How deep to store (`K`, per binder) comes from a separate first pass (`depthsOf`); the proof
holds for ANY `K`, so `depthsOf` carries no proof obligation.

## Direction of the theorem, and the fallback

A stored tail is computed before the program knows whether it will read that deep. On a list
shorter than the stored chain, or on a value that is not a list, the rewritten program stops
(`tl` of a non-`cons` has no value) where the original program, which takes some other branch,
may not. So the pass does NOT preserve every evaluation of the original program. It preserves
every evaluation of its OWN output (`go_sound`, `shareProgram_sound`): whenever the rewritten
program evaluates to `v`, the original evaluates to `v`.

The step therefore runs the rewritten program and, when it gives no value, the original one
(`stepEnc`). That step is EQUAL to `runMemSimEnc` on every input (`stepEnc_eq`), and hence
`runMemSimS` to `runMemSim` (`runMemSimS_eq`), given only the bundle's own check
`checkResidual sim.prog = some sim.bound`, which every honest bundle carries (`MemSimWFAt*`).
Every route's `runMemSim…_correct` theorem then holds for the shared step unchanged.

## Linear time

The rewrite threads its context through the traversal and restores it on the way out
(`go_ctx`), so the context arrays are updated in place.

Mathlib-free.
-/
import LeanSemanticPrimitives.Projection.MemRunFast

namespace Projection
namespace ShareTails

open Compiler Hw HwMem MemProjection

/-! ## The rewrite -/

/-- What the rewrite knows about one binding of the ORIGINAL term (by its level, counted from
the outermost binding): where its value sits in the rewritten term, and its place in a tail
chain. All levels here are levels of the REWRITTEN term. -/
structure Ent where
  /-- the level holding this binding's value -/
  nl : Nat
  /-- the level of its root: the binding whose `tl`-chain it is -/
  r  : Nat
  /-- how many `tl` steps below the root -/
  d  : Nat
  /-- how deep the root's stored chain goes -/
  k  : Nat
  /-- the level of the root's first stored tail; `tlʲ⁺¹` of the root is at `hb + j`, `j < k` -/
  hb : Nat
  deriving Inhabited, Repr

structure Ctx where
  /-- one entry per binding of the original term in scope, outermost first -/
  ent : Array Ent
  /-- the number of bindings of the rewritten term in scope -/
  nd  : Nat
  /-- the next binder number (the index into the depth oracle) -/
  bid : Nat

/-- `e` is `tl (var j)`. -/
def isTlVar : Term → Option Nat
  | .prim .tl [.var j] => some j
  | _ => none

/-- `n` stored tails: `tl (var src)`, then each next one `tl` of the one before, around `b`. -/
def chain (src : Nat) : Nat → Term → Term
  | 0,     b => b
  | n + 1, b => .letIn (.prim .tl [.var src]) (chain 0 n b)

/-- The entries of a binder group: binder `j` sits at level `base + j`, is its own root, and
stores `Ks[j]` tails, which follow those of the binders before it, from level `hb`. -/
def ents (base hb : Nat) : List Nat → List Ent
  | []      => []
  | K :: Ks => ⟨base, base, 0, K, hb⟩ :: ents (base + 1) (hb + K) Ks

/-- The stored chains of a binder group, in the group's order, at depth `dep`. -/
def emit : List Ent → Nat → Term → Term
  | [],      _,   b => b
  | e :: es, dep, b => chain (dep - 1 - e.r) e.k (emit es (dep + e.k) b)

def sumK : List Nat → Nat
  | []      => 0
  | K :: Ks => K + sumK Ks

/-- The oracle's depths for `n` binders numbered from `bid`. -/
def ksOf (ks : Array Nat) (bid : Nat) : Nat → List Nat
  | 0     => []
  | n + 1 => ks[bid]?.getD 0 :: ksOf ks (bid + 1) n

def pushAll : Array Ent → List Ent → Array Ent
  | a, []      => a
  | a, e :: es => pushAll (a.push e) es

def popN : Array Ent → Nat → Array Ent
  | a, 0     => a
  | a, n + 1 => popN a.pop n

mutual

/-- The rewrite of one term, in context `c`; the returned context equals `c` but for the binder
counter (`go_ctx`). -/
def go (ks : Array Nat) : Ctx → Term → Term × Ctx
  | c, .lit v => (.lit v, c)
  | c, .var i => (.var (c.nd - 1 - (c.ent[c.ent.size - 1 - i]?.getD default).nl), c)
  | c, .letIn e b =>
    match isTlVar e with
    | some j =>
      let en := c.ent[c.ent.size - 1 - j]?.getD default
      let nd := c.nd
      if en.d + 1 ≤ en.k then
        -- the tail is stored: this binding becomes a reference to it
        let r := go ks { ent := c.ent.push ⟨en.hb + en.d, en.r, en.d + 1, en.k, en.hb⟩,
                         nd := nd, bid := c.bid + 1 } b
        (r.1, { r.2 with ent := r.2.ent.pop, nd := nd })
      else
        let r := go ks { ent := c.ent.push ⟨nd, en.r, en.d + 1, en.k, en.hb⟩,
                         nd := nd + 1, bid := c.bid + 1 } b
        (.letIn (.prim .tl [.var (nd - 1 - en.nl)]) r.1, { r.2 with ent := r.2.ent.pop, nd := nd })
    | none =>
      let r1 := go ks c e
      let nd := r1.2.nd
      let es := ents nd (nd + 1) [ks[r1.2.bid]?.getD 0]
      let r2 := go ks { ent := pushAll r1.2.ent es, nd := nd + 1 + sumK [ks[r1.2.bid]?.getD 0],
                        bid := r1.2.bid + 1 } b
      (.letIn r1.1 (emit es (nd + 1) r2.1), { r2.2 with ent := popN r2.2.ent 1, nd := nd })
  | c, .ite a b d =>
    let r1 := go ks c a
    let r2 := go ks r1.2 b
    let r3 := go ks r2.2 d
    (.ite r1.1 r2.1 r3.1, r3.2)
  | c, .prim p ts =>
    let r := goL ks c ts
    (.prim p r.1, r.2)
  | c, .ctorT k ts =>
    let r := goL ks c ts
    (.ctorT k r.1, r.2)
  | c, .caseT s alts =>
    let r1 := go ks c s
    let r2 := goA ks r1.2 alts
    (.caseT r1.1 r2.1, r2.2)
  | c, .call f ts =>
    let r := goL ks c ts
    (.call f r.1, r.2)

def goL (ks : Array Nat) : Ctx → List Term → List Term × Ctx
  | c, []      => ([], c)
  | c, t :: ts =>
    let r1 := go ks c t
    let r2 := goL ks r1.2 ts
    (r1.1 :: r2.1, r2.2)

def goA (ks : Array Nat) : Ctx → List Alt → List Alt × Ctx
  | c, []                 => ([], c)
  | c, (tag, n, body) :: as =>
    let nd := c.nd
    let Ks := ksOf ks c.bid n
    let es := ents nd (nd + n) Ks
    let r1 := go ks { ent := pushAll c.ent es, nd := nd + n + sumK Ks, bid := c.bid + n } body
    let r2 := goA ks { r1.2 with ent := popN r1.2.ent n, nd := nd } as
    ((tag, n, emit es (nd + n) r1.1) :: r2.1, r2.2)

end

/-- A function body: its `n` parameters are a binder group at levels `0 … n-1`. -/
def goTop (ks : Array Nat) (n : Nat) (body : Term) : Term :=
  let Ks := ksOf ks 0 n
  let es := ents 0 n Ks
  emit es n (go ks { ent := pushAll #[] es, nd := n + sumK Ks, bid := n } body).1

/-! ## The depth oracle (no proof obligation)

The deepest tail of each binder that a read takes, numbering binders in the order `go` does. -/

structure DState where
  /-- per binding in scope, outermost first: (root binder number, depth below it) -/
  stk : Array (Nat × Nat)
  /-- per binder number, the deepest tail read so far -/
  ks  : Array Nat

mutual

partial def depthsT : DState → Term → DState
  | s, .lit _ => s
  | s, .var _ => s
  | s, .letIn e b =>
    match isTlVar e with
    | some j =>
      let (rb, d) := s.stk[s.stk.size - 1 - j]?.getD (0, 0)
      let ks := if rb < s.ks.size then s.ks.modify rb (max (d + 1)) else s.ks
      let s1 := depthsT { stk := s.stk.push (rb, d + 1), ks := ks.push 0 } b
      { s1 with stk := s1.stk.pop }
    | none =>
      let s1 := depthsT s e
      let me := s1.ks.size
      let s2 := depthsT { stk := s1.stk.push (me, 0), ks := s1.ks.push 0 } b
      { s2 with stk := s2.stk.pop }
  | s, .ite a b d => depthsT (depthsT (depthsT s a) b) d
  | s, .prim _ ts | s, .ctorT _ ts | s, .call _ ts => depthsL s ts
  | s, .caseT sc alts => depthsA (depthsT s sc) alts

partial def depthsL : DState → List Term → DState
  | s, []      => s
  | s, t :: ts => depthsL (depthsT s t) ts

partial def depthsA : DState → List Alt → DState
  | s, []                  => s
  | s, (_, n, body) :: as =>
    let base := s.ks.size
    let stk := (List.range n).foldl (fun a j => a.push (base + j, 0)) s.stk
    let ks := (List.range n).foldl (fun a _ => a.push 0) s.ks
    let s1 := depthsT { stk := stk, ks := ks } body
    depthsA { s1 with stk := (List.range n).foldl (fun a _ => a.pop) s1.stk } as

end

/-- The oracle for a function: binder `b`'s depth is `(depthsOf fd)[b]`. -/
def depthsOf (fd : FunDef) : Array Nat :=
  let s0 : DState := { stk := (List.range fd.arity).foldl (fun a j => a.push (j, 0)) #[],
                       ks := (List.range fd.arity).foldl (fun a _ => a.push 0) #[] }
  (depthsT s0 fd.body).ks

def shareFun (fd : FunDef) : FunDef :=
  { fd with body := goTop (depthsOf fd) fd.arity fd.body }

/-- **The pass.** Every function body, rewritten. -/
def shareProgram (R : Program) : Program :=
  { R with funs := R.funs.map shareFun }

/-! ## The step -/

/-- The rewritten residual and the bound `checkResidual` gives it, when it accepts it. -/
def shareSim (sim : MemSimulator) : Option (Program × Nat) :=
  let R2 := shareProgram sim.prog
  (checkResidual R2).map (fun b => (R2, b))

/-- One step on an encoded state: the rewritten residual, and the original one when the
rewritten one gives no value. -/
def stepEnc (sim : MemSimulator) (sh : Option (Program × Nat)) (e : ClockEdges)
    (i : RuntimeInput) (sv : Val) : Option Val :=
  match sh with
  | none => MemRunFast.runMemSimEnc sim e i sv
  | some (R2, b2) =>
    match evalFuel b2 R2 [] (.call R2.entry [.lit (encEdges e), .lit (encInput i), .lit sv]) with
    | .value v => some v
    | _        => MemRunFast.runMemSimEnc sim e i sv

/-- `runMemSim`, through `stepEnc`. -/
def runMemSimS (sim : MemSimulator) (sh : Option (Program × Nat)) (e : ClockEdges)
    (i : RuntimeInput) (st : StateM) : Option StateM.ResultM :=
  (stepEnc sim sh e i (StateM.encStateM st)).bind StateM.decResultM

/-! ## Proof: what the rewritten program computes, the original computes

The induction runs over the ORIGINAL term and inverts the rewritten one's evaluation. Levels
are counted from the outermost binding (`lv`), so pushing a binding leaves every lower level
unchanged; `Inv` ties each binding of the original term to the rewritten term's levels. -/

def tlV : Val → Option Val
  | .cons _ d => some d
  | _         => none

/-- `n` tails of `v`. -/
def tlN : Nat → Val → Option Val
  | 0,     v => some v
  | n + 1, v => (tlN n v).bind tlV

theorem tlN_succ' : ∀ (n : Nat) (v : Val), tlN (n + 1) v = (tlV v).bind (tlN n)
  | 0, v => by cases h : tlV v <;> simp [tlN, h]
  | n + 1, v => by
    show (tlN (n + 1) v).bind tlV = (tlV v).bind (tlN (n + 1))
    rw [tlN_succ' n v]
    cases tlV v <;> simp [tlN]

theorem evalPrim_tl : ∀ {u v : Val}, evalPrim .tl [u] = .ok v → tlV u = some v
  | .cons _ _, _, h => by simp only [evalPrim] at h; cases h; rfl
  | .int _,  _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .bool _, _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .nil,    _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .ctor _ _, _, h => by simp only [evalPrim] at h; split at h <;> cases h

theorem evalPrim_tl_of : ∀ {u v : Val}, tlV u = some v → evalPrim .tl [u] = .ok v
  | .cons _ _, _, h => by simp only [tlV, Option.some.injEq] at h; subst h; rfl
  | .int _,  _, h => by simp [tlV] at h
  | .bool _, _, h => by simp [tlV] at h
  | .nil,    _, h => by simp [tlV] at h
  | .ctor _ _, _, h => by simp [tlV] at h

/-! ### Levels -/

/-- The value at level `l`, counted from the outermost binding. -/
def lv (ρ : Env) (l : Nat) : Option Val := ρ.reverse[l]?

theorem lv_append_lt {xs σ : Env} {l : Nat} (h : l < σ.length) : lv (xs ++ σ) l = lv σ l := by
  unfold lv
  rw [List.reverse_append, List.getElem?_append_left (by simpa using h)]

theorem lv_append_len (xs σ : Env) (i : Nat) : lv (xs ++ σ) (σ.length + i) = xs.reverse[i]? := by
  unfold lv
  rw [List.reverse_append, List.getElem?_append_right (by simp)]
  simp

theorem lv_lt {σ : Env} {l : Nat} {v : Val} (h : lv σ l = some v) : l < σ.length := by
  unfold lv at h
  have := (List.getElem?_eq_some_iff.mp h).1
  simpa using this

theorem getElem?_lv {σ : Env} {l : Nat} (h : l < σ.length) : σ[σ.length - 1 - l]? = lv σ l := by
  unfold lv
  rw [List.getElem?_reverse h]

/-! ### The invariant -/

/-- Entry `en` describes a binding whose value is `x`, in the rewritten environment `σ`. -/
def EntOK (σ : Env) (x : Val) (en : Ent) : Prop :=
  ∃ rv, lv σ en.r = some rv ∧ lv σ en.nl = some x ∧ tlN en.d rv = some x ∧
    ∀ j, j < en.k → ∃ v, lv σ (en.hb + j) = some v ∧ tlN (j + 1) rv = some v

theorem EntOK.mono {σ : Env} {x : Val} {en : Ent} (xs : Env) (h : EntOK σ x en) :
    EntOK (xs ++ σ) x en := by
  obtain ⟨rv, hr, hn, hd, hs⟩ := h
  refine ⟨rv, by rw [lv_append_lt (lv_lt hr)]; exact hr, by rw [lv_append_lt (lv_lt hn)]; exact hn,
    hd, fun j hj => ?_⟩
  obtain ⟨v, hv, ht⟩ := hs j hj
  exact ⟨v, by rw [lv_append_lt (lv_lt hv)]; exact hv, ht⟩

/-- The context describes the original environment `ρ` inside the rewritten one `σ`. -/
def Inv (c : Ctx) (ρ σ : Env) : Prop :=
  ρ.length = c.ent.size ∧ σ.length = c.nd ∧
    ∀ l en, c.ent[l]? = some en → ∃ x, lv ρ l = some x ∧ EntOK σ x en

theorem Inv.congr {c c' : Ctx} {ρ σ : Env} (he : c'.ent = c.ent) (hn : c'.nd = c.nd)
    (h : Inv c ρ σ) : Inv c' ρ σ := by
  obtain ⟨h1, h2, h3⟩ := h
  exact ⟨by rw [he]; exact h1, by rw [hn]; exact h2, by rw [he]; exact h3⟩

/-! ### Arrays and entries -/

theorem size_pushAll : ∀ (a : Array Ent) (es : List Ent), (pushAll a es).size = a.size + es.length
  | a, []      => by simp [pushAll]
  | a, e :: es => by rw [pushAll, size_pushAll]; simp; omega

theorem pushAll_lt : ∀ (a : Array Ent) (es : List Ent) {l : Nat}, l < a.size →
    (pushAll a es)[l]? = a[l]?
  | a, [],      _, _ => by simp [pushAll]
  | a, e :: es, l, h => by
    rw [pushAll, pushAll_lt (a.push e) es (by simp; omega), Array.getElem?_push_lt h]
    exact (Array.getElem?_eq_getElem h).symm

theorem pushAll_ge : ∀ (a : Array Ent) (es : List Ent) (j : Nat),
    (pushAll a es)[a.size + j]? = es[j]?
  | a, [],      j => by simp [pushAll]
  | a, e :: es, 0 => by
    rw [pushAll, pushAll_lt (a.push e) es (by simp)]
    simp
  | a, e :: es, j + 1 => by
    rw [pushAll, show a.size + (j + 1) = (a.push e).size + j by simp; omega, pushAll_ge]
    simp

theorem popN_succ' : ∀ (n : Nat) (a : Array Ent), popN a (n + 1) = (popN a n).pop
  | 0,     a => rfl
  | n + 1, a => by
    show popN a.pop (n + 1) = (popN a.pop n).pop
    exact popN_succ' n a.pop

theorem popN_pushAll : ∀ (es : List Ent) (a : Array Ent), popN (pushAll a es) es.length = a
  | [],      a => rfl
  | e :: es, a => by
    rw [pushAll, List.length_cons, popN_succ', popN_pushAll es (a.push e), Array.pop_push]

theorem ents_length : ∀ (base hb : Nat) (Ks : List Nat), (ents base hb Ks).length = Ks.length
  | _, _, []      => rfl
  | b, h, K :: Ks => by simp [ents, ents_length (b + 1) (h + K) Ks]

theorem ksOf_length (ks : Array Nat) : ∀ (b n : Nat), (ksOf ks b n).length = n
  | _, 0     => rfl
  | b, n + 1 => by simp [ksOf, ksOf_length ks (b + 1) n]

theorem ents_get : ∀ (base hb : Nat) (Ks : List Nat) (j : Nat) (en : Ent),
    (ents base hb Ks)[j]? = some en → en.r = base + j ∧ en.nl = base + j ∧ en.d = 0 ∧ j < Ks.length
  | _, _, [],      _,     _,  h => by simp [ents] at h
  | b, h, K :: Ks, 0,     en, he => by
    simp [ents] at he; subst he; simp
  | b, h, K :: Ks, j + 1, en, he => by
    simp only [ents, List.getElem?_cons_succ] at he
    obtain ⟨h1, h2, h3, h4⟩ := ents_get (b + 1) (h + K) Ks j en he
    refine ⟨by omega, by omega, h3, by simp; omega⟩

/-! ### The stored chains -/

theorem chain_inv {P : Program} : ∀ (K src : Nat) (σ : Env) (x : Val) (b : Term) (w : Val),
    σ[src]? = some x → Eval P σ (chain src K b) w →
    ∃ ys : Env, ys.length = K ∧ Eval P (ys ++ σ) b w ∧
      ∀ i, i < K → ∃ v, lv (ys ++ σ) (σ.length + i) = some v ∧ tlN (i + 1) x = some v
  | 0, _, σ, _, b, w, _, h => ⟨[], rfl, h, fun i hi => absurd hi (Nat.not_lt_zero i)⟩
  | K + 1, src, σ, x, b, w, hx, h => by
    simp only [chain] at h
    cases h with
    | letIn he hb =>
      rename_i y
      cases he with
      | prim hts hp =>
        rename_i vs
        cases hts with
        | cons hv hn =>
          cases hn
          cases hv with
          | var hv =>
            rw [hx] at hv
            cases hv
            have hty := evalPrim_tl hp
            obtain ⟨ys, hlen, hev, hst⟩ :=
              chain_inv K 0 (y :: σ) y b w (by simp) hb
            refine ⟨ys ++ [y], by simp [hlen], by simpa using hev, fun i hi => ?_⟩
            cases i with
            | zero =>
              refine ⟨y, ?_, by simp [tlN, hty]⟩
              rw [List.append_assoc, lv_append_lt (by simp)]
              have := lv_append_len [y] σ 0
              simpa using this
            | succ i =>
              obtain ⟨v, hv1, hv2⟩ := hst i (by omega)
              refine ⟨v, ?_, ?_⟩
              · rw [List.append_assoc]
                have : σ.length + (i + 1) = (y :: σ).length + i := by simp; omega
                rw [this]
                simpa using hv1
              · rw [tlN_succ', hty]
                simpa using hv2

/-- The stored chains of a binder group, inverted: the values of every stored tail, and the
context entries of the group's binders. -/
theorem emit_inv {P : Program} : ∀ (Ks : List Nat) (base hb : Nat) (σ : Env) (b : Term) (w : Val),
    hb = σ.length → (∀ j, j < Ks.length → base + j < hb) →
    Eval P σ (emit (ents base hb Ks) hb b) w →
    ∃ xs : Env, xs.length = sumK Ks ∧ Eval P (xs ++ σ) b w ∧
      ∀ (j : Nat) (en : Ent) (x : Val), (ents base hb Ks)[j]? = some en → lv σ en.r = some x →
        EntOK (xs ++ σ) x en
  | [], base, hb, σ, b, w, _, _, h => by
    refine ⟨[], rfl, h, ?_⟩
    intro j en x he
    simp [ents] at he
  | K :: Ks, base, hb, σ, b, w, hσ, hlt, h => by
    simp only [ents, emit] at h
    have hb0 : base < σ.length := by have := hlt 0 (by simp); omega
    obtain ⟨x0, hx0⟩ : ∃ x0, lv σ base = some x0 := by
      unfold lv
      exact ⟨_, List.getElem?_eq_getElem (by simpa using hb0)⟩
    have hsrc : σ[hb - 1 - base]? = some x0 := by
      rw [hσ, getElem?_lv hb0, hx0]
    obtain ⟨ys, hyl, hev, hst⟩ := chain_inv K _ σ x0 _ w hsrc h
    obtain ⟨xs, hxl, hev2, hent⟩ := emit_inv Ks (base + 1) (hb + K) (ys ++ σ) b w
      (by simp [hyl, hσ]; omega) (fun j hj => by have := hlt (j + 1) (by simp; omega); omega) hev
    refine ⟨xs ++ ys, by simp [hxl, hyl, sumK]; omega, by simpa using hev2, ?_⟩
    intro j en x he hx
    cases j with
    | zero =>
      simp [ents] at he
      subst he
      simp only at hx
      rw [hx0] at hx
      cases hx
      refine ⟨x0, ?_, ?_, by rfl, fun i hi => ?_⟩
      · rw [lv_append_lt hb0]; exact hx0
      · rw [lv_append_lt hb0]; exact hx0
      · obtain ⟨v, hv1, hv2⟩ := hst i hi
        refine ⟨v, ?_, hv2⟩
        simp only at hi ⊢
        rw [List.append_assoc, lv_append_lt (by simp [hyl]; omega), hσ]
        exact hv1
    | succ j =>
      simp only [ents, List.getElem?_cons_succ] at he
      obtain ⟨hr, -, -, hjl⟩ := ents_get (base + 1) (hb + K) Ks j en he
      have hr' : en.r < σ.length := by have := hlt (j + 1) (by simp; omega); omega
      have := hent j en x he (by rw [lv_append_lt hr']; exact hx)
      simpa using this

/-! ### The rewrite leaves the context as it found it -/

mutual

theorem go_ctx (ks : Array Nat) : ∀ (c : Ctx) (t : Term),
    (go ks c t).2.ent = c.ent ∧ (go ks c t).2.nd = c.nd
  | c, .lit _ => by simp [go]
  | c, .var _ => by simp [go]
  | c, .letIn e b => by
    simp only [go]
    split
    · split
      · refine ⟨?_, rfl⟩
        rw [(go_ctx ks _ b).1]
        simp
      · refine ⟨?_, rfl⟩
        rw [(go_ctx ks _ b).1]
        simp
    · obtain ⟨h1, h2⟩ := go_ctx ks c e
      refine ⟨?_, by simp [h2]⟩
      rw [(go_ctx ks _ b).1]
      have := popN_pushAll (ents (go ks c e).2.nd ((go ks c e).2.nd + 1) [ks[(go ks c e).2.bid]?.getD 0])
        (go ks c e).2.ent
      rw [ents_length] at this
      simp only [List.length_cons, List.length_nil, Nat.zero_add] at this
      rw [this, h1]
  | c, .ite a b d => by
    simp only [go]
    obtain ⟨h1, h2⟩ := go_ctx ks c a
    obtain ⟨h3, h4⟩ := go_ctx ks (go ks c a).2 b
    obtain ⟨h5, h6⟩ := go_ctx ks (go ks (go ks c a).2 b).2 d
    exact ⟨by rw [h5, h3, h1], by rw [h6, h4, h2]⟩
  | c, .prim _ ts => by simp only [go]; exact goL_ctx ks c ts
  | c, .ctorT _ ts => by simp only [go]; exact goL_ctx ks c ts
  | c, .caseT s alts => by
    simp only [go]
    obtain ⟨h1, h2⟩ := go_ctx ks c s
    obtain ⟨h3, h4⟩ := goA_ctx ks (go ks c s).2 alts
    exact ⟨by rw [h3, h1], by rw [h4, h2]⟩
  | c, .call _ ts => by simp only [go]; exact goL_ctx ks c ts

theorem goL_ctx (ks : Array Nat) : ∀ (c : Ctx) (ts : List Term),
    (goL ks c ts).2.ent = c.ent ∧ (goL ks c ts).2.nd = c.nd
  | c, []      => by simp [goL]
  | c, t :: ts => by
    simp only [goL]
    obtain ⟨h1, h2⟩ := go_ctx ks c t
    obtain ⟨h3, h4⟩ := goL_ctx ks (go ks c t).2 ts
    exact ⟨by rw [h3, h1], by rw [h4, h2]⟩

theorem goA_ctx (ks : Array Nat) : ∀ (c : Ctx) (alts : List Alt),
    (goA ks c alts).2.ent = c.ent ∧ (goA ks c alts).2.nd = c.nd
  | c, []                   => by simp [goA]
  | c, (tag, n, body) :: as => by
    simp only [goA]
    refine ⟨?_, by rw [(goA_ctx ks _ as).2]⟩
    rw [(goA_ctx ks _ as).1]
    simp only
    rw [(go_ctx ks _ body).1]
    have := popN_pushAll (ents c.nd (c.nd + n) (ksOf ks c.bid n)) c.ent
    rw [ents_length, ksOf_length] at this
    exact this

end

/-! ### The simulation -/

theorem isTlVar_some {e : Term} {j : Nat} (h : isTlVar e = some j) : e = .prim .tl [.var j] := by
  unfold isTlVar at h
  split at h
  · cases h; rfl
  · cases h

/-- The context after pushing one more binding of the original term. -/
theorem Inv.push {c : Ctx} {ρ σ : Env} {y : Val} {en : Ent} (h : Inv c ρ σ) (σ' : Env)
    (hσ' : ∃ xs, σ' = xs ++ σ) (nd' bid' : Nat) (hnd : σ'.length = nd') (hen : EntOK σ' y en) :
    Inv { ent := c.ent.push en, nd := nd', bid := bid' } (y :: ρ) σ' := by
  obtain ⟨h1, h2, h3⟩ := h
  obtain ⟨xs, rfl⟩ := hσ'
  refine ⟨by simp [h1], hnd, fun l en0 he => ?_⟩
  simp only at he
  rw [Array.getElem?_push] at he
  split at he
  · rename_i hl
    cases he
    refine ⟨y, ?_, hen⟩
    have := lv_append_len [y] ρ 0
    simp only [Nat.add_zero] at this
    rw [show (y :: ρ) = [y] ++ ρ by rfl, hl, ← h1, this]
    rfl
  · obtain ⟨x, hx, hok⟩ := h3 l en0 he
    refine ⟨x, ?_, hok.mono xs⟩
    rw [show (y :: ρ) = [y] ++ ρ by rfl, lv_append_lt (lv_lt hx)]
    exact hx

/-- The context after pushing a binder group (`vs`, innermost first, of `n = vs.length`
bindings) whose stored chains `emit_inv` inverted. -/
theorem Inv.group {c : Ctx} {ρ σ : Env} {vs xs : Env} {Ks : List Nat} (h : Inv c ρ σ)
    (hn : Ks.length = vs.length) (hxl : xs.length = sumK Ks) (bid' : Nat)
    (hent : ∀ (j : Nat) (en : Ent) (x : Val), (ents c.nd (c.nd + vs.length) Ks)[j]? = some en →
      lv (vs ++ σ) en.r = some x → EntOK (xs ++ (vs ++ σ)) x en) :
    Inv { ent := pushAll c.ent (ents c.nd (c.nd + vs.length) Ks),
          nd := c.nd + vs.length + sumK Ks, bid := bid' } (vs ++ ρ) (xs ++ (vs ++ σ)) := by
  obtain ⟨h1, h2, h3⟩ := h
  refine ⟨by simp [size_pushAll, ents_length, h1, hn]; omega,
    by simp [hxl, h2]; omega, fun l en he => ?_⟩
  simp only at he
  by_cases hl : l < c.ent.size
  · rw [pushAll_lt _ _ hl] at he
    obtain ⟨x, hx, hok⟩ := h3 l en he
    refine ⟨x, ?_, ?_⟩
    · rw [lv_append_lt (lv_lt hx)]; exact hx
    · have := hok.mono (xs ++ vs)
      simpa using this
  · obtain ⟨j, rfl⟩ : ∃ j, l = c.ent.size + j := ⟨l - c.ent.size, by omega⟩
    rw [pushAll_ge] at he
    obtain ⟨hr, -, -, hj⟩ := ents_get _ _ _ j en he
    have hjv : j < vs.length := by omega
    obtain ⟨x, hx⟩ : ∃ x, vs.reverse[j]? = some x :=
      ⟨_, List.getElem?_eq_getElem (by simpa using hjv)⟩
    refine ⟨x, ?_, hent j en x he ?_⟩
    · rw [← h1, lv_append_len]; exact hx
    · rw [hr, ← h2, lv_append_len]; exact hx

mutual

/-- **The simulation.** Whatever the rewritten term evaluates to, the original evaluates to. -/
theorem go_sound {P P2 : Program} (ks : Array Nat) : ∀ (t : Term) (c : Ctx) (ρ σ : Env) (w : Val),
    Inv c ρ σ → Hw.fragB ρ.length t = true → Eval P2 σ (go ks c t).1 w → Eval P ρ t w
  | .lit _, c, ρ, σ, w, _, _, h => by
    simp only [go] at h
    cases h
    exact .lit
  | .var i, c, ρ, σ, w, hI, hf, h => by
    obtain ⟨h1, h2, h3⟩ := hI
    simp only [Hw.fragB, decide_eq_true_eq] at hf
    simp only [go] at h
    have hl : c.ent.size - 1 - i < c.ent.size := by omega
    obtain ⟨en, hen⟩ : ∃ en, c.ent[c.ent.size - 1 - i]? = some en :=
      ⟨_, Array.getElem?_eq_getElem hl⟩
    obtain ⟨x, hx, -, -, hn, -, -⟩ := h3 _ en hen
    rw [hen, Option.getD_some] at h
    cases h with
    | var hv =>
      have hnl := lv_lt hn
      rw [← h2, getElem?_lv hnl, hn] at hv
      cases hv
      refine .var ?_
      rw [show i = ρ.length - 1 - (ρ.length - 1 - i) by omega, getElem?_lv (by omega), h1]
      exact hx
  | .letIn e b, c, ρ, σ, w, hI, hf, h => by
    simp only [Hw.fragB, Bool.and_eq_true] at hf
    cases hj : isTlVar e with
    | some j =>
      have he := isTlVar_some hj
      subst he
      simp only [Hw.fragB, Hw.fragListB, Prim.arity, decide_eq_true_eq, Bool.and_true,
        List.length_cons, List.length_nil, Bool.and_eq_true] at hf
      obtain ⟨⟨-, hjl⟩, hfb⟩ := hf
      have hI' := hI
      obtain ⟨h1, h2, h3⟩ := hI
      have hl : c.ent.size - 1 - j < c.ent.size := by omega
      obtain ⟨en, hen⟩ : ∃ en, c.ent[c.ent.size - 1 - j]? = some en :=
        ⟨_, Array.getElem?_eq_getElem hl⟩
      obtain ⟨x, hx, rv, hr, hn, hd, hs⟩ := h3 _ en hen
      have hρj : ρ[j]? = some x := by
        rw [show j = ρ.length - 1 - (ρ.length - 1 - j) by omega, getElem?_lv (by omega), h1]
        exact hx
      simp only [go, isTlVar, hen, Option.getD_some] at h
      split at h
      · -- the tail is stored
        rename_i hk
        obtain ⟨y, hy, hty⟩ := hs en.d (by omega)
        have hxy : tlV x = some y := by rw [tlN, hd] at hty; simpa using hty
        have hb := go_sound (P := P) ks b _ (y :: ρ) σ w
          (hI'.push (en := ⟨en.hb + en.d, en.r, en.d + 1, en.k, en.hb⟩) σ ⟨[], rfl⟩ c.nd (c.bid + 1) h2
            ⟨rv, hr, hy, hty, hs⟩)
          (by simpa using hfb) h
        exact .letIn (.prim (.cons (.var hρj) .nil) (evalPrim_tl_of hxy)) hb
      · -- a deeper read: the binding stays
        cases h with
        | letIn he hb =>
          rename_i y
          cases he with
          | prim hts hp =>
            cases hts with
            | cons hv hnil =>
              cases hnil
              cases hv with
              | var hv =>
                have hnl := lv_lt hn
                rw [← h2, getElem?_lv hnl, hn] at hv
                cases hv
                have hxy := evalPrim_tl hp
                have hyσ : lv (y :: σ) c.nd = some y := by
                  have := lv_append_len [y] σ 0
                  rw [h2] at this
                  simpa using this
                have hok : EntOK (y :: σ) y ⟨c.nd, en.r, en.d + 1, en.k, en.hb⟩ := by
                  refine ⟨rv, ?_, hyσ, ?_, ?_⟩
                  · show lv (y :: σ) en.r = some rv
                    rw [show (y :: σ) = [y] ++ σ by rfl, lv_append_lt (lv_lt hr)]; exact hr
                  · show tlN (en.d + 1) rv = some y
                    rw [tlN, hd]; simpa using hxy
                  · intro j' hj'
                    obtain ⟨v, hv1, hv2⟩ := hs j' hj'
                    exact ⟨v, by rw [show (y :: σ) = [y] ++ σ by rfl, lv_append_lt (lv_lt hv1)]; exact hv1,
                      hv2⟩
                have hb' := go_sound (P := P) ks b _ (y :: ρ) (y :: σ) w
                  (hI'.push (en := ⟨c.nd, en.r, en.d + 1, en.k, en.hb⟩) (y :: σ) ⟨[y], rfl⟩ (c.nd + 1)
                    (c.bid + 1) (by simp [h2]) hok)
                  (by simpa using hfb) hb
                exact .letIn (.prim (.cons (.var hρj) .nil) hp) hb'
    | none =>
      simp only [go, hj] at h
      obtain ⟨hce, hcn⟩ := go_ctx ks c e
      cases h with
      | letIn he hb =>
        rename_i v1
        have hE := go_sound (P := P) ks e c ρ σ v1 hI hf.1 he
        obtain ⟨h1, h2, h3⟩ := hI
        rw [hcn] at hb
        obtain ⟨xs, hxl, hev, hent⟩ := emit_inv _ c.nd (c.nd + 1) (v1 :: σ) _ w
          (by simp [h2]) (fun j hj => by simp at hj; omega) hb
        have hI2 := Inv.group (c := c) (vs := [v1]) (xs := xs) ⟨h1, h2, h3⟩ rfl hxl
          ((go ks c e).2.bid + 1) hent
        refine .letIn hE (go_sound ks b _ (v1 :: ρ) (xs ++ (v1 :: σ)) w
          (Inv.congr ?_ ?_ hI2) (by simpa using hf.2) hev)
        · simp [hce]
        · simp
  | .ite a b d, c, ρ, σ, w, hI, hf, h => by
    simp only [Hw.fragB, Bool.and_eq_true] at hf
    simp only [go] at h
    obtain ⟨hae, han⟩ := go_ctx ks c a
    obtain ⟨hbe, hbn⟩ := go_ctx ks (go ks c a).2 b
    have hI1 : Inv (go ks c a).2 ρ σ := hI.congr hae han
    have hI2 : Inv (go ks (go ks c a).2 b).2 ρ σ := hI1.congr hbe hbn
    cases h with
    | iteT hc ha => exact .iteT (go_sound ks a c ρ σ _ hI hf.1.1 hc) (go_sound ks b _ ρ σ w hI1 hf.1.2 ha)
    | iteF hc hb => exact .iteF (go_sound ks a c ρ σ _ hI hf.1.1 hc) (go_sound ks d _ ρ σ w hI2 hf.2 hb)
  | .prim p ts, c, ρ, σ, w, hI, hf, h => by
    simp only [Hw.fragB, Bool.and_eq_true] at hf
    simp only [go] at h
    cases h with
    | prim hts hp => exact .prim (goL_sound ks ts c ρ σ _ hI hf.2 hts) hp
  | .ctorT k ts, c, ρ, σ, w, hI, hf, h => by
    simp only [Hw.fragB] at hf
    simp only [go] at h
    cases h with
    | ctorT hts => exact .ctorT (goL_sound ks ts c ρ σ _ hI hf hts)
  | .caseT s alts, c, ρ, σ, w, hI, hf, h => by
    simp only [Hw.fragB, Bool.and_eq_true] at hf
    simp only [go] at h
    obtain ⟨hse, hsn⟩ := go_ctx ks c s
    cases h with
    | caseT hs hfa har hbody =>
      have hS := go_sound (P := P) ks s c ρ σ _ hI hf.1 hs
      obtain ⟨a, ha, har', hA⟩ := goA_sound (P := P) ks alts _ ρ σ _ _ _ _ (hI.congr hse hsn) hf.2 hfa har hbody
      exact .caseT hS ha har' hA
  | .call _ _, _, _, _, _, _, hf, _ => by simp [Hw.fragB] at hf

theorem goL_sound {P P2 : Program} (ks : Array Nat) : ∀ (ts : List Term) (c : Ctx) (ρ σ : Env)
    (ws : List Val), Inv c ρ σ → Hw.fragListB ρ.length ts = true →
    EvalList P2 σ (goL ks c ts).1 ws → EvalList P ρ ts ws
  | [], c, ρ, σ, ws, _, _, h => by
    simp only [goL] at h
    cases h
    exact .nil
  | t :: ts, c, ρ, σ, ws, hI, hf, h => by
    simp only [Hw.fragListB, Bool.and_eq_true] at hf
    simp only [goL] at h
    obtain ⟨he, hn⟩ := go_ctx ks c t
    cases h with
    | cons ht hts =>
      exact .cons (go_sound ks t c ρ σ _ hI hf.1 ht)
        (goL_sound ks ts _ ρ σ _ (hI.congr he hn) hf.2 hts)

theorem goA_sound {P P2 : Program} (ks : Array Nat) : ∀ (alts : List Alt) (c : Ctx) (ρ σ : Env)
    (tag : Nat) (a' : Alt) (vs : List Val) (w : Val), Inv c ρ σ →
    Hw.fragAltsB ρ.length alts = true → findAlt (goA ks c alts).1 tag = some a' →
    a'.arity = vs.length → Eval P2 (vs ++ σ) a'.body w →
    ∃ a, findAlt alts tag = some a ∧ a.arity = vs.length ∧ Eval P (vs ++ ρ) a.body w
  | [], c, ρ, σ, tag, a', vs, w, _, _, hfa, _, _ => by
    simp [goA, findAlt] at hfa
  | (t0, n, body) :: as, c, ρ, σ, tag, a', vs, w, hI, hf, hfa, har, hbody => by
    simp only [Hw.fragAltsB, Bool.and_eq_true] at hf
    simp only [goA, findAlt, Alt.tag] at hfa
    by_cases ht : t0 = tag
    · simp only [ht, reduceIte] at hfa
      cases hfa
      simp only [Alt.arity] at har
      simp only [Alt.body] at hbody
      subst har
      obtain ⟨h1, h2, h3⟩ := hI
      obtain ⟨xs, hxl, hev, hent⟩ := emit_inv (ksOf ks c.bid vs.length) c.nd (c.nd + vs.length)
        (vs ++ σ) _ w (by simp [h2]; omega)
        (fun j hj => by rw [ksOf_length] at hj; omega) hbody
      have hI2 := Inv.group (c := c) (vs := vs) ⟨h1, h2, h3⟩ (ksOf_length ks c.bid vs.length) hxl
        (c.bid + vs.length) hent
      have hB := go_sound (P := P) ks body _ (vs ++ ρ) (xs ++ (vs ++ σ)) w hI2
        (by simpa [Nat.add_comm] using hf.1) hev
      refine ⟨(t0, vs.length, body), ?_, rfl, hB⟩
      simp [findAlt, Alt.tag, ht]
    · simp only [ht, reduceIte] at hfa
      have key : ∃ a, findAlt as tag = some a ∧ a.arity = vs.length ∧ Eval P (vs ++ ρ) a.body w := by
        refine goA_sound ks as _ ρ σ tag a' vs w (Inv.congr ?_ ?_ hI) hf.2 hfa har hbody
        · simp only
          rw [(go_ctx ks _ body).1]
          have := popN_pushAll (ents c.nd (c.nd + n) (ksOf ks c.bid n)) c.ent
          rw [ents_length, ksOf_length] at this
          exact this
        · rfl
      obtain ⟨a, ha, har', hA⟩ := key
      refine ⟨a, ?_, har', hA⟩
      simp only [findAlt, Alt.tag, ht, reduceIte]
      exact ha

end

/-- A function body with `n` parameters. -/
theorem goTop_sound {P P2 : Program} (ks : Array Nat) (body : Term) (vs : Env) (w : Val)
    (hf : Hw.fragB vs.length body = true) (h : Eval P2 vs (goTop ks vs.length body) w) :
    Eval P vs body w := by
  unfold goTop at h
  obtain ⟨xs, hxl, hev, hent⟩ := emit_inv (ksOf ks 0 vs.length) 0 vs.length vs _ w rfl
    (fun j hj => by rw [ksOf_length] at hj; omega) h
  have hI0 : Inv { ent := #[], nd := 0, bid := 0 } [] [] := ⟨rfl, rfl, fun l en he => by simp at he⟩
  have hI := Inv.group (c := { ent := #[], nd := 0, bid := 0 }) (vs := vs) (xs := xs) hI0
    (ksOf_length ks 0 vs.length) hxl vs.length (by simpa using hent)
  simp only [List.append_nil, Nat.zero_add] at hI
  exact go_sound ks body _ vs (xs ++ vs) w hI hf hev

/-! ### The program and the step -/

/-- **The pass is sound**: on a checked residual, whatever the rewritten program's entry call
evaluates to, the original's evaluates to. -/
theorem shareProgram_sound {R : Program} {b : Nat} (hc : checkResidual R = some b)
    {a₀ a₁ a₂ v : Val}
    (h : Eval (shareProgram R) [] (.call (shareProgram R).entry [.lit a₀, .lit a₁, .lit a₂]) v) :
    Eval R [] (.call R.entry [.lit a₀, .lit a₁, .lit a₂]) v := by
  obtain ⟨fd, hfd, ha, hfr, -⟩ := checkResidual_entry hc
  have hfd2 : (shareProgram R).fn (shareProgram R).entry = some (shareFun fd) := by
    simp only [shareProgram, Program.fn, List.getElem?_map]
    simp only [Program.fn] at hfd
    rw [hfd]; rfl
  cases h with
  | call hts hfn har hb =>
    rename_i vs fd2
    have hvs : vs = [a₀, a₁, a₂] := EvalList_lits_inj [a₀, a₁, a₂] vs hts
    subst hvs
    rw [hfd2] at hfn
    cases hfn
    have hlen : ([a₀, a₁, a₂] : Env).length = fd.arity := by rw [ha]; rfl
    simp only [shareFun] at hb
    rw [← hlen] at hb
    have hB := goTop_sound (P := R) (depthsOf fd) fd.body [a₀, a₁, a₂] v (by rw [hlen, ha]; exact hfr) hb
    exact .call (.cons .lit (.cons .lit (.cons .lit .nil))) hfd (by rw [ha]; rfl) hB

/-- **The step is the original step**, on every input, for every bundle that carries its own
check. -/
theorem stepEnc_eq {sim : MemSimulator} (hchk : checkResidual sim.prog = some sim.bound)
    (e : ClockEdges) (i : RuntimeInput) (sv : Val) :
    stepEnc sim (shareSim sim) e i sv = MemRunFast.runMemSimEnc sim e i sv := by
  unfold stepEnc shareSim
  dsimp only
  cases hc : checkResidual (shareProgram sim.prog) with
  | none => rfl
  | some b2 =>
    simp only [Option.map_some]
    cases hv : evalFuel b2 (shareProgram sim.prog) []
        (.call (shareProgram sim.prog).entry [.lit (encEdges e), .lit (encInput i), .lit sv]) with
    | value v =>
      simp only
      have h2 := evalFuel_sound _ _ _ _ _ hv
      have h1 := shareProgram_sound hchk h2
      unfold MemRunFast.runMemSimEnc
      rw [checkResidual_complete hchk h1]
    | outOfFuel => rfl
    | typeError _ => rfl

theorem runMemSimS_eq {sim : MemSimulator} (hchk : checkResidual sim.prog = some sim.bound)
    (e : ClockEdges) (i : RuntimeInput) (st : StateM) :
    runMemSimS sim (shareSim sim) e i st = runMemSim sim e i st := by
  unfold runMemSimS
  rw [stepEnc_eq hchk, MemRunFast.runMemSim_eq_enc]

/-- For example, `runMemSim_correct` through the shared step. Every other route's theorem
(`runMemSimF_correct`, `runMemSimWF_correct`, `runMemSimV_correct`, `runMemSimW_correct`, …)
carries over the same way: each bundle's `chk` field is `hchk`. -/
theorem runMemSimS_correct {sf wf : Nat} {sim : MemSimulator} (hw : MemSimWFAt sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSimS sim (shareSim sim) e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  rw [runMemSimS_eq hw.chk]
  exact runMemSim_correct hw hR

#audit_axioms Projection.ShareTails.shareProgram_sound
#audit_axioms Projection.ShareTails.stepEnc_eq
#audit_axioms Projection.ShareTails.runMemSimS_correct

end ShareTails
end Projection
