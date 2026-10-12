/-
# A SELF-CONTAINED finite memory image, and its OBJECT-LANGUAGE evaluation

## The gap this closes

`Projection/MemLog.lean` gave a base INDEX plus an override list, and its
`denote` still took an external `Array (Int → BV)` of base functions.  Those are
valid relative write-log lemmas, but the base images were a HOST-FUNCTION ORACLE:
`encMemLog` encoded the index and the overrides, never the functions, so nothing
first-order could evaluate the result.  A projected `Program` cannot call a Lean
function sitting outside the encoding.  That obligation was NOT discharged there,
and this file does not reuse the index.

Here a memory image is finite first-order data and nothing else:

    MemImg := words : List BV   -- the actual contents, index = address
              dflt  : BV        -- every address outside [0, words.length)

`get` is therefore total without an oracle, and `encMemImg` encodes ALL of it.

## The reachable-state contract, stated rather than assumed

The reference memory is `Int → BV`, total over the integers; a finite table is
not.  The contract is explicit and is checked, never wished:

  * READS are total on both sides.  `get` answers `dflt` outside the table, and
    `readEn_eq` holds for every address with no hypothesis.
  * WRITES agree only where the table HAS the address.  `denote_setAt` carries
    `InRange` as a hypothesis, and `setAt` outside the range is a no-op rather
    than a silent extension.  A design is admissible only when its addresses are
    in range -- for a read/write memory the table is sized `2 ^ aw`, so every
    `aw`-bit address is in range by construction; `setAt_outOfRange` records what
    happens when it is not.
  * `SourceDesc.memConst` uses **`contents.size`**, NOT `2 ^ aw`: a ROM narrower
    than its address space reads `dflt = 0` above its contents, which is what
    `cert_mem_read` does against an image that was never written there.  Initial
    contents are PRESERVED, not zeroed.

No total first-order round trip for arbitrary `Int → BV` is claimed, because none
exists.  What is claimed is a round trip for `MemImg`, and an exact denotation.

## The object language really evaluates it

`memProg` is a `Program` in the projection's own object language, built from its
own `Prim`s -- list walking with `isNil`/`hd`/`tl`/`consP`, address arithmetic
with `eqI`/`ltI`/`addI`/`subI`/`mulI`/`divI`, and bit work with `bvMk`/`bvBit`.
It is NOT a host wrapper that calls `cert_mem_read`.  `getGo_body` proves the
table WALK correct for every list and address; `memGet_sound` lifts that to the
ENCODED-IMAGE wrapper, so `call 1` on an `encMemImg` equals `MemImg.get`.  The
worked examples at PicoRV32's shape are `evalFuel` runs over encoded data.

Fuel bounds differ per theorem and are NOT interchangeable: `getGo_body` needs
`4 * length + 8`, the literal-argument `getGo_sound` needs `+ 9`, and the wrapper
`memGet_sound` needs `+ 14`.

## Fuel, honestly

The lookup walks the table, so the old syntax-height bound does not cover it.

CORRECTION to an earlier note here: the bound is the TABLE LENGTH, not "the
address, fixed by the design".  The address is RUN-TIME data and is fixed by
nothing.  The walk does not need it to be: `getGo` stops at `nil`, so a negative
or huge address terminates there and yields the default.  What is
design-determined is the LENGTH (`2 ^ aw`, or `contents.size` for a ROM), and
that is what makes the bound design-dependent rather than data-dependent.
`memFuel` RECORDS a proposed bound; it is not yet a proved one -- see the
obligations at the end.  Wiring any bound into `Hw.checkResidual` is separately
open and is not assumed here.
-/
import LeanSemanticPrimitives.Projection.RuntimeEncoding

namespace Projection
open Compiler

/-! ## The image -/

structure MemImg where
  /-- the contents; index is the address -/
  words : List BV
  /-- the word at every address outside `[0, words.length)` -/
  dflt  : BV
deriving Repr, Inhabited, DecidableEq

namespace MemImg

/-- Total, and with no external array: this is the whole image. -/
def get (m : MemImg) (x : Int) : BV :=
  if 0 ≤ x then m.words[x.toNat]?.getD m.dflt else m.dflt

/-- The image AS the reference's function-valued memory. -/
def denote (m : MemImg) : Int → BV := m.get

/-- `x` is an address the table actually holds. -/
def InRange (m : MemImg) (x : Int) : Prop := 0 ≤ x ∧ x.toNat < m.words.length

instance (m : MemImg) (x : Int) : Decidable (InRange m x) := by
  unfold InRange; infer_instance

/-- In range: replace.  Out of range: NO-OP, never a silent extension. -/
def setAt (m : MemImg) (x : Int) (d : BV) : MemImg :=
  if 0 ≤ x ∧ x.toNat < m.words.length then { m with words := m.words.set x.toNat d } else m

/-- A read/write memory covering an `aw`-bit address space. -/
def zeroImg (aw dw : Nat) : MemImg := ⟨List.replicate (2 ^ aw) (mk_bv dw 0), mk_bv dw 0⟩

/-- `SourceDesc.memConst`: **actual contents**, zero above them.  The table is
`contents.size` long, NOT `2 ^ aw`, and the words are preserved. -/
def ofMemConst (dw : Nat) (contents : Array Int) : MemImg :=
  ⟨contents.toList.map (mk_bv dw), mk_bv dw 0⟩

@[simp] theorem get_neg (m : MemImg) {x : Int} (h : x < 0) : m.get x = m.dflt := by
  simp [get, Int.not_le.mpr h]

@[simp] theorem denote_eq_get (m : MemImg) : denote m = m.get := rfl

/-! ## Correspondence with the reference operators -/

/-- The read is the reference read, for EVERY address, with no hypothesis. -/
def readEn (w : Nat) (m : MemImg) (a en : BV) : BV := cert_mem_read w (denote m) a en

