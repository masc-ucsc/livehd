/-
`Sym.Layout` -- the residual's arguments as symbolic values (M3 S3).

A memory-route residual takes `[encEdges e, encInput i, encStateM st]`. Its
symbolic counterpart keeps every SPINE concrete (the number of clocks,
inputs, flops, memories and words is the design's) and makes each hardware
value a VARIABLE: `bvVar w k` is primary variable `k` read as a `w`-bit
vector. `pbvOf b` is the value an assignment holds for the concrete `b`.

Each `*_rel` lemma: if the assignment holds the concrete values at the
layout's indices and the concrete data has the layout's shape, the symbolic
argument denotes the concrete encoding.
-/
import LeanSemanticPrimitives.Projection.Sym.Asg
import LeanSemanticPrimitives.Projection.MemImage

namespace Projection.Sym
open Std.Tactic.BVDecide Compiler

/-! ## Lists with a concrete spine -/

def sListOf : List SVal → SVal
  | []      => .conc .nil
  | s :: ss => .cons s (sListOf ss)

/-- `ss` and `xs` correspond element by element through `f`. -/
inductive SRelLF (σ : Asg) {α : Type} (f : α → Val) : List SVal → List α → Prop where
  | nil  : SRelLF σ f [] []
  | cons : SRel σ s (f x) → SRelLF σ f ss xs → SRelLF σ f (s :: ss) (x :: xs)

theorem sListOf_rel {σ : Asg} {α : Type} (f : α → Val) :
    ∀ {ss : List SVal} {xs : List α}, SRelLF σ f ss xs → SRel σ (sListOf ss) (encListG f xs)
  | _, _, .nil => .conc
  | _, _, .cons h1 h2 => .cons h1 (sListOf_rel f h2)

/-! ## Variables -/

/-- Primary variable `k`, read as a `w`-bit vector. -/
def bvVar (w k : Nat) : SVal := .bv w (.var k)

/-- Primary variable `k`, read as a Boolean. -/
def boolVar (k : Nat) : SVal := .bool (.var k)

/-- The value an assignment holds for a concrete bit vector: its normalized
value, at its own width. -/
def pbvOf (b : BV) : PBV := ⟨BitVec.ofNat b.width (bv_uint b).toNat⟩

def pbvOfBool (b : Bool) : PBV := ⟨BitVec.ofBool b⟩

theorem eval_var_of {σ : Asg} {w idx : Nat} {x : BitVec w} (h : σ.get idx = ⟨x⟩) :
    (BVExpr.var idx : BVExpr w).eval σ = x := by
  rw [BVExpr.eval_var, h]
  simp

theorem bvVar_rel {σ : Asg} {w k : Nat} {b : BV} (hw : b.width = w) (hk : σ.get k = pbvOf b) :
    SRel σ (bvVar w k) (ofBV b) := by
  subst hw
  refine .bv (asBV_ofBV b) rfl ?_
  rw [eval_var_of hk, BitVec.toNat_ofNat]
  have h0 := bv_uint_nonneg' b
  have h1 := bv_uint_lt' b
  rw [two_pow_cast] at h1
  rw [Nat.mod_eq_of_lt (by omega)]
  omega

theorem boolVar_rel {σ : Asg} {k : Nat} {b : Bool} (hk : σ.get k = pbvOfBool b) :
    SRel σ (boolVar k) (.bool b) := by
  refine .bool ?_
  rw [eval_var_of hk]
  cases b <;> rfl

/-! ## Agreement: the assignment holds these values from `start` on -/

def AgreeAt (σ : Asg) (start : Nat) (vs : List PBV) : Prop :=
  ∀ j x, vs[j]? = some x → σ.get (start + j) = x

theorem AgreeAt_append {σ : Asg} {start : Nat} {a b : List PBV} :
    AgreeAt σ start (a ++ b) ↔ AgreeAt σ start a ∧ AgreeAt σ (start + a.length) b := by
  constructor
  · intro h
    refine ⟨fun j x hj => h j x (by rw [List.getElem?_append_left (by
        rcases Nat.lt_or_ge j a.length with hl | hl
        · exact hl
        · rw [List.getElem?_eq_none hl] at hj; cases hj)]; exact hj), fun j x hj => ?_⟩
    have := h (a.length + j) x (by rw [List.getElem?_append_right (by omega)]; simpa using hj)
    rw [← this]; congr 1; omega
  · rintro ⟨ha, hb⟩ j x hj
    rcases Nat.lt_or_ge j a.length with hl | hl
    · rw [List.getElem?_append_left hl] at hj; exact ha j x hj
    · rw [List.getElem?_append_right hl] at hj
      have := hb (j - a.length) x hj
      rw [← this]; congr 1; omega

theorem AgreeAt_cons {σ : Asg} {start : Nat} {v : PBV} {vs : List PBV} :
    AgreeAt σ start (v :: vs) ↔ σ.get start = v ∧ AgreeAt σ (start + 1) vs := by
  have := @AgreeAt_append σ start [v] vs
  simp only [List.singleton_append] at this
  rw [this]
  constructor
  · rintro ⟨h1, h2⟩; exact ⟨by simpa using h1 0 v rfl, by simpa using h2⟩
  · rintro ⟨h1, h2⟩
    refine ⟨fun j x hj => ?_, by simpa using h2⟩
    cases j with
    | zero => simp at hj; subst hj; simpa using h1
    | succ j => simp at hj

/-! ## Bit-vector lists -/

/-- `w₀ w₁ …` as variables `start, start+1, …`. -/
def bvVars : List Nat → Nat → List SVal
  | [], _ => []
  | w :: ws, k => bvVar w k :: bvVars ws (k + 1)

theorem bvVars_length : ∀ (ws : List Nat) (k : Nat), (bvVars ws k).length = ws.length
  | [], _ => rfl
  | _ :: ws, k => by simp [bvVars, bvVars_length ws (k + 1)]

theorem bvVars_rel {σ : Asg} :
    ∀ (xs : List BV) (k : Nat), AgreeAt σ k (xs.map pbvOf) →
      SRelLF σ ofBV (bvVars (xs.map BV.width) k) xs
  | [], _, _ => .nil
  | x :: xs, k, h => by
      rw [List.map_cons, AgreeAt_cons] at h
      exact .cons (bvVar_rel rfl h.1) (bvVars_rel xs (k + 1) h.2)

theorem bvVars_rel' {σ : Asg} {xs : List BV} {ws : List Nat} {k : Nat}
    (hw : xs.map BV.width = ws) (h : AgreeAt σ k (xs.map pbvOf)) :
    SRelLF σ ofBV (bvVars ws k) xs := by
  subst hw; exact bvVars_rel xs k h

/-! ## Memory images -/

/-- One image of `2^aw` words of width `dw` and a default, as variables. -/
def imgVar (aw dw k : Nat) : SVal :=
  .ctor MemImg.tagMemImg [sListOf (bvVars (List.replicate (2 ^ aw) dw) k), bvVar dw (k + 2 ^ aw)]

def imgVals (m : MemImg) : List PBV := m.words.map pbvOf ++ [pbvOf m.dflt]

theorem imgVals_length (m : MemImg) : (imgVals m).length = m.words.length + 1 := by
  simp [imgVals]

theorem imgVar_rel {σ : Asg} {aw dw k : Nat} {m : MemImg}
    (hlen : m.words.length = 2 ^ aw) (hws : ∀ x ∈ m.words, x.width = dw) (hd : m.dflt.width = dw)
    (h : AgreeAt σ k (imgVals m)) : SRel σ (imgVar aw dw k) (MemImg.encMemImg m) := by
  unfold imgVals at h
  rw [AgreeAt_append, AgreeAt_cons] at h
  obtain ⟨hw, hdv, -⟩ := h
  have hmap : m.words.map BV.width = List.replicate (2 ^ aw) dw := by
    rw [← hlen]
    apply List.ext_getElem (by simp)
    intro i h1 h2
    simp only [List.getElem_map, List.getElem_replicate]
    exact hws _ (List.getElem_mem _)
  refine .ctor (.cons (sListOf_rel ofBV (bvVars_rel' hmap hw)) (.cons (bvVar_rel hd ?_) .nil))
  rw [← hdv]; simp [hlen]

/-! ## The state layout -/

/-- A state's shape: each flop's width, each memory's `(aw, dw)`. -/
structure Shape where
  fws : List Nat
  mds : List (Nat × Nat)
  deriving BEq, DecidableEq, Repr

/-- A concrete state of exactly this shape. -/
def StMatches (L : Shape) (st : StateM) : Prop :=
  st.flops.toList.map BV.width = L.fws ∧
  st.mems.size = L.mds.length ∧
  ∀ (j : Nat) (m : MemImg) (aw dw : Nat), st.mems[j]? = some m → L.mds[j]? = some (aw, dw) →
    m.words.length = 2 ^ aw ∧ (∀ x ∈ m.words, x.width = dw) ∧ m.dflt.width = dw

def imgsVar : List (Nat × Nat) → Nat → List SVal
  | [], _ => []
  | (aw, dw) :: mds, k => imgVar aw dw k :: imgsVar mds (k + 2 ^ aw + 1)

def stateVar (L : Shape) (k : Nat) : SVal :=
  .ctor StateM.tagStateM [sListOf (bvVars L.fws k), sListOf (imgsVar L.mds (k + L.fws.length))]

def stateVals (st : StateM) : List PBV :=
  st.flops.toList.map pbvOf ++ st.mems.toList.flatMap imgVals

theorem imgsVar_rel {σ : Asg} :
    ∀ (mds : List (Nat × Nat)) (ms : List MemImg) (k : Nat),
      ms.length = mds.length →
      (∀ (j : Nat) (m : MemImg) (aw dw : Nat), ms[j]? = some m → mds[j]? = some (aw, dw) →
        m.words.length = 2 ^ aw ∧ (∀ x ∈ m.words, x.width = dw) ∧ m.dflt.width = dw) →
      AgreeAt σ k (ms.flatMap imgVals) →
      SRelLF σ MemImg.encMemImg (imgsVar mds k) ms
  | [], [], _, _, _, _ => .nil
  | (aw, dw) :: mds, m :: ms, k, hl, hs, ha => by
      rw [List.flatMap_cons, AgreeAt_append] at ha
      obtain ⟨h1, h2, h3⟩ := hs 0 m aw dw rfl rfl
      refine .cons (imgVar_rel h1 h2 h3 ha.1) ?_
      have e : k + (imgVals m).length = k + 2 ^ aw + 1 := by rw [imgVals_length, h1]; omega
      rw [e] at ha
      exact imgsVar_rel mds ms _ (by simpa using hl)
        (fun j m' aw' dw' h1' h2' => hs (j + 1) m' aw' dw' (by simpa using h1') (by simpa using h2')) ha.2
  | [], _ :: _, _, hl, _, _ => by simp at hl
  | _ :: _, [], _, hl, _, _ => by simp at hl

/-- **The state as variables.** A state of the layout's shape, held by the
assignment from `k` on, is denoted by `stateVar L k`. -/
theorem stateVar_rel {σ : Asg} {L : Shape} {st : StateM} {k : Nat} (hm : StMatches L st)
    (ha : AgreeAt σ k (stateVals st)) : SRel σ (stateVar L k) (StateM.encStateM st) := by
  obtain ⟨hf, hl, hs⟩ := hm
  unfold stateVals at ha
  rw [AgreeAt_append] at ha
  unfold StateM.encStateM stateVar encBVs encArr
  refine .ctor (.cons (sListOf_rel ofBV (bvVars_rel' hf ha.1)) (.cons ?_ .nil))
  have e : k + (st.flops.toList.map pbvOf).length = k + L.fws.length := by rw [← hf]; simp
  rw [e] at ha
  exact sListOf_rel _ (imgsVar_rel L.mds st.mems.toList _ (by simpa using hl)
    (fun j m aw dw h1 h2 => hs j m aw dw (by simpa using h1) h2) ha.2)

/-! ## Inputs and edges -/

def inputVar (ws : List Nat) (k : Nat) : SVal := sListOf (bvVars ws k)

def inputVals (i : RuntimeInput) : List PBV := i.toList.map pbvOf

theorem inputVar_rel {σ : Asg} {ws : List Nat} {i : RuntimeInput} {k : Nat}
    (hw : i.toList.map BV.width = ws) (ha : AgreeAt σ k (inputVals i)) :
    SRel σ (inputVar ws k) (encInput i) :=
  sListOf_rel ofBV (bvVars_rel' hw ha)

/-- Every clock fires: the edge vector is concrete. -/
def allEdges (n : Nat) : ClockEdges := Array.replicate n true

theorem allEdges_rel {σ : Asg} (n : Nat) :
    SRel σ (.conc (encEdges (allEdges n))) (encEdges (allEdges n)) := .conc

end Projection.Sym
