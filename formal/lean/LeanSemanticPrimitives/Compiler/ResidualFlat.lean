/-
# vc's event-driven step with memories as tables

A memory image in the certificate semantics is a function `Int → BV`, and a write is a new
function in front of the old one: a read walks back through every write since the image was
built, so reads get slower the longer a run goes (compaction flattens them every 256 cycles).

Here vc keeps each memory as a TABLE (`FMem`): words `0 .. n-1` in chunks of `c` words, and a
function `rest` for every other address. A write copies one chunk and the chunk array (when
they are shared: the image before the write stays as it was) instead of adding a closure, so a
read is two array reads at any age. `FMem.read` is the image the table stands for:
`read_write` and `read_ofFun` are all the table needs to mean what the function means.

`FVal` is vc's value with its memories as tables, `FVal.abs` its meaning as a `CertVal`, and
`denoteExprF`, the event-driven pass (`evSourcesF`, `evLoopF`) and the step's result
(`stepOfEnvF`) are vc's own (`ResidualInc`, `ResidualEv`) with tables: each is proved to give,
under `abs`, exactly what vc's gives (`denoteExprF_abs`, `evLoopF_abs`, `stepOfEnvF_abs`). So a
run with tables is, step for step, `runFull`'s from the state the tables stand for
(`runEvF_correct`), and through `CompileOpt.runEvFlatOpt_correct` `interpretDesign`'s.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualEv

namespace Compiler
namespace ResidualFlat

open Residual ResidualOpt ResidualInc

/-! ## Tables -/

/-- A memory image as a table: words `0 .. n-1` in chunks of `c` words, every other address
from `rest`. -/
structure FMem where
  n      : Nat
  c      : Nat
  chunks : Array (Array BV)
  rest   : Int → BV