theorem readEn_eq (w : Nat) (m : MemImg) (a en : BV) :
    readEn w m a en = cert_mem_read w (denote m) a en := rfl

/-- The enabled write, as a table update. -/
def writeEn (m : MemImg) (a d en : BV) : MemImg :=
  if bv_nonzero en then setAt m (bv_uint a) d else m

/-- The byte-enabled write.  The masked word is computed against the CURRENT
contents at that address, exactly as the reference does. -/
def writeBE (w : Nat) (m : MemImg) (a d bev : BV) (byte_w : Nat) : MemImg :=
  if bv_nonzero bev then
    setAt m (bv_uint a) (cert_masked_update w (get m (bv_uint a)) d bev byte_w)
  else m

theorem get_setAt_self (m : MemImg) {x : Int} (h : InRange m x) (d : BV) :
    get (setAt m x d) x = d := by
  obtain ⟨h0, hl⟩ := h
  simp [setAt, get, h0, hl, List.getElem?_set_self]

theorem get_setAt_other (m : MemImg) (x y : Int) (d : BV) (hxy : y ≠ x) :
    get (setAt m x d) y = get m y := by
  unfold setAt
  by_cases h : 0 ≤ x ∧ x.toNat < m.words.length
  · simp only [h, if_true]
    by_cases hy : 0 ≤ y
    · have : y.toNat ≠ x.toNat := by
        intro hc; exact hxy (by omega)
      simp [get, hy, List.getElem?_set_ne (Ne.symm this)]
    · simp [get, hy]
  · simp [h]

/-- **The enabled write is faithful, where the table holds the address.**  The
hypothesis is the contract, not a convenience: a finite table cannot represent a
write to an address it does not have. -/
theorem denote_writeEn (m : MemImg) (a d en : BV) (h : InRange m (bv_uint a)) :
    denote (writeEn m a d en) = cert_mem_write (denote m) a d en := by
  unfold writeEn cert_mem_write
  by_cases hen : bv_nonzero en
  · simp only [hen, if_true]
    funext x
    by_cases hx : x = bv_uint a
    · subst hx; simpa using get_setAt_self m h d
    · simpa [hx] using get_setAt_other m (bv_uint a) x d hx
  · simp [hen]

/-- **The byte-enabled write is faithful**, under the same contract. -/
theorem denote_writeBE (w : Nat) (m : MemImg) (a d bev : BV) (byte_w : Nat)
    (h : InRange m (bv_uint a)) :
    denote (writeBE w m a d bev byte_w)
      = cert_mem_write_be w (denote m) a d bev byte_w := by
  unfold writeBE cert_mem_write_be
  by_cases hb : bv_nonzero bev
  · simp only [hb, if_true]
    funext x
    by_cases hx : x = bv_uint a
    · subst hx; simpa using get_setAt_self m h _
    · simpa [hx] using get_setAt_other m (bv_uint a) x _ hx
  · simp [hb]

/-- Out of range OUR table update is a no-op.  The REFERENCE write is NOT: a
`cert_mem_write` at an address the table does not hold takes effect on the
function-valued memory, which is exactly why `InRange` is a real premise of
`denote_writeEn`/`denote_writeBE` and must be ESTABLISHED for a supported
source/state/operand path, never assumed from a label.  `zeroImg_inRange`
establishes it for a `2 ^ aw` read/write table; a ROM is read-only, so no write
premise arises there. -/
theorem setAt_outOfRange (m : MemImg) {x : Int} (h : ¬ InRange m x) (d : BV) :
    setAt m x d = m := by
  unfold setAt InRange at *; simp [h]

/-- Every `aw`-bit address is in range of a `zeroImg aw dw`, so a read/write
memory built this way satisfies the contract by construction. -/
theorem zeroImg_inRange (aw dw : Nat) (a : BV) (h : a.width = aw) :
    InRange (zeroImg aw dw) (bv_uint a) := by
  have hpos : (0:Int) < 2 ^ a.width := by
    have : 0 < 2 ^ a.width := Nat.two_pow_pos a.width
    exact_mod_cast this
  have hnn : 0 ≤ bv_uint a := by
    unfold bv_uint; exact Int.emod_nonneg _ (Int.ne_of_gt hpos)
  have hlt : bv_uint a < 2 ^ a.width := by
    unfold bv_uint; exact Int.emod_lt_of_pos _ hpos
  refine ⟨hnn, ?_⟩
  unfold zeroImg
  simp only [List.length_replicate]
  subst h
  have hcast : ((2 ^ a.width : Nat) : Int) = (2:Int) ^ a.width := by
    simp [Int.natCast_pow]
  have hc : ((bv_uint a).toNat : Int) < ((2 ^ a.width : Nat) : Int) := by
    rw [Int.toNat_of_nonneg hnn, hcast]; exact hlt
  exact_mod_cast hc

/-- A ROM narrower than its address space reads zero above its contents -- what
`cert_mem_read` gives against an image never written there. -/
theorem ofMemConst_above (dw : Nat) (contents : Array Int) {x : Int}
    (h : contents.size ≤ x.toNat) (h0 : 0 ≤ x) :
    get (ofMemConst dw contents) x = mk_bv dw 0 := by
  simp [get, ofMemConst, h0, List.getElem?_eq_none, h]

/-! ## Encoding -/

def tagMemImg : Nat := 124

@[inline] def encMemImg (m : MemImg) : Val :=
  .ctor tagMemImg [encListG ofBV m.words, ofBV m.dflt]

def decMemImg (v : Val) : Option MemImg :=
  (field2 tagMemImg v).bind (fun p =>
    (decListG asBV p.1).bind (fun ws =>
      (asBV p.2).map (fun d => ⟨ws, d⟩)))

