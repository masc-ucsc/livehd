(** * SemanticPrimitives — total, width-explicit bit-vector primitives for [pass.rocq].

    Rocq port of [formal/semantic_primitives/SemanticPrimitives.thy] and
    [formal/lean/LeanSemanticPrimitives/SemanticPrimitives.lean].

    Design notes (see [pass/rocq/LITERATURE_REVIEW.md] §11):

    - Rocq has no canonical computable bit-vector type in the standard library,
      so we define our own.  [BitVec w] is a *parameterised record* wrapping a
      [Z].  Because [BitVec 4] and [BitVec 8] are distinct applications of the
      same inductive, the type checker catches emitter width bugs, and because
      the parameter is phantom there are no dependent proof obligations and no
      transport along width equalities.

    - Every operator returns through [bv_norm], and every read goes through
      [bv_uint], which normalises.  So the representation invariant
      "the raw value is in [0, 2^w)" is maintained by construction *and* every
      lemma below is unconditional.

    - Every primitive is TOTAL: no [False_rect], no axioms, no partiality.
      Division by zero yields 0, out-of-range shifts saturate.  This matters
      more here than in Isabelle/Lean: a stuck reduction in Rocq produces an
      enormous partially-evaluated term and exhausts memory (literature review
      §6), so nothing on the evaluation path may get stuck.

    - Shift amounts are clamped to the word width BEFORE computing [2 ^ n].
      The clamp is semantically the identity (anything shifted left by [w] or
      more is 0 mod 2^w) but it is what stops a 2^31 shift amount from trying
      to build a 2-billion-bit integer. *)

From Stdlib Require Import ZArith List Bool Lia.

Import ListNotations.
Local Open Scope Z_scope.

(** ** The bit-vector type *)

Record BitVec (w : nat) : Set := mkBitVec { bv_raw : Z }.

Arguments mkBitVec {w} _.
Arguments bv_raw {w} _.

Definition two_pow (w : nat) : Z := 2 ^ (Z.of_nat w).

(** [bv_norm w z] is the only constructor callers should use. *)
Definition bv_norm (w : nat) (z : Z) : BitVec w := mkBitVec (z mod two_pow w).

(** Unsigned interpretation.  Normalises defensively, so a hand-written
    [mkBitVec] with an out-of-range payload still behaves. *)
Definition bv_uint {w : nat} (x : BitVec w) : Z := (bv_raw x) mod two_pow w.