/-- The empty table, `0` everywhere (what a non-memory reads as a memory). -/
def FMem.zero : FMem := { n := 0, c := 1, chunks := #[], rest := fun _ => mk_bv 0 0 }

instance : Inhabited FMem := ⟨FMem.zero⟩

/-- Word `x`, when the table holds it. -/
@[inline] def FMem.get? (m : FMem) (x : Int) : Option BV :=
  if 0 ≤ x ∧ x.toNat < m.n then (m.chunks[x.toNat / m.c]?).bind (·[x.toNat % m.c]?) else none

/-- The image the table stands for. -/
@[inline] def FMem.read (m : FMem) (x : Int) : BV := (m.get? x).getD (m.rest x)

/-- `d` at `a`: into its chunk when the table holds `a`, else in front of `rest`. -/
def FMem.write (m : FMem) (a : Int) (d : BV) : FMem :=
  if (m.get? a).isSome then
    { m with chunks := m.chunks.modify (a.toNat / m.c) (·.set! (a.toNat % m.c) d) }
  else
    let r := m.rest
    { m with rest := fun x => if x = a then d else r x }

/-- The table of image `f` over words `0 .. n-1`, in chunks of `c`. -/
def FMem.ofFun (n c : Nat) (f : Int → BV) : FMem :=
  { n, c, rest := f
    chunks := (Array.range ((n + c - 1) / c)).map fun q =>
      (Array.range (min c (n - q * c))).map fun j => f (Int.ofNat (q * c + j)) }

/-- The chunk size for `n` words: about `sqrt n`, at least 16. -/
def FMem.chunkFor (n : Nat) : Nat := max 16 (2 ^ ((Nat.log2 n + 1) / 2))

/-- Entries `0 .. n-1` loaded from a bus (`rmemLoadV`'s rule), one write each. -/
def FMem.load (w n : Nat) (m : FMem) (bus : BV) : FMem :=
  (List.range n).foldl (fun acc x => acc.write (Int.ofNat x) (mk_bv w (bv_uint bus / 2 ^ (x * w)))) m

theorem FMem.toNat_inj {x a : Int} (hx : 0 ≤ x) (ha : 0 ≤ a) (h : x.toNat = a.toNat) : x = a := by
  rw [← Int.toNat_of_nonneg hx, ← Int.toNat_of_nonneg ha, h]

theorem FMem.divmod_inj {x a c : Nat} (hq : x / c = a / c) (hr : x % c = a % c) : x = a := by
  rw [← Nat.div_add_mod x c, ← Nat.div_add_mod a c, hq, hr]

/-- **A read after a write**: the written word at its address, the old image elsewhere. -/
theorem FMem.read_write (m : FMem) (a x : Int) (d : BV) :
    (m.write a d).read x = if x = a then d else m.read x := by
  unfold FMem.write
  split
  · -- the table holds `a`: the write went into its chunk
    rename_i hs
    obtain ⟨v, hv⟩ := Option.isSome_iff_exists.mp hs
    have hv' := hv
    simp only [FMem.get?] at hv'
    split at hv'
    · rename_i ha
      obtain ⟨ch, hch, hj⟩ := Option.bind_eq_some_iff.mp hv'
      have hjlt : a.toNat % m.c < ch.size := by
        rcases Nat.lt_or_ge (a.toNat % m.c) ch.size with h | h
        · exact h
        · rw [Array.getElem?_eq_none h] at hj; simp at hj
      simp only [FMem.read, FMem.get?]
      by_cases hxa : x = a
      · subst hxa
        simp only [ha, and_self, ite_true, Array.getElem?_modify, hch, Option.map_some,
          Option.bind_some, Array.set!_eq_setIfInBounds,
          Array.getElem?_setIfInBounds_self_of_lt hjlt, Option.getD_some]
      · simp only [hxa, ite_false]
        split
        · rename_i hx
          rw [Array.getElem?_modify]
          split
          · rename_i hq
            rw [← hq, hch]
            simp only [Option.map_some, Option.bind_some, Array.set!_eq_setIfInBounds]
            have hr : a.toNat % m.c ≠ x.toNat % m.c := by
              intro hr
              exact hxa (FMem.toNat_inj hx.1 ha.1 (FMem.divmod_inj hq.symm hr.symm))
            rw [Array.getElem?_setIfInBounds_ne hr]
          · rfl
        · rfl
    · simp at hv'
  · -- it does not: the write went in front of `rest`
    rename_i hs
    have hs : m.get? a = none := by cases h : m.get? a <;> simp_all
    simp only [FMem.read]
    have hg : ∀ y, ({ m with rest := fun x => if x = a then d else m.rest x } : FMem).get? y =
        m.get? y := fun _ => rfl
    by_cases hxa : x = a
    · subst hxa; rw [hg, hs]; simp
    · rw [hg]; simp [hxa]

/-- **A table built from an image reads as that image.** -/
theorem FMem.read_ofFun (n c : Nat) (f : Int → BV) (x : Int) : (FMem.ofFun n c f).read x = f x := by
  unfold FMem.read FMem.get? FMem.ofFun
  dsimp only
  by_cases hx : 0 ≤ x ∧ x.toNat < n
  · rw [ite_eq_left hx, Array.getElem?_map, Array.getElem?_range]
    by_cases hq : x.toNat / c < (n + c - 1) / c
    · rw [ite_eq_left hq, Option.map_some, Option.bind_some, Array.getElem?_map, Array.getElem?_range]
      by_cases hj : x.toNat % c < min c (n - x.toNat / c * c)
      · rw [ite_eq_left hj, Option.map_some, Option.getD_some]
        congr 1
        rw [Nat.div_add_mod']
        exact Int.toNat_of_nonneg hx.1
      · rw [ite_eq_right hj]; rfl
    · rw [ite_eq_right hq]; rfl
  · rw [ite_eq_right hx]; rfl

theorem FMem.read_load_list (w : Nat) (bus : BV) :
    ∀ (l : List Nat) (m : FMem) (y : Int),
      (l.foldl (fun acc x => acc.write (Int.ofNat x) (mk_bv w (bv_uint bus / 2 ^ (x * w)))) m).read y
        = if (∃ x ∈ l, y = Int.ofNat x) then mk_bv w (bv_uint bus / 2 ^ (y.toNat * w)) else m.read y
  | [], m, y => by simp
  | x :: l, m, y => by
    simp only [List.foldl_cons]
    rw [FMem.read_load_list w bus l _ y, FMem.read_write]
    by_cases h1 : ∃ x' ∈ l, y = Int.ofNat x'
    · have h2 : ∃ x' ∈ x :: l, y = Int.ofNat x' := by
        obtain ⟨x', hx', hy⟩ := h1; exact ⟨x', List.mem_cons_of_mem _ hx', hy⟩
      rw [ite_eq_left h1, ite_eq_left h2]
    · rw [ite_eq_right h1]
      by_cases hy : y = Int.ofNat x
      · have h2 : ∃ x' ∈ x :: l, y = Int.ofNat x' := ⟨x, List.mem_cons_self, hy⟩
        rw [ite_eq_left hy, ite_eq_left h2, hy]
        rfl
      · have h2 : ¬ ∃ x' ∈ x :: l, y = Int.ofNat x' := by
          rintro ⟨x', hx', hy'⟩
          rcases List.mem_cons.mp hx' with h | h
          · exact hy (h ▸ hy')
          · exact h1 ⟨x', h, hy'⟩
        rw [ite_eq_right hy, ite_eq_right h2]

theorem FMem.read_load (w n : Nat) (m : FMem) (bus : BV) (y : Int) :
    (FMem.load w n m bus).read y
      = if 0 ≤ y ∧ y < n then mk_bv w (bv_uint bus / 2 ^ (y.toNat * w)) else m.read y := by
  unfold FMem.load
  rw [FMem.read_load_list]
  congr 1
  apply propext
  constructor
  · rintro ⟨x, hx, rfl⟩
    exact ⟨Int.natCast_nonneg x, by simpa using List.mem_range.mp hx⟩
  · rintro ⟨h0, hn⟩
    exact ⟨y.toNat, List.mem_range.mpr (by omega), (Int.toNat_of_nonneg h0).symm⟩

/-! ## Values, environments and states with tables -/

/-- vc's value with its memory images as tables. -/
inductive FVal where
  | bv  (x : BV)
  | mem (m : FMem)

instance : Inhabited FVal := ⟨.bv (mk_bv 0 0)⟩

/-- The certificate value a table value stands for. -/
def FVal.abs : FVal → CertVal
  | .bv x => .bv x
  | .mem m => .mem m.read

@[inline] def FVal.asBV : FVal → BV
  | .bv x => x
  | .mem _ => mk_bv 0 0

@[inline] def FVal.asMem : FVal → FMem
  | .mem m => m
  | .bv _ => FMem.zero

abbrev SlotEnvF := Array FVal

@[inline] def badValF : FVal := .bv (mk_bv 0 0)
@[inline] def denoteRefF (env : SlotEnvF) (r : Nat) : FVal := env[r]?.getD badValF
@[inline] def refBVF (env : SlotEnvF) (r : Nat) : BV := (denoteRefF env r).asBV
@[inline] def refBVsF (env : SlotEnvF) (rs : Array Nat) : List BV := rs.toList.map (refBVF env)
@[inline] def refMemF (env : SlotEnvF) (r : Nat) : FMem := (denoteRefF env r).asMem

/-- The environment a table environment stands for. -/
def absEnv (env : SlotEnvF) : SlotEnv := env.map FVal.abs

/-- A state with its memories as tables. -/
structure RuntimeStateF where
  flops : Array BV
  mems  : Array FMem

/-- The state a table state stands for. -/
def absS (s : RuntimeStateF) : RuntimeState := { flops := s.flops, mems := s.mems.map FMem.read }

/-- `s` with each memory `k` as a table over its `depth k` words. -/
def ofState (depth : Nat → Nat) (s : RuntimeState) : RuntimeStateF :=
  { flops := s.flops
    mems := (Array.range s.mems.size).map fun k =>
      FMem.ofFun (depth k) (FMem.chunkFor (depth k)) (s.mems[k]?.getD fun _ => mk_bv 0 0) }

theorem FMem.read_zero : FMem.zero.read = fun _ => mk_bv 0 0 := by
  funext x; simp [FMem.read, FMem.get?, FMem.zero]

theorem abs_asBV (v : FVal) : v.abs.asBV = v.asBV := by cases v <;> rfl

theorem abs_asMem (v : FVal) : v.abs.asMem = v.asMem.read := by
  cases v with
  | bv _ => simp only [FVal.abs, CertVal.asMem, FVal.asMem, FMem.read_zero]
  | mem _ => rfl

theorem denoteRef_abs (env : SlotEnvF) (r : Nat) :
    denoteRef (absEnv env) r = (denoteRefF env r).abs := by
  simp only [denoteRef, denoteRefF, absEnv, Array.getElem?_map]
  cases env[r]? <;> rfl

theorem refBV_abs (env : SlotEnvF) (r : Nat) : refBV (absEnv env) r = refBVF env r := by
  simp only [refBV, refBVF, denoteRef_abs, abs_asBV]

theorem refBVs_abs (env : SlotEnvF) (rs : Array Nat) : refBVs (absEnv env) rs = refBVsF env rs := by
  simp only [refBVs, refBVsF]
  congr 1
  funext r
  exact refBV_abs env r

theorem refMem_abs (env : SlotEnvF) (r : Nat) : refMem (absEnv env) r = (refMemF env r).read := by
  simp only [refMem, refMemF, denoteRef_abs, abs_asMem]

theorem absEnv_set (env : SlotEnvF) (j : Nat) (v : FVal) :
    absEnv (env.set! j v) = (absEnv env).set! j v.abs := by
  simp only [absEnv, Array.set!_eq_setIfInBounds]
  apply Array.ext_getElem?
  intro r
  simp only [Array.getElem?_map, Array.getElem?_setIfInBounds, Array.size_map]
  split
  · split <;> rfl
  · rfl

theorem absEnv_push (env : SlotEnvF) (v : FVal) : absEnv (env.push v) = (absEnv env).push v.abs := by
  simp [absEnv, Array.map_push]

/-! ## The evaluator with tables -/

/-- `denoteExpr` with memory images as tables: the same rules, a write into the table. -/
def denoteExprF (env : SlotEnvF) : ResidualExpr → FVal
  | .rsum w nAdd args     => .bv (rsumV w nAdd (refBVsF env args))
  | .rmult w args         => .bv (rmultV w (refBVsF env args))
  | .rand w args          => .bv (randV w (refBVsF env args))
  | .rorBits w args       => .bv (rorBitsV w (refBVsF env args))
  | .rxor w args          => .bv (rxorV w (refBVsF env args))
  | .rredOr w args        => .bv (rredOrV w (refBVsF env args))
  | .req w args           => .bv (reqV w (refBVsF env args))
  | .rshl w args          => .bv (rshlV w (refBVsF env args))
  | .rmuxN w args         => .bv (rmuxNV w (refBVsF env args))
  | .rnot w a             => .bv (rnotV w (refBVF env a))
  | .rult w a b           => .bv (rultV w (refBVF env a) (refBVF env b))
  | .rugt w a b           => .bv (rugtV w (refBVF env a) (refBVF env b))
  | .rslt w a b           => .bv (rsltV w (refBVF env a) (refBVF env b))
  | .rsgt w a b           => .bv (rsgtV w (refBVF env a) (refBVF env b))
  | .rsra w a b           => .bv (rsraV w (refBVF env a) (refBVF env b))
  | .rsext w a amt        => .bv (rsextV w (refBVF env a) (refBVF env amt))
  | .rgetMask w a m       => .bv (rgetMaskV w (refBVF env a) (refBVF env m))
  | .rudiv w a b          => .bv (rudivV w (refBVF env a) (refBVF env b))
  | .rsdiv w a b          => .bv (rsdivV w (refBVF env a) (refBVF env b))
  | .rsetMask w a m v     => .bv (rsetMaskV w (refBVF env a) (refBVF env m) (refBVF env v))
  | .rmux w sel fv tv     => .bv (rmuxV w (refBVF env sel) (refBVF env fv) (refBVF env tv))
  | .rmemRead w m a en    =>
      .bv (if bv_uint (refBVF env en) = 0 then mk_bv w 0
           else bv_resize w ((refMemF env m).read (bv_uint (refBVF env a))))
  | .rmemWrite m a d en   =>
      .mem (if bv_uint (refBVF env en) = 0 then refMemF env m
            else (refMemF env m).write (bv_uint (refBVF env a)) (refBVF env d))
  | .rmemWriteBE w bw m a d be =>
      .mem (if bv_uint (refBVF env be) = 0 then refMemF env m
            else (refMemF env m).write (bv_uint (refBVF env a))
              (rMaskedUpdateV w ((refMemF env m).read (bv_uint (refBVF env a))) (refBVF env d)
                (refBVF env be) bw))
  | .rmemLoad w n m bus en =>
      .mem (if bv_uint (refBVF env en) = 0 then refMemF env m
            else FMem.load w n (refMemF env m) (refBVF env bus))
  | .rmemToBus w n m      => .bv (rmemToBusV w n (refMemF env m).read)

/-- **`denoteExprF` is `denoteExpr`** on the environment the tables stand for. -/
theorem denoteExprF_abs (env : SlotEnvF) (e : ResidualExpr) :
    (denoteExprF env e).abs = denoteExpr (absEnv env) e := by
  cases e with
  | rmemRead w m a en =>
    simp only [denoteExprF, denoteExpr, FVal.abs, rmemReadV, refBV_abs, refMem_abs]
  | rmemWrite m a d en =>
    simp only [denoteExprF, denoteExpr, refBV_abs, refMem_abs]
    by_cases h : bv_uint (refBVF env en) = 0
    · rw [ite_eq_left h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; simp only [rmemWriteV, h, ite_true]
    · rw [ite_eq_right h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; rw [FMem.read_write]; simp only [rmemWriteV, h, ite_false]
  | rmemWriteBE w bw m a d be =>
    simp only [denoteExprF, denoteExpr, refBV_abs, refMem_abs]
    by_cases h : bv_uint (refBVF env be) = 0
    · rw [ite_eq_left h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; simp only [rmemWriteBEV, h, ite_true]
    · rw [ite_eq_right h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; rw [FMem.read_write]; simp only [rmemWriteBEV, h, ite_false]
  | rmemLoad w n m bus en =>
    simp only [denoteExprF, denoteExpr, refBV_abs, refMem_abs]
    by_cases h : bv_uint (refBVF env en) = 0
    · rw [ite_eq_left h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; simp only [rmemLoadV, h, ite_true]
    · rw [ite_eq_right h]; simp only [FVal.abs, CertVal.mem.injEq]
      funext x; rw [FMem.read_load]; simp only [rmemLoadV, h, ite_false]
  | rmemToBus w n m =>
    simp only [denoteExprF, denoteExpr, FVal.abs, refMem_abs]
  | _ => simp only [denoteExprF, denoteExpr, FVal.abs, refBV_abs, refBVs_abs]

/-! ## Sources, and the event-driven pass with tables -/

/-- `sourceValue` with memory images as tables. -/
def sourceValueF (i : RuntimeInput) (s : RuntimeStateF) : SourceDesc → FVal
  | .input  idx w    => .bv (bv_resize w (i[idx]?.getD (mk_bv w 0)))
  | .const  w v      => .bv (mk_bv w v)
  | .flopQ  idx w    => .bv (bv_resize w (s.flops[idx]?.getD (mk_bv w 0)))
  | .flopQAsync idx w ri rv al =>
      let r : BV := i[ri]?.getD (mk_bv 1 0)
      let asserted : Bool := if al then !(bv_nonzero r) else bv_nonzero r
      .bv (if asserted then mk_bv w rv else bv_resize w (s.flops[idx]?.getD (mk_bv w 0)))
  | .memImg idx _ _  => .mem (s.mems[idx]?.getD FMem.zero)
  | .memConst _ dw contents =>
      .mem { n := 0, c := 1, chunks := #[]
             rest := fun x => if 0 ≤ x ∧ x.toNat < contents.size
                              then mk_bv dw (contents[x.toNat]!) else mk_bv dw 0 }

theorem sourceValueF_abs (i : RuntimeInput) (s : RuntimeStateF) (sd : SourceDesc) :
    (sourceValueF i s sd).abs = sourceValue i (absS s) sd := by
  cases sd with
  | memImg idx aw dw =>
    simp only [sourceValueF, sourceValue, FVal.abs, absS, Array.getElem?_map, CertVal.mem.injEq]
    cases s.mems[idx]? with
    | none => exact FMem.read_zero
    | some m => rfl
  | memConst aw dw contents =>
    simp only [sourceValueF, sourceValue, FVal.abs, CertVal.mem.injEq]
    funext x; simp [FMem.read, FMem.get?]
  | _ => rfl

/-- Equal values, decided cheaply; a memory never compares equal (as `sameVal`). -/
@[inline] def sameValF : FVal → FVal → Bool
  | .bv x, .bv y => x.width == y.width && x.value == y.value
  | _, _ => false

theorem sameValF_abs (a b : FVal) : sameVal a.abs b.abs = sameValF a b := by
  cases a <;> cases b <;> rfl

/-- `evSources` with tables. -/
def evSourcesF (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput)
    (s : RuntimeStateF) (j : Nat) (cur : SlotEnvF) (pend : Array Bool) : SlotEnvF × Array Bool :=
  if h : j < R.sources.size then
    match R.sources[j] with
    | .const _ _ => evSourcesF R fan i s (j + 1) cur pend
    | sd =>
      let v := sourceValueF i s sd
      if sameValF v (denoteRefF cur j) then evSourcesF R fan i s (j + 1) cur pend
      else evSourcesF R fan i s (j + 1) (cur.set! j v) (markFan fan j pend)
  else (cur, pend)
termination_by R.sources.size - j

/-- `evLoop` with tables. -/
def evLoopF (R : ResidualProgram) (fan : Array (Array Nat)) (k : Nat) (cur : SlotEnvF)
    (pend : Array Bool) : SlotEnvF :=
  if h : k < R.bindings.size then
    if pend.getD k true then
      let v := denoteExprF cur R.bindings[k].rhs
      if sameValF v (denoteRefF cur (R.sources.size + k)) then evLoopF R fan (k + 1) cur pend
      else evLoopF R fan (k + 1) (cur.set! (R.sources.size + k) v)
        (markFan fan (R.sources.size + k) pend)
    else evLoopF R fan (k + 1) cur pend
  else cur
termination_by R.bindings.size - k

theorem evSourcesF_abs (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput)
    (s : RuntimeStateF) :
    ∀ (n j : Nat) (cur : SlotEnvF) (pend : Array Bool), R.sources.size - j = n →
      (absEnv (evSourcesF R fan i s j cur pend).1, (evSourcesF R fan i s j cur pend).2)
        = evSources R fan i (absS s) j (absEnv cur) pend := by
  intro n
  induction n with
  | zero =>
    intro j cur pend hn
    rw [evSourcesF, evSources, dite_eq_right (by omega), dite_eq_right (by omega)]
  | succ n ih =>
    intro j cur pend hn
    have hj : j < R.sources.size := by omega
    rw [evSourcesF, evSources, dite_eq_left hj, dite_eq_left hj]
    generalize R.sources[j] = sd
    have hnc : ∀ sd' : SourceDesc, (∀ w v, sd' ≠ .const w v) →
        (absEnv (let v := sourceValueF i s sd'
                 if sameValF v (denoteRefF cur j) = true then evSourcesF R fan i s (j + 1) cur pend
                 else evSourcesF R fan i s (j + 1) (cur.set! j v) (markFan fan j pend)).1,
         (let v := sourceValueF i s sd'
          if sameValF v (denoteRefF cur j) = true then evSourcesF R fan i s (j + 1) cur pend
          else evSourcesF R fan i s (j + 1) (cur.set! j v) (markFan fan j pend)).2)
        = (let v := sourceValue i (absS s) sd'
           if sameVal v (denoteRef (absEnv cur) j) = true then evSources R fan i (absS s) (j + 1) (absEnv cur) pend
           else evSources R fan i (absS s) (j + 1) ((absEnv cur).set! j v) (markFan fan j pend)) := by
      intro sd' _
      dsimp only
      rw [← sourceValueF_abs, denoteRef_abs, sameValF_abs]
      split
      · exact ih (j + 1) cur pend (by omega)
      · rw [← absEnv_set]; exact ih (j + 1) _ _ (by omega)
    cases sd with
    | const w v => exact ih (j + 1) cur pend (by omega)
    | input idx w => exact hnc _ (by intro _ _ h; cases h)
    | flopQ idx w => exact hnc _ (by intro _ _ h; cases h)
    | flopQAsync idx w ri rv al => exact hnc _ (by intro _ _ h; cases h)
    | memImg idx aw dw => exact hnc _ (by intro _ _ h; cases h)
    | memConst aw dw contents => exact hnc _ (by intro _ _ h; cases h)

theorem evLoopF_abs (R : ResidualProgram) (fan : Array (Array Nat)) :
    ∀ (n k : Nat) (cur : SlotEnvF) (pend : Array Bool), R.bindings.size - k = n →
      absEnv (evLoopF R fan k cur pend) = evLoop R fan k (absEnv cur) pend := by
  intro n
  induction n with
  | zero =>
    intro k cur pend hn
    rw [evLoopF, evLoop, dite_eq_right (by omega), dite_eq_right (by omega)]
  | succ n ih =>
    intro k cur pend hn
    have hk : k < R.bindings.size := by omega
    rw [evLoopF, evLoop, dite_eq_left hk, dite_eq_left hk]
    split
    · dsimp only
      rw [← denoteExprF_abs, denoteRef_abs, sameValF_abs]
      split
      · exact ih (k + 1) cur pend (by omega)
      · rw [← absEnv_set]; exact ih (k + 1) _ _ (by omega)
    · exact ih (k + 1) cur pend (by omega)

/-- One event-driven pass with tables (`evEnv`'s). -/
def evEnvF (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnvF) (i : RuntimeInput)
    (s : RuntimeStateF) : SlotEnvF :=
  let p := evSourcesF R fan i s 0 prev (Array.replicate R.bindings.size false)
  evLoopF R fan 0 p.1 p.2

theorem evEnvF_abs (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnvF)
    (i : RuntimeInput) (s : RuntimeStateF) :
    absEnv (evEnvF R fan prev i s) = evEnv R fan (absEnv prev) i (absS s) := by
  have h := evSourcesF_abs R fan i s _ 0 prev (Array.replicate R.bindings.size false) rfl
  simp only [evEnvF, evEnv]
  rw [evLoopF_abs R fan _ 0 _ _ rfl, ← h]

/-- The full environment with tables (`fullEnv`'s). -/
def fullEnvF (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeStateF) : SlotEnvF :=
  R.bindings.foldl (fun env b => env.push (denoteExprF env b.rhs)) (R.sources.map (sourceValueF i s))

theorem foldF_abs : ∀ (l : List ResidualBinding) (env : SlotEnvF),
    absEnv (l.foldl (fun env b => env.push (denoteExprF env b.rhs)) env)
      = l.foldl (fun env b => env.push (denoteExpr env b.rhs)) (absEnv env)
  | [], _ => rfl
  | b :: l, env => by
    simp only [List.foldl_cons]
    rw [foldF_abs l, absEnv_push, denoteExprF_abs]

theorem fullEnvF_abs (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeStateF) :
    absEnv (fullEnvF R i s) = fullEnv R i (absS s) := by
  simp only [fullEnvF, fullEnv, ← Array.foldl_toList]
  rw [foldF_abs]
  congr 1
  simp only [absEnv, sourceEnvArr, Array.map_map]
  congr 1
  funext sd
  exact sourceValueF_abs i s sd

/-! ## The step's result with tables -/

def flopNextF (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) (idx : Nat)
    (f : ResidualFlopUpdate) : BV :=
  let inReset : Bool :=
    match f.resetPin with
    | none   => false
    | some r => let rv := bv_nonzero (refBVF env r)
                if f.resetActiveLow then !rv else rv
  let edge : Bool := fires e f.clock
  if inReset && (edge || f.asyncReset) then mk_bv f.width f.resetValue
  else
    let en : Bool :=
      match f.enable with
      | none    => true
      | some en => bv_nonzero (refBVF env en)
    if edge && en then bv_resize f.width (refBVF env f.din)
    else s.flops[idx]?.getD (mk_bv f.width 0)

def memHeldF (env : SlotEnvF) (s : RuntimeStateF) (idx : Nat) (m : ResidualMemoryUpdate) : FMem :=
  match m.asyncImg with
  | some a => refMemF env a
  | none   => s.mems[idx]?.getD FMem.zero

def memNextF (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) (idx : Nat)
    (m : ResidualMemoryUpdate) : FMem :=
  if m.commits.isEmpty then
    if fires e m.clock then refMemF env m.nextImg
    else memHeldF env s idx m
  else if m.toDesc.allFire e then refMemF env m.nextImg
  else
    match m.commits.find? (m.toDesc.firedIs e) with
    | some c => refMemF env c.nextImg
    | none   => memHeldF env s idx m

/-- The step's outputs and next state with tables (`stepOfEnv`'s). -/
def stepOfEnvF (R : ResidualProgram) (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) :
    Array BV × RuntimeStateF :=
  (R.outputs.map fun o => bv_resize o.width (refBVF env o.slot),
   { flops := R.flopUpdates.mapIdx fun idx f => flopNextF env e s idx f
     mems  := R.memoryUpdates.mapIdx fun idx m => memNextF env e s idx m })

/-- The result a table result stands for. -/
def absRes (r : Array BV × RuntimeStateF) : RuntimeResult := { outputs := r.1, nextState := absS r.2 }

theorem flopNextF_abs (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) (idx : Nat)
    (f : ResidualFlopUpdate) : flopNext (absEnv env) e (absS s) idx f = flopNextF env e s idx f := by
  simp only [flopNext, flopNextF, refBV_abs, absS]
  rfl

theorem memHeldF_abs (env : SlotEnvF) (s : RuntimeStateF) (idx : Nat) (m : ResidualMemoryUpdate) :
    memHeld (absEnv env) (absS s) idx m = (memHeldF env s idx m).read := by
  unfold memHeld memHeldF
  cases m.asyncImg with
  | some a => exact refMem_abs env a
  | none =>
    simp only [absS, Array.getElem?_map]
    cases s.mems[idx]? with
    | none => exact FMem.read_zero.symm
    | some _ => rfl

theorem memNextF_abs (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) (idx : Nat)
    (m : ResidualMemoryUpdate) : memNext (absEnv env) e (absS s) idx m = (memNextF env e s idx m).read := by
  unfold memNext memNextF
  split
  · split
    · exact refMem_abs env _
    · exact memHeldF_abs env s idx m
  · split
    · exact refMem_abs env _
    · cases m.commits.find? (m.toDesc.firedIs e) with
      | none => exact memHeldF_abs env s idx m
      | some c => exact refMem_abs env _

/-- **The step's result with tables is `stepOfEnv`'s.** -/
theorem stepOfEnvF_abs (R : ResidualProgram) (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) :
    absRes (stepOfEnvF R env e s) = stepOfEnv R (absEnv env) e (absS s) := by
  simp only [stepOfEnv, refBV_abs, flopNextF_abs, memNextF_abs]
  simp only [absRes, stepOfEnvF, absS, FastBV.map_mapIdx]

/-! ## Runs -/

/-- Event-driven steps with tables, each from the environment the one before left. -/
def runEvFFrom (R : ResidualProgram) (fan : Array (Array Nat)) (env : SlotEnvF) :
    RuntimeStateF → List (ClockEdges × RuntimeInput) → List (Array BV × RuntimeStateF)
  | _, [] => []
  | s, (e, i) :: rest =>
    let env' := evEnvF R fan env i s
    let r := stepOfEnvF R env' e s
    r :: runEvFFrom R fan env' r.2 rest

/-- A run with tables: the first step full, the others event-driven. -/
def runEvF (R : ResidualProgram) : RuntimeStateF → List (ClockEdges × RuntimeInput) →
    List (Array BV × RuntimeStateF)
  | _, [] => []
  | s, (e, i) :: rest =>
    let env := fullEnvF R i s
    let r := stepOfEnvF R env e s
    r :: runEvFFrom R (fanout R) env r.2 rest

theorem runEvFFrom_correct {R : ResidualProgram} (hw : wellOrdered R = true) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (env : SlotEnvF) (s : RuntimeStateF)
      (i' : RuntimeInput) (s' : RuntimeState), absEnv env = fullEnv R i' s' →
      (runEvFFrom R (fanout R) env s ticks).map absRes = runFull R (absS s) ticks
  | [], _, _, _, _, _ => rfl
  | (e, i) :: rest, env, s, i', s', h => by
    have henv : absEnv (evEnvF R (fanout R) env i s) = fullEnv R i (absS s) := by
      rw [evEnvF_abs, h, evEnv_correct hw (fanout_ok R)]
    have hstep : absRes (stepOfEnvF R (evEnvF R (fanout R) env i s) e s) = denoteResidual R e i (absS s) := by
      rw [stepOfEnvF_abs, henv, denoteResidual_eq_stepOfEnv]
    simp only [runEvFFrom, runFull, List.map_cons]
    rw [hstep, runEvFFrom_correct hw rest _ _ i (absS s) henv]
    congr 2
    rw [← hstep]; rfl

/-- **A run with tables is a run of `denoteResidual`** from the state the tables stand for,
step for step. -/
theorem runEvF_correct {R : ResidualProgram} (hw : wellOrdered R = true) (s : RuntimeStateF)
    (ticks : List (ClockEdges × RuntimeInput)) :
    (runEvF R s ticks).map absRes = runFull R (absS s) ticks := by
  cases ticks with
  | nil => rfl
  | cons t rest =>
    obtain ⟨e, i⟩ := t
    have hstep : absRes (stepOfEnvF R (fullEnvF R i s) e s) = denoteResidual R e i (absS s) := by
      rw [stepOfEnvF_abs, fullEnvF_abs, denoteResidual_eq_stepOfEnv]
    simp only [runEvF, runFull, List.map_cons]
    rw [hstep, runEvFFrom_correct hw rest _ _ i (absS s) (fullEnvF_abs R i s)]
    congr 2
    rw [← hstep]; rfl

/-- **Tables built from a state stand for that state.** -/
theorem absS_ofState (depth : Nat → Nat) (s : RuntimeState) : absS (ofState depth s) = s := by
  simp only [absS, ofState, Array.map_map]
  cases s with
  | mk flops mems =>
    congr 1
    apply Array.ext
    · simp
    · intro k h1 h2
      simp only [Array.getElem_map, Array.getElem_range, Function.comp]
      funext x
      rw [FMem.read_ofFun]
      simp only [Array.size_map, Array.size_range] at h1
      rw [Array.getElem?_eq_getElem h2, Option.getD_some]

end ResidualFlat
end Compiler

#print axioms Compiler.ResidualFlat.denoteExprF_abs
#print axioms Compiler.ResidualFlat.runEvF_correct
#print axioms Compiler.ResidualFlat.absS_ofState