@[simp] theorem decMemImg_encMemImg (m : MemImg) : decMemImg (encMemImg m) = some m := by
  cases m with
  | mk ws d =>
    simp [decMemImg, encMemImg, field2,
          decListG_encListG (e := ofBV) (d := asBV) asBV_ofBV]

def MemRel (v : Val) (m : MemImg) : Prop := decMemImg v = some m

theorem MemRel_encMemImg (m : MemImg) : MemRel (encMemImg m) m := decMemImg_encMemImg m

theorem MemRel_functional {v : Val} {m n : MemImg} (h₁ : MemRel v m) (h₂ : MemRel v n) :
    m = n :=
  Option.some.inj (h₁ ▸ h₂)

end MemImg

/-! ## The OBJECT-LANGUAGE implementation

Nothing below calls `cert_mem_read`, `MemImg.get` or any other host function.
These are `Term`s over the projection's own `Prim`s, evaluated by `evalFuel`
against an `encMemImg`-encoded image.  That is the point: a projected `Program`
can run them, and no base-image oracle is reachable from one.

Function table:

    0  getGo  (ws, i, d)                         walk the table
    1  get    (img, x)                           unpack, bounds-check, walk
    2  setGo  (ws, i, d)                         rebuild with one word replaced
    3  set    (img, x, d)                        unpack, rebuild the ctor
    4  maskGo (i, w, old, new, bev, bw, pow)     the byte-mask, bit by bit
    5  writeBE(img, a, d, bev, w, bw)            mask then set
-/

namespace MemImg

/-- `getGo ws i d` -- first element when `i` reaches 0, `d` when the list runs out. -/
def fGetGo : FunDef := ⟨3,
  .ite (.prim .isNil [.var 0])
       (.var 2)
       (.ite (.prim .eqI [.var 1, .lit (.int 0)])
             (.prim .hd [.var 0])
             (.call 0 [.prim .tl [.var 0], .prim .subI [.var 1, .lit (.int 1)], .var 2]))⟩

/-- `get img x`.  Env on entry: `[img, x]`. -/
def fGet : FunDef := ⟨2,
  .letIn (.prim .ctorFieldsP [.var 0])            -- [fields, img, x]
    (.letIn (.prim .hd [.var 0])                  -- [ws, fields, img, x]
      (.letIn (.prim .hd [.prim .tl [.var 1]])    -- [d, ws, fields, img, x]
        (.ite (.prim .ltI [.var 4, .lit (.int 0)])
              (.var 0)
              (.call 0 [.var 1, .var 4, .var 0]))))⟩

/-- `setGo ws i d` -- out of range leaves the list unchanged, matching `setAt`. -/
def fSetGo : FunDef := ⟨3,
  .ite (.prim .isNil [.var 0])
       (.lit .nil)
       (.ite (.prim .eqI [.var 1, .lit (.int 0)])
             (.prim .consP [.var 2, .prim .tl [.var 0]])
             (.prim .consP [.prim .hd [.var 0],
                            .call 2 [.prim .tl [.var 0],
                                     .prim .subI [.var 1, .lit (.int 1)], .var 2]]))⟩

/-- `set img x d`.  Env on entry: `[img, x, d]`. -/
def fSet : FunDef := ⟨3,
  .letIn (.prim .ctorFieldsP [.var 0])            -- [fields, img, x, d]
    (.letIn (.prim .hd [.var 0])                  -- [ws, fields, img, x, d]
      (.letIn (.prim .hd [.prim .tl [.var 1]])    -- [df, ws, fields, img, x, d]
        (.ite (.prim .ltI [.var 4, .lit (.int 0)])
              (.var 3)
              (.prim .mkCtorP
                [.lit (.int 124),
                 .prim .consP [.call 2 [.var 1, .var 4, .var 5],
                               .prim .consP [.var 0, .lit .nil]]]))))⟩

/-- `maskGo i w old new bev bw pow` -- bit `i` comes from `new` when byte
`i / bw` is enabled, else from `old`; `pow` carries `2 ^ i`. -/
def fMaskGo : FunDef := ⟨7,
  .ite (.prim .leI [.var 1, .var 0])
       (.lit (.int 0))
       (.prim .addI
         [.ite (.ite (.prim .bvBit [.var 4, .prim .divI [.var 0, .var 5]])
                     (.prim .bvBit [.var 3, .var 0])
                     (.prim .bvBit [.var 2, .var 0]))
               (.var 6) (.lit (.int 0)),
          .call 4 [.prim .addI [.var 0, .lit (.int 1)], .var 1, .var 2, .var 3,
                   .var 4, .var 5, .prim .mulI [.var 6, .lit (.int 2)]]])⟩

/-- `writeBE img a d bev w bw`.  Env on entry: `[img, a, d, bev, w, bw]`. -/
def fWriteBE : FunDef := ⟨6,
  .ite (.prim .eqI [.prim .bvUint [.var 3], .lit (.int 0)])
       (.var 0)
       (.letIn (.prim .bvUint [.var 1])                                   -- [adr, img,a,d,bev,w,bw]
         (.letIn (.call 1 [.var 1, .var 0])                               -- [old, adr, img,a,d,bev,w,bw]
           (.letIn (.prim .bvMk
                     [.var 6,
                      .call 4 [.lit (.int 0), .var 6, .var 0, .var 4, .var 5,
                               .var 7, .lit (.int 1)]])                   -- [msk, old, adr, img,a,d,bev,w,bw]
             (.call 3 [.var 3, .var 2, .var 0]))))⟩

def memProg : Program := ⟨[fGetGo, fGet, fSetGo, fSet, fMaskGo, fWriteBE], 1⟩

/-! ### Executed, and what is NOT yet proved

