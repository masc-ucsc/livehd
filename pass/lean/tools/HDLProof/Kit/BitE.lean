/-
`Kit.BitE` -- formulas and words over a design's real ports at one cycle.

A refinement spec names events, observations, responses and fairness through these, so a
theorem about the streams they define mentions only the design's ports:
- `PortW`: an output at the cycle (or the next one, `next`), or an input at the cycle.
- `BitE`: a Boolean over ports: nonzero, equal to a constant, and the connectives.
- `ObsW`: an observed word, a slice of a port that reads 0 unless a guard bit is set. This lets
  masked bus data be stated as byte lanes guarded by their strobe bit.

Each has a concrete value over `Ports` (the arrays of one cycle) and a symbolic one over
`SPorts`, with a soundness lemma under `PortsRel`. Mathlib-free.
-/
import HDLProof.Kit.Norm

open Projection
namespace HDLProof.Kit
open HDLProof.Sym Std.Tactic.BVDecide Compiler

/-- A port word: output `o` at the cycle (`next`: at the next cycle), or input `k`. -/
inductive PortW where
  | out (o : Nat) (next : Bool := false)
  | inp (k : Nat)
  deriving Repr, Inhabited, DecidableEq

/-- The ports of one cycle: its outputs, the next cycle's outputs, its inputs. -/
structure Ports where
  outs     : Array BV
  outsNext : Array BV
  ins      : RuntimeInput

structure SPorts where
  outs     : SVal
  outsNext : SVal
  ins      : SVal

def PortsRel (σ : Asg) (P : SPorts) (C : Ports) : Prop :=
  SRel σ P.outs (encBVs C.outs) ∧ SRel σ P.outsNext (encBVs C.outsNext) ∧ SRel σ P.ins (encBVs C.ins)

def PortW.val (C : Ports) : PortW → Option BV
  | .out o false => C.outs[o]?
  | .out o true  => C.outsNext[o]?
  | .inp k       => C.ins[k]?

def PortW.sym (P : SPorts) : PortW → Option Leaf
  | .out o false => sNth P.outs o >>= bvOfS
  | .out o true  => sNth P.outsNext o >>= bvOfS
  | .inp k       => sNth P.ins k >>= bvOfS

theorem nth_sym_sound {σ : Asg} {s : SVal} {xs : Array BV} (hr : SRel σ s (encBVs xs)) {k : Nat}
    {l : Leaf} (h : (sNth s k >>= bvOfS) = some l) : ∃ b, xs[k]? = some b ∧ LeafRel σ l b := by
  obtain ⟨w, e⟩ := l
  simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨sk, hsk, hb⟩ := h
  obtain ⟨b, hbk, rb⟩ := sNth_sound s xs.toList k sk hr hsk
  exact ⟨b, by simpa using hbk, bvOfS_sound rb hb⟩

theorem PortW.sym_sound {σ : Asg} {P : SPorts} {C : Ports} (hP : PortsRel σ P C) :
    ∀ (p : PortW) (l : Leaf), p.sym P = some l → ∃ b, p.val C = some b ∧ LeafRel σ l b
  | .out _ false, _, h => nth_sym_sound hP.1 h
  | .out _ true, _, h => nth_sym_sound hP.2.1 h
  | .inp _, _, h => nth_sym_sound hP.2.2 h

/-- A bit vector as the `BitVec` of its width. -/
def BV.bits (b : BV) : BitVec b.width := BitVec.ofNat b.width (bv_uint b).toNat

theorem leaf_toNat {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV} (h : LeafRel σ ⟨w, e⟩ b) :
    (e.eval σ).toNat = (BV.bits b).toNat := by
  obtain ⟨hw, hu⟩ := h
  simp only [BV.bits, BitVec.toNat_ofNat]
  have h0 := bv_uint_nonneg' b
  have h1 := bv_uint_lt' b
  have hlt : (bv_uint b).toNat < 2 ^ b.width := by
    have : ((bv_uint b).toNat : Int) < ((2 ^ b.width : Nat) : Int) := by
      rw [Int.toNat_of_nonneg h0]; exact_mod_cast h1
    exact_mod_cast this
  rw [Nat.mod_eq_of_lt hlt]
  have : ((e.eval σ).toNat : Int) = ((bv_uint b).toNat : Int) := by
    rw [← hu, Int.toNat_of_nonneg h0]
  exact_mod_cast this