(** Two's-complement signed interpretation. *)
Definition bv_sint {w : nat} (x : BitVec w) : Z :=
  let u := bv_uint x in
  if u <? two_pow (Nat.pred w) then u else u - two_pow w.

Definition bv_eqb {w : nat} (a b : BitVec w) : bool := Z.eqb (bv_uint a) (bv_uint b).

Definition bv_bit {w : nat} (x : BitVec w) (i : nat) : bool :=
  Z.testbit (bv_uint x) (Z.of_nat i).

(** Comparisons.  The signed/unsigned split is resolved by the EMITTER choosing
    which of these to call (and the matching [Op_SLT]/[Op_ULT] in the
    certificate); it is never inferred from the operands.  Keeping the four
    spellings distinct here is what makes that split visible in generated code. *)

Definition bv_ultb {w : nat} (a b : BitVec w) : bool := Z.ltb (bv_uint a) (bv_uint b).
Definition bv_ugtb {w : nat} (a b : BitVec w) : bool := Z.ltb (bv_uint b) (bv_uint a).
Definition bv_sltb {w : nat} (a b : BitVec w) : bool := Z.ltb (bv_sint a) (bv_sint b).
Definition bv_sgtb {w : nat} (a b : BitVec w) : bool := Z.ltb (bv_sint b) (bv_sint a).

Definition bitvec_nonzero {w : nat} (x : BitVec w) : bool :=
  negb (Z.eqb (bv_uint x) 0).

Definition bool_to_bv1 (b : bool) : BitVec 1 :=
  bv_norm 1 (if b then 1 else 0).

(** ** Width changes *)

Definition bv_zext (b : nat) {a : nat} (x : BitVec a) : BitVec b :=
  bv_norm b (bv_uint x).

Definition bv_sext (b : nat) {a : nat} (x : BitVec a) : BitVec b :=
  bv_norm b (bv_sint x).

(** ** Arithmetic *)

Definition bv_add {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (bv_uint a + bv_uint b).

Definition bv_sub {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (bv_uint a - bv_uint b).

Definition bv_neg {w : nat} (a : BitVec w) : BitVec w :=
  bv_norm w (- bv_uint a).

Definition bv_mul {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (bv_uint a * bv_uint b).

(** Unsigned division, total: [b = 0] yields 0. *)
Definition sem_udiv {w : nat} (a b : BitVec w) : BitVec w :=
  if Z.eqb (bv_uint b) 0 then bv_norm w 0
  else bv_norm w (bv_uint a / bv_uint b).

(** Truncating (toward-zero) integer division, matching the Lean/Isabelle
    [trunc_div_int].  [Z.quot] truncates; [Z.div] floors.  [Z.quot _ 0 = 0]. *)
Definition trunc_div_int (a b : Z) : Z := Z.quot a b.

Definition sem_sdiv {w : nat} (a b : BitVec w) : BitVec w :=
  if Z.eqb (bv_uint b) 0 then bv_norm w 0
  else bv_norm w (trunc_div_int (bv_sint a) (bv_sint b)).

(** ** Bitwise *)

Definition bv_and {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (Z.land (bv_uint a) (bv_uint b)).

Definition bv_or {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (Z.lor (bv_uint a) (bv_uint b)).

Definition bv_xor {w : nat} (a b : BitVec w) : BitVec w :=
  bv_norm w (Z.lxor (bv_uint a) (bv_uint b)).

Definition bv_not {w : nat} (a : BitVec w) : BitVec w :=
  bv_norm w (Z.lnot (bv_uint a)).

(** ** Shifts

    [shamt] arrives as a [Z] read out of another bit vector, so it is always
    non-negative.  Both directions clamp at [w] before exponentiating. *)

Definition clamp_shift (w : nat) (sh : Z) : Z :=
  if sh <? 0 then 0 else if Z.of_nat w <=? sh then Z.of_nat w else sh.

Definition bv_shl_z {w : nat} (a : BitVec w) (sh : Z) : BitVec w :=
  bv_norm w (bv_uint a * 2 ^ (clamp_shift w sh)).

Definition bv_shl {w n : nat} (a : BitVec w) (sh : BitVec n) : BitVec w :=
  bv_shl_z a (bv_uint sh).

(** Arithmetic shift right.  [Z.div] floors, which is exactly the sign-extending
    behaviour wanted here. *)
Definition sem_sra_z {w : nat} (x : BitVec w) (sh : Z) : BitVec w :=
  bv_norm w (bv_sint x / 2 ^ (clamp_shift w sh)).

Definition sem_sra {w n : nat} (x : BitVec w) (sh : BitVec n) : BitVec w :=
  sem_sra_z x (bv_uint sh).

(** ** Reduction-or *)

Definition sem_ror_bool (bs : list bool) : BitVec 1 :=
  bv_norm 1 (if existsb (fun b => b) bs then 1 else 0).

(** ** Masks *)

Definition bit_mask_at (w : nat) (i : nat) : BitVec w :=
  bv_norm w (2 ^ (Z.of_nat i)).

Definition mask_indices {w : nat} (m : BitVec w) : list nat :=
  filter (fun i => bv_bit m i) (seq 0 w).

(** Pack the bits of [get] named by [is] into the low bits of the result, head
    of [is] landing in the highest position.  Matches the Lean [pack_low]: the
    disjunction there is equivalent to addition because the accumulated value is
    always below [2 ^ length is']. *)
Fixpoint pack_low_z (get : nat -> bool) (is : list nat) : Z :=
  match is with
  | [] => 0
  | i :: is' =>
      let packed := pack_low_z get is' in
      if get i then 2 ^ (Z.of_nat (length is')) + packed else packed
  end.

Definition sem_get_mask (b : nat) {a m : nat} (x : BitVec a) (mask : BitVec m)
  : BitVec b :=
  bv_norm b (pack_low_z (bv_bit x) (rev (mask_indices mask))).

Definition set_bit_z (z : Z) (i : nat) (b : bool) : Z :=
  if b then Z.lor z (2 ^ (Z.of_nat i)) else Z.ldiff z (2 ^ (Z.of_nat i)).

Definition sem_set_mask {a m v : nat} (acc : BitVec a) (mask : BitVec m)
                        (val : BitVec v) : BitVec a :=
  let idxs := mask_indices mask in
  bv_norm a
    (fold_left (fun z p => set_bit_z z (snd p) (bv_bit val (fst p)))
               (combine (seq 0 (length idxs)) idxs)
               (bv_uint acc)).

(** Reference-only: n-ary shift-left fold, kept for parity with the Isabelle and
    Lean preludes. *)
Definition sem_shl_many {a b : nat} (x : BitVec a) (bs : list (BitVec b))
  : BitVec a :=
  fold_left (fun acc s => bv_or (bv_shl_z x (bv_uint s)) acc) bs (bv_norm a 0).

(** ** State elements *)

Definition flop_next {w : nat} (reset_active : bool) (initial_value : BitVec w)
                     (enable_active : bool) (din current : BitVec w) : BitVec w :=
  if reset_active then initial_value
  else if enable_active then din
  else current.

(** ** Function-valued memories

    Deliberately NOT scalarised into one flop per bit: a large SRAM stays a
    function from address to word. *)

Definition mem (addr data : nat) : Type := BitVec addr -> BitVec data.

Definition mem_read {addr data : nat} (m : mem addr data) (a : BitVec addr)
  : BitVec data := m a.

Definition mem_write {addr data : nat} (m : mem addr data) (a : BitVec addr)
                     (v : BitVec data) : mem addr data :=
  fun x => if bv_eqb x a then v else m x.

(** ** Byte enables *)

Definition byte_mask_word (byte_w byte_idx : nat) (d : nat) : BitVec d :=
  if Nat.leb d (Nat.mul byte_idx byte_w) then bv_norm d 0
  else bv_norm d ((2 ^ (Z.of_nat byte_w) - 1) * 2 ^ (Z.of_nat (Nat.mul byte_idx byte_w))).

Definition byte_enable_mask (d : nat) {be : nat} (bev : BitVec be) (byte_w : nat)
  : BitVec d :=
  fold_left
    (fun acc byte_idx =>
       if bv_bit bev byte_idx then bv_or acc (byte_mask_word byte_w byte_idx d)
       else acc)
    (seq 0 be)
    (bv_norm d 0).

Definition masked_word_update {d be : nat} (old new : BitVec d)
                              (bev : BitVec be) (byte_w : nat) : BitVec d :=
  let m := byte_enable_mask d bev byte_w in
  bv_or (bv_and old (bv_not m)) (bv_and new m).

Definition mem_write_be {addr data be : nat} (m : mem addr data) (a : BitVec addr)
                        (v : BitVec data) (bev : BitVec be) (byte_w : nat)
  : mem addr data :=
  mem_write m a (masked_word_update (mem_read m a) v bev byte_w).

(** ** SRAM port policies

    Each returns [(next memory, read data)]. *)

Definition sram_1rw_read_first {addr data : nat} (we : bool) (a : BitVec addr)
    (wdata : BitVec data) (m : mem addr data) : mem addr data * BitVec data :=
  let rdata := mem_read m a in
  let m' := if we then mem_write m a wdata else m in
  (m', rdata).

Definition sram_1rw_write_first {addr data : nat} (we : bool) (a : BitVec addr)
    (wdata : BitVec data) (m : mem addr data) : mem addr data * BitVec data :=
  let m' := if we then mem_write m a wdata else m in
  (m', mem_read m' a).

Definition sram_1rw_be_read_first {addr data be : nat} (we : bool) (a : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : nat) (m : mem addr data)
  : mem addr data * BitVec data :=
  let rdata := mem_read m a in
  let m' := if we then mem_write_be m a wdata bev byte_w else m in
  (m', rdata).

Definition sram_1rw_be_write_first {addr data be : nat} (we : bool) (a : BitVec addr)
    (wdata : BitVec data) (bev : BitVec be) (byte_w : nat) (m : mem addr data)
  : mem addr data * BitVec data :=
  let m' := if we then mem_write_be m a wdata bev byte_w else m in
  (m', mem_read m' a).

Definition sram_1r1w_read_first {addr data : nat} (we : bool) (waddr : BitVec addr)
    (wdata : BitVec data) (raddr : BitVec addr) (m : mem addr data)
  : mem addr data * BitVec data :=
  let rdata := mem_read m raddr in
  let m' := if we then mem_write m waddr wdata else m in
  (m', rdata).

Definition sram_1r1w_write_first {addr data : nat} (we : bool) (waddr : BitVec addr)
    (wdata : BitVec data) (raddr : BitVec addr) (m : mem addr data)
  : mem addr data * BitVec data :=
  let m' := if we then mem_write m waddr wdata else m in
  (m', mem_read m' raddr).

Definition sram_1r1w_be_read_first {addr data be : nat} (we : bool)
    (waddr : BitVec addr) (wdata : BitVec data) (bev : BitVec be) (byte_w : nat)
    (raddr : BitVec addr) (m : mem addr data) : mem addr data * BitVec data :=
  let rdata := mem_read m raddr in
  let m' := if we then mem_write_be m waddr wdata bev byte_w else m in
  (m', rdata).

Definition sram_1r1w_be_write_first {addr data be : nat} (we : bool)
    (waddr : BitVec addr) (wdata : BitVec data) (bev : BitVec be) (byte_w : nat)
    (raddr : BitVec addr) (m : mem addr data) : mem addr data * BitVec data :=
  let m' := if we then mem_write_be m waddr wdata bev byte_w else m in
  (m', mem_read m' raddr).

Definition sram_sync_read_reg_next {d : nat} (ren : bool)
                                   (read_value current : BitVec d) : BitVec d :=
  if ren then read_value else current.

(** ** Representation lemmas

    These are what make the "always normalised" claim usable in later proofs.
    They are also the smoke test that the definitions above reduce. *)

Lemma two_pow_pos : forall w, 0 < two_pow w.
Proof.
  intros w. unfold two_pow. apply Z.pow_pos_nonneg; [lia | apply Nat2Z.is_nonneg].
Qed.

Lemma bv_uint_range : forall (w : nat) (x : BitVec w),
  0 <= bv_uint x < two_pow w.
Proof.
  intros w x. unfold bv_uint. apply Z.mod_pos_bound. apply two_pow_pos.
Qed.

Lemma bv_uint_norm : forall (w : nat) (z : Z),
  bv_uint (bv_norm w z) = z mod two_pow w.
Proof.
  intros w z. unfold bv_uint, bv_norm. simpl.
  rewrite Z.mod_mod by (generalize (two_pow_pos w); lia). reflexivity.
Qed.

Lemma bv_norm_uint : forall (w : nat) (x : BitVec w),
  bv_norm w (bv_uint x) = mkBitVec (bv_uint x).
Proof.
  intros w x. unfold bv_norm. f_equal.
  apply Z.mod_small. apply bv_uint_range.
Qed.