The object-language functions below are RUN -- `#guard` evaluates them and fails
the build if the answer is wrong -- on PicoRV32's actual memory shape, `aw 5,
dw 32`.  Every run goes through `evalFuel` over `encMemImg`-encoded data, so it
is projected execution, not a host wrapper.

**The general lookup theorem is now PROVED** -- `getGo_body` and `getGo_sound`
above, for every list, every run-time address (negative, in range, or past the
end) and every fuel at or above `4 * length + 8`.  The earlier obstacle was
tactic plumbing, and the fix was to stop unfolding the six-function program with
a broad `simp`: the small one-step lemmas (`eval_lit_succ`, `eval_var_succ`,
`evalList_cons_eq`, `eval_prim_succ`, `eval_ite_*_succ`, `eval_call_succ`) do the
reduction explicitly, the list-index step is a separate lemma with its integer
arithmetic discharged by `omega`, and the induction is on the LIST.

What is still NOT proved for the other two paths: the `set`/`setGo` update and
the `maskGo`/`writeBE` byte mask have no general correspondence theorem yet, only
the executed `#guard`s below.  They are the next ones, and the same lemma kit
applies to them.

**Fuel is bounded statically, and NOT by the syntax height.**  `getGo` walks the
table and stops at `nil`, so its cost is the table LENGTH -- the run-time address
does not enter the bound, and is fixed by nothing.  `maskGo` runs once per bit,
so its cost is the word width.  Length and width are design-determined (`2 ^ aw`
or `contents.size`, and `dw`), which is what makes the bound design-dependent
rather than data-dependent.  `memFuel` RECORDS a proposal; `getGo_body` below
PROVES the lookup half of it.  The existing `Hw.checkResidual` height bound does
NOT cover these traversals, and wiring a bound into the checker is a separate
open obligation; nothing here assumes the old bound suffices. -/

/-- PROPOSED fuel bound -- in the table LENGTH (the walk stops at `nil`, so the
run-time address does not enter it) and the word width (one mask pass per bit).
This is a recorded proposal, NOT a theorem: nothing below proves it. -/
def memFuel (len width : Nat) : Nat := 4 * len + 6 * width + 32

/-! ### Small evaluation lemmas

Unfolding the whole six-function program with a broad `simp` does not drive the
nested `evalFuel`/`evalFuelList` matches to completion.  These one-step lemmas do
the reduction explicitly, so each proof below is a chain of rewrites rather than
a hope about `simp`. -/

section EvalHelpers
variable {P : Program} {ρ : Env}

theorem eval_lit_succ (n : Nat) (v : Val) : evalFuel (n + 1) P ρ (.lit v) = .value v := by
  simp [evalFuel]

@[simp] theorem evalList_nil_eq (n : Nat) : evalFuelList n P ρ ([] : List Term) = .inl [] := by
  cases n <;> simp [evalFuelList]

theorem eval_var_succ (n i : Nat) {v : Val} (h : ρ[i]? = some v) :
    evalFuel (n + 1) P ρ (.var i) = .value v := by
  simp [evalFuel, h]

theorem evalList_cons_eq (n : Nat) (t : Term) (ts : List Term) {v : Val} {vs : List Val}
    (h : evalFuel n P ρ t = .value v) (hs : evalFuelList n P ρ ts = .inl vs) :
    evalFuelList n P ρ (t :: ts) = .inl (v :: vs) := by
  simp [evalFuelList, h, hs]

theorem eval_prim_succ (n : Nat) (p : Prim) (ts : List Term) {vs : List Val} {r : Val}
    (hs : evalFuelList n P ρ ts = .inl vs) (hp : evalPrim p vs = .ok r) :
    evalFuel (n + 1) P ρ (.prim p ts) = .value r := by
  simp [evalFuel, hs, hp]

theorem eval_ite_true_succ (n : Nat) {c : Term} (a b : Term)
    (hc : evalFuel n P ρ c = .value (.bool true)) :
    evalFuel (n + 1) P ρ (.ite c a b) = evalFuel n P ρ a := by
  simp [evalFuel, hc]

theorem eval_ite_false_succ (n : Nat) {c : Term} (a b : Term)
    (hc : evalFuel n P ρ c = .value (.bool false)) :
    evalFuel (n + 1) P ρ (.ite c a b) = evalFuel n P ρ b := by
  simp [evalFuel, hc]

theorem eval_letIn_succ (n : Nat) {e : Term} (b : Term) {v : Val}
    (he : evalFuel n P ρ e = .value v) :
    evalFuel (n + 1) P ρ (.letIn e b) = evalFuel n P (v :: ρ) b := by
  simp [evalFuel, he]

theorem eval_call_succ (n f : Nat) (ts : List Term) {vs : List Val} {fd : FunDef}
    (hs : evalFuelList n P ρ ts = .inl vs) (hf : P.fn f = some fd)
    (ha : fd.arity = vs.length) :
    evalFuel (n + 1) P ρ (.call f ts) = evalFuel n P vs fd.body := by
  simp [evalFuel, hs, hf, ha]

end EvalHelpers

/-! ### The list-index step, with the integer arithmetic discharged separately -/

theorem cons_index_step (a : BV) (t : List BV) (d : BV) {x : Int} (hx : x ≠ 0) :
    (if 0 ≤ x then ((a :: t)[x.toNat]?.getD d) else d)
      = (if 0 ≤ x - 1 then (t[(x - 1).toNat]?.getD d) else d) := by
  by_cases hpos : 0 ≤ x
  · have h2 : (0:Int) ≤ x - 1 := by omega
    have hnat : x.toNat = (x - 1).toNat + 1 := by omega
    rw [if_pos hpos, if_pos h2, hnat, List.getElem?_cons_succ]
  · have h2 : ¬ (0:Int) ≤ x - 1 := by omega
    rw [if_neg hpos, if_neg h2]

/-- **The object-language table walk is correct for every list and every
run-time address.**