theorem extract_of_toNat {w w' : Nat} {x : BitVec w} {y : BitVec w'} (h : x.toNat = y.toNat)
    (lo len : Nat) : x.extractLsb' lo len = y.extractLsb' lo len := by
  apply BitVec.eq_of_toNat_eq
  simp only [BitVec.extractLsb'_toNat, h]

theorem getLsbD_of_toNat {w w' : Nat} {x : BitVec w} {y : BitVec w'} (h : x.toNat = y.toNat)
    (i : Nat) : x.getLsbD i = y.getLsbD i := by
  simp only [BitVec.getLsbD, h]

/-! ## Booleans over ports -/

inductive BitE where
  | tt
  | ff
  | nz  (p : PortW)
  | eqc (p : PortW) (v : Nat)
  | not (a : BitE)
  | and (a b : BitE)
  | or  (a b : BitE)
  deriving Repr, Inhabited

def BitE.bit (C : Ports) : BitE → Bool
  | .tt => true
  | .ff => false
  | .nz p => match p.val C with | some b => bv_nonzero b | none => false
  | .eqc p v => match p.val C with | some b => decide (bv_uint b = v) | none => false
  | .not a => !a.bit C
  | .and a b => a.bit C && b.bit C
  | .or a b => a.bit C || b.bit C

/-- `e = v` as a bit, for any width (a value out of range never holds). -/
def eqConstBit {w : Nat} (e : BVExpr w) (v : Nat) : BVExpr 1 :=
  SInt.eq (SInt.ofUint e) (SInt.lit v)

theorem bit_eqConstBit {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV} (v : Nat)
    (hu : bv_uint b = ((e.eval σ).toNat : Int)) :
    bitOf ((eqConstBit e v).eval σ) = decide (bv_uint b = v) := by
  simp only [eqConstBit, SInt.bit_eq, SInt.val_ofUint, SInt.val_lit, hu]

def BitE.sym (P : SPorts) : BitE → Option (BVExpr 1)
  | .tt => some trueE
  | .ff => some falseE
  | .nz p => (p.sym P).map fun l => nzBit l.2
  | .eqc p v => (p.sym P).map fun l => eqConstBit l.2 v
  | .not a => (a.sym P).map notE
  | .and a b => do let x ← a.sym P; let y ← b.sym P; pure (andE x y)
  | .or a b => do let x ← a.sym P; let y ← b.sym P; pure (orE x y)

theorem BitE.sym_sound {σ : Asg} {P : SPorts} {C : Ports} (hP : PortsRel σ P C) :
    ∀ (f : BitE) (e : BVExpr 1), f.sym P = some e → bitOf (e.eval σ) = f.bit C
  | .tt, e, h => by simp [BitE.sym] at h; subst h; simp [BitE.bit, trueE, bitOf]
  | .ff, e, h => by simp [BitE.sym] at h; subst h; simp [BitE.bit, falseE, bitOf]
  | .nz p, e, h => by
      simp only [BitE.sym, Option.map_eq_some_iff] at h
      obtain ⟨l, hl, rfl⟩ := h
      obtain ⟨b, hb, hr⟩ := p.sym_sound hP l hl
      simp only [BitE.bit, hb]
      exact bit_nzBit hr.2
  | .eqc p v, e, h => by
      simp only [BitE.sym, Option.map_eq_some_iff] at h
      obtain ⟨l, hl, rfl⟩ := h
      obtain ⟨b, hb, hr⟩ := p.sym_sound hP l hl
      simp only [BitE.bit, hb]
      exact bit_eqConstBit v hr.2
  | .not a, e, h => by
      simp only [BitE.sym, Option.map_eq_some_iff] at h
      obtain ⟨x, hx, rfl⟩ := h
      simp only [bit_notE, BitE.bit, BitE.sym_sound hP a x hx]
  | .and a b, e, h => by
      simp only [BitE.sym, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, y, hy, rfl⟩ := h
      simp only [bit_andE, BitE.bit, BitE.sym_sound hP a x hx, BitE.sym_sound hP b y hy]
  | .or a b, e, h => by
      simp only [BitE.sym, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, y, hy, rfl⟩ := h
      simp only [bit_orE, BitE.bit, BitE.sym_sound hP a x hx, BitE.sym_sound hP b y hy]

/-! ## Observed words -/

/-- Bits `lo .. lo+len-1` of a port, read as 0 unless bit `i` of port `g` is set. -/
structure ObsW where
  port  : PortW
  lo    : Nat := 0
  len   : Nat
  guard : Option (PortW × Nat) := none
  deriving Repr, Inhabited

/-- The guarded slice as a `BitVec`. -/
def sliceBits (b : BV) (lo len : Nat) (g : Option (BV × Nat)) : BitVec len :=
  match g with
  | none => (BV.bits b).extractLsb' lo len
  | some (gb, i) => if (BV.bits gb).getLsbD i then (BV.bits b).extractLsb' lo len else 0#len

def ObsW.val (C : Ports) (w : ObsW) : Option Int := do
  let b ← w.port.val C
  match w.guard with
  | none => pure ((sliceBits b w.lo w.len none).toNat : Int)
  | some (g, i) =>
      let gb ← g.val C
      pure ((sliceBits b w.lo w.len (some (gb, i))).toNat : Int)

def ObsW.sym (P : SPorts) (w : ObsW) : Option (BVExpr w.len) := do
  let ⟨_, e⟩ ← w.port.sym P
  let x : BVExpr w.len := .extract w.lo w.len e
  match w.guard with
  | none => pure x
  | some (g, i) =>
      let ⟨_, ge⟩ ← g.sym P
      pure (muxE (.extract i 1 ge) x (.const 0#w.len))

theorem ObsW.sym_sound {σ : Asg} {P : SPorts} {C : Ports} (hP : PortsRel σ P C) (w : ObsW)
    (e : BVExpr w.len) (h : w.sym P = some e) :
    ∃ v, w.val C = some v ∧ ((e.eval σ).toNat : Int) = v := by
  obtain ⟨port, lo, len, guard⟩ := w
  simp only [ObsW.sym, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨⟨pw, pe⟩, hpe, h⟩ := h
  obtain ⟨b, hb, hrb⟩ := PortW.sym_sound hP port _ hpe
  have hev := leaf_toNat hrb
  cases guard with
  | none =>
    simp only [Option.pure_def, Option.some.injEq] at h
    subst h
    refine ⟨((sliceBits b lo len none).toNat : Int), by simp [ObsW.val, hb], ?_⟩
    simp only [BVExpr.eval_extract, sliceBits, extract_of_toNat hev]
  | some gi =>
    obtain ⟨g, i⟩ := gi
    simp only [Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
    obtain ⟨⟨gw, ge⟩, hge, rfl⟩ := h
    obtain ⟨gb, hgb, hrg⟩ := PortW.sym_sound hP g _ hge
    have hgev := leaf_toNat hrg
    refine ⟨((sliceBits b lo len (some (gb, i))).toNat : Int), by simp [ObsW.val, hb, hgb], ?_⟩
    rw [eval_muxE]
    have hbit : bitOf ((BVExpr.extract i 1 ge).eval σ) = (BV.bits gb).getLsbD i := by
      simp only [BVExpr.eval_extract, bitOf]
      simp only [BitVec.getLsbD_extractLsb', Nat.add_zero, Nat.zero_lt_one,
        decide_true, Bool.true_and]
      exact getLsbD_of_toNat hgev i
    rw [hbit]
    simp only [BVExpr.eval_extract, BVExpr.eval_const, sliceBits, extract_of_toNat hev]

/-- The observed word as a bit vector of its width (for `eqBvE`). -/
def ObsW.bv (C : Ports) (w : ObsW) : Option BV := (w.val C).map (mk_bv w.len)

end HDLProof.Kit