The address is quantified, not assumed small or known: a negative address and an
address past the end both walk to `nil` and give the default, which is what the
`if 0 ≤ x` records.  The fuel bound is in the TABLE LENGTH, and the induction is
on the LIST, so nothing here depends on the address.

Stated about the function BODY with a value environment, because that is what the
recursive `call` re-enters: its arguments are computed terms, so a hypothesis
phrased about a `call` with literal arguments would not apply. -/
theorem getGo_body : ∀ (ws : List BV) (x : Int) (d : BV) (n : Nat),
    4 * ws.length + 8 ≤ n →
    evalFuel n memProg [encListG ofBV ws, Val.int x, ofBV d] fGetGo.body
      = .value (ofBV (if 0 ≤ x then (ws[x.toNat]?.getD d) else d)) := by
  intro ws
  induction ws with
  | nil =>
    intro x d n hn
    simp only [List.length_nil] at hn
    rcases n with _ | _ | _ | k
    · omega
    · omega
    · omega
    have hc : evalFuel (k + 2) memProg [encListG ofBV ([] : List BV), Val.int x, ofBV d]
                (.prim .isNil [.var 0]) = .value (.bool true) :=
      eval_prim_succ (k + 1) _ _
        (evalList_cons_eq _ _ _ (eval_var_succ k 0 rfl) (evalList_nil_eq _)) (by simp [evalPrim, encListG])
    show evalFuel (k + 3) memProg _ (Term.ite _ _ _) = _
    rw [eval_ite_true_succ (k + 2) _ _ hc, eval_var_succ (k + 1) 2 rfl]
    simp
  | cons a t ih =>
    intro x d n hn
    simp only [List.length_cons] at hn
    rcases n with _ | _ | _ | _ | _ | k
    · omega
    · omega
    · omega
    · omega
    · omega
    have hL : encListG ofBV (a :: t) = .cons (ofBV a) (encListG ofBV t) := rfl
    have hc1 : evalFuel (k + 4) memProg [encListG ofBV (a :: t), Val.int x, ofBV d]
                 (.prim .isNil [.var 0]) = .value (.bool false) :=
      eval_prim_succ (k + 3) _ _
        (evalList_cons_eq _ _ _ (eval_var_succ (k + 2) 0 rfl) (evalList_nil_eq _))
        (by rw [hL]; simp [evalPrim])
    show evalFuel (k + 5) memProg _ (Term.ite _ _ _) = _
    rw [eval_ite_false_succ (k + 4) _ _ hc1]
    by_cases hx0 : x = 0
    · subst hx0
      have hc2 : evalFuel (k + 3) memProg [encListG ofBV (a :: t), Val.int 0, ofBV d]
                   (.prim .eqI [.var 1, .lit (.int 0)]) = .value (.bool true) :=
        eval_prim_succ (k + 2) _ _
          (evalList_cons_eq (k + 2) _ _ (eval_var_succ (k + 1) 1 rfl)
            (evalList_cons_eq (k + 2) _ _ (eval_lit_succ (k + 1) _) (evalList_nil_eq (k + 2))))
          (by simp [evalPrim])
      rw [eval_ite_true_succ (k + 3) _ _ hc2]
      rw [eval_prim_succ (k + 2) .hd [.var 0] (r := ofBV a)
            (evalList_cons_eq _ _ _ (eval_var_succ (k + 1) 0 rfl) (evalList_nil_eq _))
            (by rw [hL]; simp [evalPrim])]
      simp
    · have hc2 : evalFuel (k + 3) memProg [encListG ofBV (a :: t), Val.int x, ofBV d]
                   (.prim .eqI [.var 1, .lit (.int 0)]) = .value (.bool false) :=
        eval_prim_succ (k + 2) _ _
          (evalList_cons_eq (k + 2) _ _ (eval_var_succ (k + 1) 1 rfl)
            (evalList_cons_eq (k + 2) _ _ (eval_lit_succ (k + 1) _) (evalList_nil_eq (k + 2))))
          (by simp [evalPrim, hx0])
      rw [eval_ite_false_succ (k + 3) _ _ hc2]
      have hargs : evalFuelList (k + 2) memProg [encListG ofBV (a :: t), Val.int x, ofBV d]
          [.prim .tl [.var 0], .prim .subI [.var 1, .lit (.int 1)], .var 2]
          = .inl [encListG ofBV t, Val.int (x - 1), ofBV d] :=
        evalList_cons_eq _ _ _
          (eval_prim_succ (k + 1) .tl [.var 0]
            (evalList_cons_eq _ _ _ (eval_var_succ k 0 rfl) (evalList_nil_eq _))
            (by rw [hL]; simp [evalPrim]))
          (evalList_cons_eq _ _ _
            (eval_prim_succ (k + 1) .subI [.var 1, .lit (.int 1)]
              (evalList_cons_eq (k + 1) _ _ (eval_var_succ k 1 rfl)
                (evalList_cons_eq (k + 1) _ _ (eval_lit_succ k _) (evalList_nil_eq (k + 1))))
              (by simp [evalPrim]))
            (evalList_cons_eq _ _ _ (eval_var_succ (k + 1) 2 rfl) (evalList_nil_eq _)))
      rw [eval_call_succ (k + 2) 0 _ hargs rfl rfl]
      rw [cons_index_step a t d hx0]
      exact ih (x - 1) d (k + 2) (by omega)

/-- The same thing at the call site, for any sufficient fuel. -/
theorem getGo_sound (ws : List BV) (x : Int) (d : BV) (ρ : Env) (n : Nat)
    (hn : 4 * ws.length + 9 ≤ n) :
    evalFuel n memProg ρ
      (.call 0 [.lit (encListG ofBV ws), .lit (.int x), .lit (ofBV d)])
      = .value (ofBV (if 0 ≤ x then (ws[x.toNat]?.getD d) else d)) := by
  rcases n with _ | m
  · omega
  · have hargs : evalFuelList m memProg ρ
        [.lit (encListG ofBV ws), .lit (.int x), .lit (ofBV d)]
        = .inl [encListG ofBV ws, Val.int x, ofBV d] := by
      rcases m with _ | j
      · omega
      · exact evalList_cons_eq (j + 1) _ _ (eval_lit_succ j _)
          (evalList_cons_eq (j + 1) _ _ (eval_lit_succ j _)
            (evalList_cons_eq (j + 1) _ _ (eval_lit_succ j _) (evalList_nil_eq (j + 1))))
    rw [eval_call_succ m 0 _ hargs rfl rfl]
    exact getGo_body ws x d m (by omega)

/-- **The ENCODED-IMAGE wrapper is correct.**  `call 1` on an `encMemImg` equals
`MemImg.get` for every image and every run-time address -- negative, in range, or
past the contents.  This is the theorem the module header claims; the table walk
alone does not discharge it, because the wrapper also unpacks the constructor,
finds the default word and does the sign test. -/
theorem memGet_sound (m : MemImg) (x : Int) (ρ : Env) (n : Nat)
    (hn : 4 * m.words.length + 15 ≤ n) :
    evalFuel n memProg ρ (.call 1 [.lit (encMemImg m), .lit (.int x)])
      = .value (ofBV (m.get x)) := by
  rcases n with _|_|_|_|_|_|_|_|j
  · omega
  · omega
  · omega
  · omega
  · omega
  · omega
  · omega
  · omega
  have hargs : evalFuelList (j + 7) memProg ρ
      [.lit (encMemImg m), .lit (.int x)] = .inl [encMemImg m, Val.int x] :=
    evalList_cons_eq (j + 7) _ _ (eval_lit_succ (j + 6) _)
      (evalList_cons_eq (j + 7) _ _ (eval_lit_succ (j + 6) _) (evalList_nil_eq (j + 7)))
  rw [eval_call_succ (j + 7) 1 _ hargs rfl rfl]
  have hf : evalFuel (j + 6) memProg [encMemImg m, Val.int x] (.prim .ctorFieldsP [.var 0])
      = .value (.cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil)) :=
    eval_prim_succ (j + 5) _ _
      (evalList_cons_eq (j + 5) _ _ (eval_var_succ (j + 4) 0 rfl) (evalList_nil_eq (j + 5)))
      (by simp [evalPrim, encMemImg, listVal])
  show evalFuel (j + 7) memProg _ (Term.letIn _ _) = _
  rw [eval_letIn_succ (j + 6) _ hf]
  have hw : evalFuel (j + 5) memProg
      [.cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil), encMemImg m, Val.int x]
      (.prim .hd [.var 0]) = .value (encListG ofBV m.words) :=
    eval_prim_succ (j + 4) _ _
      (evalList_cons_eq (j + 4) _ _ (eval_var_succ (j + 3) 0 rfl) (evalList_nil_eq (j + 4)))
      (by simp [evalPrim])
  rw [eval_letIn_succ (j + 5) _ hw]
  have hdf : evalFuel (j + 4) memProg
      [encListG ofBV m.words, .cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil),
       encMemImg m, Val.int x]
      (.prim .hd [.prim .tl [.var 1]]) = .value (ofBV m.dflt) :=
    eval_prim_succ (j + 3) _ _
      (evalList_cons_eq (j + 3) _ _
        (eval_prim_succ (j + 2) .tl [.var 1] (r := .cons (ofBV m.dflt) .nil)
          (evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 1 rfl) (evalList_nil_eq (j + 2)))
          (by simp [evalPrim]))
        (evalList_nil_eq (j + 3)))
      (by simp [evalPrim])
  rw [eval_letIn_succ (j + 4) _ hdf]
  by_cases hneg : x < 0
  · have hc : evalFuel (j + 3) memProg
        [ofBV m.dflt, encListG ofBV m.words,
         .cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil), encMemImg m, Val.int x]
        (.prim .ltI [.var 4, .lit (.int 0)]) = .value (.bool true) :=
      eval_prim_succ (j + 2) _ _
        (evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 4 rfl)
          (evalList_cons_eq (j + 2) _ _ (eval_lit_succ (j + 1) _) (evalList_nil_eq (j + 2))))
        (by simp [evalPrim, hneg])
    rw [eval_ite_true_succ (j + 3) _ _ hc, eval_var_succ (j + 2) 0 rfl,
        MemImg.get_neg m hneg]
  · have hc : evalFuel (j + 3) memProg
        [ofBV m.dflt, encListG ofBV m.words,
         .cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil), encMemImg m, Val.int x]
        (.prim .ltI [.var 4, .lit (.int 0)]) = .value (.bool false) :=
      eval_prim_succ (j + 2) _ _
        (evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 4 rfl)
          (evalList_cons_eq (j + 2) _ _ (eval_lit_succ (j + 1) _) (evalList_nil_eq (j + 2))))
        (by simp [evalPrim, hneg])
    rw [eval_ite_false_succ (j + 3) _ _ hc]
    have hcargs : evalFuelList (j + 2) memProg
        [ofBV m.dflt, encListG ofBV m.words,
         .cons (encListG ofBV m.words) (.cons (ofBV m.dflt) .nil), encMemImg m, Val.int x]
        [.var 1, .var 4, .var 0]
        = .inl [encListG ofBV m.words, Val.int x, ofBV m.dflt] :=
      evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 1 rfl)
        (evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 4 rfl)
          (evalList_cons_eq (j + 2) _ _ (eval_var_succ (j + 1) 0 rfl) (evalList_nil_eq (j + 2))))
    rw [eval_call_succ (j + 2) 0 _ hcargs rfl rfl]
    have hpos : (0:Int) ≤ x := by omega
    rw [getGo_body m.words x m.dflt (j + 2) (by omega)]
    simp [MemImg.get, hpos]

/-! ### Worked runs at PicoRV32's shape (`aw 5, dw 32`) -/

/-- A 32-entry, 32-bit register file, all zero -- `cpuregs`'s shape. -/
def picoImg : MemImg := zeroImg 5 32

private def addr7 : BV := mk_bv 5 7
private def w32   : Val := .int 32
private def bw8   : Val := .int 8

/- Read address 7 of the zeroed file, IN THE OBJECT LANGUAGE. -/
#guard evalFuel 400 memProg []
         (.call 1 [.lit (encMemImg picoImg), .lit (.int 7)])
       == .value (ofBV (mk_bv 32 0))

/- Full-width masked write of `0xDEADBEEF` at address 7, then read it back --
both steps in the object language, the write feeding the read. -/
#guard (match evalFuel 4000 memProg []
          (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                    .lit (ofBV (mk_bv 32 0xDEADBEEF)), .lit (ofBV (mk_bv 4 0xF)),
                    .lit w32, .lit bw8]) with
        | .value img => evalFuel 400 memProg [] (.call 1 [.lit img, .lit (.int 7)])
        | r          => r)
       == .value (ofBV (mk_bv 32 0xDEADBEEF))

/- A PARTIAL byte mask: write `0xFFFFFFFF` with only byte 0 enabled over a zero
word.  The answer must be `0x000000FF`, not `0xFFFFFFFF` -- this is the case a
mask that ignored `bev` would get wrong. -/
#guard (match evalFuel 4000 memProg []
          (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                    .lit (ofBV (mk_bv 32 0xFFFFFFFF)), .lit (ofBV (mk_bv 4 1)),
                    .lit w32, .lit bw8]) with
        | .value img => evalFuel 400 memProg [] (.call 1 [.lit img, .lit (.int 7)])
        | r          => r)
       == .value (ofBV (mk_bv 32 0x000000FF))

/- A second partial mask at a different byte lane: byte 2 only, so `0x00FF0000`. -/
#guard (match evalFuel 4000 memProg []
          (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                    .lit (ofBV (mk_bv 32 0xFFFFFFFF)), .lit (ofBV (mk_bv 4 4)),
                    .lit w32, .lit bw8]) with
        | .value img => evalFuel 400 memProg [] (.call 1 [.lit img, .lit (.int 7)])
        | r          => r)
       == .value (ofBV (mk_bv 32 0x00FF0000))

/- A DISABLED write leaves the image untouched, byte for byte. -/
#guard evalFuel 4000 memProg []
         (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                   .lit (ofBV (mk_bv 32 0xDEADBEEF)), .lit (ofBV (mk_bv 4 0)),
                   .lit w32, .lit bw8])
       == .value (encMemImg picoImg)

/- A neighbouring address is undisturbed by the write. -/
#guard (match evalFuel 4000 memProg []
          (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                    .lit (ofBV (mk_bv 32 0xDEADBEEF)), .lit (ofBV (mk_bv 4 0xF)),
                    .lit w32, .lit bw8]) with
        | .value img => evalFuel 400 memProg [] (.call 1 [.lit img, .lit (.int 8)])
        | r          => r)
       == .value (ofBV (mk_bv 32 0))

/- **The two implementations agree on this data.**  The object-language write
produces exactly the encoding of what the HOST `writeBE` produces -- which is the
shape the eventual general theorem has to take. -/
#guard evalFuel 4000 memProg []
         (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                   .lit (ofBV (mk_bv 32 0xDEADBEEF)), .lit (ofBV (mk_bv 4 0xF)),
                   .lit w32, .lit bw8])
       == .value (encMemImg (writeBE 32 picoImg addr7 (mk_bv 32 0xDEADBEEF) (mk_bv 4 0xF) 8))

/- …and on the partial-mask case too, which is the one that discriminates. -/
#guard evalFuel 4000 memProg []
         (.call 5 [.lit (encMemImg picoImg), .lit (ofBV addr7),
                   .lit (ofBV (mk_bv 32 0xFFFFFFFF)), .lit (ofBV (mk_bv 4 1)),
                   .lit w32, .lit bw8])
       == .value (encMemImg (writeBE 32 picoImg addr7 (mk_bv 32 0xFFFFFFFF) (mk_bv 4 1) 8))

/- A ROM keeps its CONTENTS: `ofMemConst` is `contents.size` long, and reading
inside it returns the initialised word, not zero. -/
#guard evalFuel 400 memProg []
         (.call 1 [.lit (encMemImg (ofMemConst 32 #[11, 22, 33])), .lit (.int 1)])
       == .value (ofBV (mk_bv 32 22))

/- …and above the contents it reads the default, which is what `cert_mem_read`
gives against an image never written there. -/
#guard evalFuel 400 memProg []
         (.call 1 [.lit (encMemImg (ofMemConst 32 #[11, 22, 33])), .lit (.int 9)])
       == .value (ofBV (mk_bv 32 0))

end MemImg

/-! ## A memory-aware runtime state, as first-order data

`RuntimeState` holds `mems : Array (Int → BV)`, which no first-order encoding can
carry.  `StateM` is the representable counterpart: the same flops, and one
`MemImg` per memory.  `denote` maps it onto the reference `RuntimeState`, so the
two are related by a FUNCTION rather than by an assumption.

The existing memory-free path is UNTOUCHED.  `encState`/`decState` and `StateRel`
keep their meaning and their theorems; `denote_memFree` below says exactly how
this codec extends the old one, so the memory-free case is a special case rather
than something replaced. -/

structure StateM where
  flops : Array BV
  mems  : Array MemImg
deriving Repr, Inhabited, DecidableEq

namespace StateM

/-- The reference state this denotes: each finite image becomes the
function-valued memory it means. -/
def denote (s : StateM) : RuntimeState :=
  { flops := s.flops, mems := s.mems.map MemImg.denote }

@[simp] theorem denote_flops (s : StateM) : (denote s).flops = s.flops := rfl

@[simp] theorem denote_mems_size (s : StateM) : (denote s).mems.size = s.mems.size := by
  simp [denote]

/-- **Memory-free compatibility.**  A state with no images denotes a `MemFree`
reference state, and conversely -- which is what makes the existing memory-free
path a special case of this one rather than something it replaces. -/
theorem denote_memFree (s : StateM) : MemFree (denote s) ↔ s.mems = #[] := by
  constructor
  · intro h
    have hs : s.mems.size = 0 := by
      have := congrArg Array.size h
      simpa [denote] using this
    exact Array.eq_empty_of_size_eq_zero hs
  · intro h; simp [MemFree, denote, h]

theorem denote_of_memFree (s : StateM) (h : s.mems = #[]) :
    denote s = { flops := s.flops, mems := #[] } := by
  simp [denote, h]

def tagStateM : Nat := 125

@[inline] def encStateM (s : StateM) : Val :=
  .ctor tagStateM [encBVs s.flops, encArr MemImg.encMemImg s.mems]

def decStateM (v : Val) : Option StateM :=
  (field2 tagStateM v).bind (fun p =>
    (decBVs p.1).bind (fun fs =>
      (decArr MemImg.decMemImg p.2).map (fun ms => ⟨fs, ms⟩)))

/-- The round trip: flops AND every memory image survive. -/
@[simp] theorem decStateM_encStateM (s : StateM) : decStateM (encStateM s) = some s := by
  cases s with
  | mk fs ms =>
    simp [decStateM, encStateM, field2, decBVs_encBVs,
          decArr_encArr (e := MemImg.encMemImg) (d := MemImg.decMemImg)
            MemImg.decMemImg_encMemImg]

/-- `StateRelM v s` -- object value `v` denotes memory-aware state `s`. -/
def StateRelM (v : Val) (s : StateM) : Prop := decStateM v = some s

theorem StateRelM_encStateM (s : StateM) : StateRelM (encStateM s) s := decStateM_encStateM s

/-- One value denotes at most one state -- the obligation `StateRel_functional`
carries on the memory-free side. -/
theorem StateRelM_functional {v : Val} {s t : StateM}
    (h₁ : StateRelM v s) (h₂ : StateRelM v t) : s = t :=
  Option.some.inj (h₁ ▸ h₂)

/-- …and it pins the REFERENCE state too, which is what a runtime relation built
on this will need. -/
theorem StateRelM_denote_functional {v : Val} {s t : StateM}
    (h₁ : StateRelM v s) (h₂ : StateRelM v t) : denote s = denote t :=
  congrArg denote (StateRelM_functional h₁ h₂)

/- The PROVED WRAPPER bound at PicoRV32's table size: 4*32+15 = 143 fuel, on an
encoded image, inside the table and past it. -/
#guard evalFuel 143 MemImg.memProg []
         (.call 1 [.lit (MemImg.encMemImg (MemImg.zeroImg 5 32)), .lit (.int 31)])
       == .value (ofBV (mk_bv 32 0))
#guard evalFuel 143 MemImg.memProg []
         (.call 1 [.lit (MemImg.encMemImg (MemImg.ofMemConst 32 #[11, 22, 33])), .lit (.int 2)])
       == .value (ofBV (mk_bv 32 33))

/- The PROVED bound at PicoRV32's table size (32 words): 4*32+9 = 137 fuel is
enough for a get-call at an address inside the table, at the far end, and past
it.  These agree with `getGo_sound`; they are a sanity run of it, not its proof. -/
#guard evalFuel 137 MemImg.memProg [] (.call 0 [.lit (encListG ofBV (List.replicate 32 (mk_bv 32 0))),
         .lit (.int 31), .lit (ofBV (mk_bv 32 7))]) == .value (ofBV (mk_bv 32 0))
#guard evalFuel 137 MemImg.memProg [] (.call 0 [.lit (encListG ofBV (List.replicate 32 (mk_bv 32 0))),
         .lit (.int 100000), .lit (ofBV (mk_bv 32 7))]) == .value (ofBV (mk_bv 32 7))
#guard evalFuel 137 MemImg.memProg [] (.call 0 [.lit (encListG ofBV (List.replicate 32 (mk_bv 32 0))),
         .lit (.int (-100)), .lit (ofBV (mk_bv 32 7))]) == .value (ofBV (mk_bv 32 7))

/- A memory-bearing state really does survive the codec, carrying a ROM's
initial contents: this is the case `decState` cannot represent at all. -/
#guard decStateM (encStateM ⟨#[mk_bv 3 5], #[MemImg.ofMemConst 32 #[11, 22, 33]]⟩)
       == some ⟨#[mk_bv 3 5], #[MemImg.ofMemConst 32 #[11, 22, 33]]⟩

end StateM

section Audit
#print axioms Projection.MemImg.memGet_sound
#print axioms Projection.MemImg.getGo_body
#print axioms Projection.MemImg.getGo_sound
#print axioms Projection.MemImg.cons_index_step
#print axioms Projection.MemImg.denote_writeEn
#print axioms Projection.MemImg.denote_writeBE
#print axioms Projection.MemImg.setAt_outOfRange
#print axioms Projection.MemImg.zeroImg_inRange
#print axioms Projection.MemImg.ofMemConst_above
#print axioms Projection.MemImg.decMemImg_encMemImg
#print axioms Projection.StateM.decStateM_encStateM
#print axioms Projection.StateM.denote_memFree
#print axioms Projection.StateM.StateRelM_functional
#print axioms Projection.StateM.StateRelM_denote_functional
end Audit

end Projection
