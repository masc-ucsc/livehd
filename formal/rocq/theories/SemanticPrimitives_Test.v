(** * SemanticPrimitives_Test — computational regression for the Rocq prelude.

    Rocq counterpart of [formal/semantic_primitives/SemanticPrimitives_Test.thy].

    Every check below is closed by [vm_compute; reflexivity], which is exactly
    the reduction path the generated certificate proofs take, so this file
    doubles as a smoke test that nothing on the evaluation path gets stuck.

    The corner cases are the ones [pass/isabelle/README.md] requires before a
    new construct is trusted: mux polarity, mask packing, non-contiguous
    Get_mask/Set_mask, arithmetic-shift sign preservation, signed vs unsigned
    compare and divide, reset priority, enable behaviour, and constants wider
    than 64 bits.  The last group checks that the fast model and the
    certificate agree — the bug class that dominates
    [pass/isabelle/BRIDGE_BUGS.md] and [pass/lean/STEP5_BRIDGE_BUGS.md]. *)

From Stdlib Require Import ZArith NArith List Bool.
From RocqSemanticPrimitives Require Import SemanticPrimitives.
From RocqSemanticPrimitives.Translation Require Import LGraphModel.

Import ListNotations.
Local Open Scope Z_scope.

Ltac check := vm_compute; reflexivity.

(** ** Normalisation and signedness *)

Example bv_add_wraps : bv_uint (bv_add (bv_norm 4 12) (bv_norm 4 7)) = 3.
Proof. check. Qed.

Example bv_sub_wraps : bv_uint (bv_sub (bv_norm 4 3) (bv_norm 4 7)) = 12.
Proof. check. Qed.

Example bv_sint_negative : bv_sint (bv_norm 4 12) = -4.
Proof. check. Qed.

Example bv_sint_positive : bv_sint (bv_norm 4 7) = 7.
Proof. check. Qed.

Example bv_sint_width1 : bv_sint (bv_norm 1 1) = -1.
Proof. check. Qed.

(** Width-0 is rejected by the pass, but the primitives must still be total. *)
Example bv_uint_width0_total : bv_uint (bv_norm 0 12345) = 0.
Proof. check. Qed.

(** ** Constants wider than 64 bits *)

Example wide_const : bv_uint (bv_norm 128 (2 ^ 100 + 1)) = 2 ^ 100 + 1.
Proof. check. Qed.

Example wide_const_wraps : bv_uint (bv_norm 64 (2 ^ 64 + 5)) = 5.
Proof. check. Qed.

(** ** Bitwise *)

Example bv_not_4 : bv_uint (bv_not (bv_norm 4 5)) = 10.
Proof. check. Qed.

Example bv_and_4 : bv_uint (bv_and (bv_norm 4 12) (bv_norm 4 10)) = 8.
Proof. check. Qed.

Example bv_or_4 : bv_uint (bv_or (bv_norm 4 12) (bv_norm 4 10)) = 14.
Proof. check. Qed.

Example bv_xor_4 : bv_uint (bv_xor (bv_norm 4 12) (bv_norm 4 10)) = 6.
Proof. check. Qed.

(** ** Shifts, including the saturation guards *)

Example shl_basic : bv_uint (bv_shl_z (bv_norm 8 3) 2) = 12.
Proof. check. Qed.

Example shl_out_of_range : bv_uint (bv_shl_z (bv_norm 8 3) 9) = 0.
Proof. check. Qed.

(** A 2^31 shift amount must not try to build a two-billion-bit integer. *)
Example shl_huge_amount_terminates : bv_uint (bv_shl_z (bv_norm 8 255) 2147483648) = 0.
Proof. check. Qed.

(** Arithmetic shift right preserves the sign bit (floor division). *)
Example sra_negative : bv_uint (sem_sra_z (bv_norm 4 12) 1) = 14.
Proof. check. Qed.

Example sra_negative_saturates : bv_uint (sem_sra_z (bv_norm 4 12) 9) = 15.
Proof. check. Qed.

Example sra_positive : bv_uint (sem_sra_z (bv_norm 4 6) 1) = 3.
Proof. check. Qed.

Example sra_huge_amount_terminates : bv_uint (sem_sra_z (bv_norm 8 200) 2147483648) = 255.
Proof. check. Qed.

(** ** Division: signed vs unsigned, and totality at zero *)

Example udiv_basic : bv_uint (sem_udiv (bv_norm 8 200) (bv_norm 8 3)) = 66.
Proof. check. Qed.

Example udiv_by_zero_is_zero : bv_uint (sem_udiv (bv_norm 8 200) (bv_norm 8 0)) = 0.
Proof. check. Qed.

(** -7 / 2 truncates toward zero to -3, i.e. 13 in 4 bits. *)
Example sdiv_truncates_toward_zero : bv_uint (sem_sdiv (bv_norm 4 9) (bv_norm 4 2)) = 13.
Proof. check. Qed.

(** The same operands read unsigned give 9 / 2 = 4, a genuinely different
    answer — the signed/unsigned split must survive the whole flow. *)
Example udiv_differs_from_sdiv : bv_uint (sem_udiv (bv_norm 4 9) (bv_norm 4 2)) = 4.
Proof. check. Qed.

Example sdiv_by_zero_is_zero : bv_uint (sem_sdiv (bv_norm 4 9) (bv_norm 4 0)) = 0.
Proof. check. Qed.

(** ** Width changes *)

Example zext_pads_zero : bv_uint (bv_zext 8 (bv_norm 4 12)) = 12.
Proof. check. Qed.

Example sext_extends_sign : bv_uint (bv_sext 8 (bv_norm 4 12)) = 252.
Proof. check. Qed.

(** ** Masks: non-contiguous Get_mask / Set_mask *)

(** mask 0b1010 selects bits 3 and 1 of 0b1100 -> 0b10 *)
Example get_mask_noncontiguous :
  bv_uint (sem_get_mask 2 (bv_norm 4 12) (bv_norm 4 10)) = 2.
Proof. check. Qed.

(** The all-ones mask is the zero-extension idiom: it must select every source
    bit, not just bit 0.  This exact shape was a real shared bug across
    pass.isabelle and pass.lean. *)
Example get_mask_all_ones_is_identity :
  bv_uint (sem_get_mask 4 (bv_norm 4 11) (bv_norm 4 15)) = 11.
Proof. check. Qed.

(** The mask is materialised at max(src_w, out_w), so a wider all-ones mask
    still selects the whole source. *)
Example get_mask_all_ones_wide_mask :
  bv_uint (sem_get_mask 8 (bv_norm 4 11) (bv_norm 8 255)) = 11.
Proof. check. Qed.

(** mask 0b1010 writes val bit 0 into position 1 and val bit 1 into position 3 *)
Example set_mask_noncontiguous :
  bv_uint (sem_set_mask (bv_norm 4 0) (bv_norm 4 10) (bv_norm 2 3)) = 10.
Proof. check. Qed.

Example set_mask_clears_when_val_bit_zero :
  bv_uint (sem_set_mask (bv_norm 4 15) (bv_norm 4 10) (bv_norm 2 0)) = 5.
Proof. check. Qed.

(** ** Reduction-or *)

Example ror_all_zero : bv_uint (sem_ror_bool [false; false]) = 0.
Proof. check. Qed.

Example ror_any_one : bv_uint (sem_ror_bool [false; true]) = 1.
Proof. check. Qed.

(** ** Flops: reset priority and enable behaviour *)

Example flop_reset_beats_enable :
  bv_uint (flop_next true (bv_norm 4 0) true (bv_norm 4 9) (bv_norm 4 5)) = 0.
Proof. check. Qed.

Example flop_enabled_takes_din :
  bv_uint (flop_next false (bv_norm 4 0) true (bv_norm 4 9) (bv_norm 4 5)) = 9.
Proof. check. Qed.

Example flop_disabled_holds :
  bv_uint (flop_next false (bv_norm 4 0) false (bv_norm 4 9) (bv_norm 4 5)) = 5.
Proof. check. Qed.

(** ** Memories *)

Definition zero_mem : mem 4 8 := fun _ => bv_norm 8 0.

Example mem_write_then_read :
  bv_uint (mem_read (mem_write zero_mem (bv_norm 4 3) (bv_norm 8 200)) (bv_norm 4 3)) = 200.
Proof. check. Qed.

Example mem_write_leaves_other_addresses :
  bv_uint (mem_read (mem_write zero_mem (bv_norm 4 3) (bv_norm 8 200)) (bv_norm 4 4)) = 0.
Proof. check. Qed.

(** Byte enable 0b01 with byte_w = 4 updates only the low nibble. *)
Example masked_word_update_be :
  bv_uint (masked_word_update (bv_norm 8 255) (bv_norm 8 0) (bv_norm 2 1) 4) = 240.
Proof. check. Qed.

(** Read-first returns the OLD value at the written address; write-first the new. *)
Example sram_read_first_sees_old :
  bv_uint (snd (sram_1rw_read_first true (bv_norm 4 3) (bv_norm 8 200) zero_mem)) = 0.
Proof. check. Qed.

Example sram_write_first_sees_new :
  bv_uint (snd (sram_1rw_write_first true (bv_norm 4 3) (bv_norm 8 200) zero_mem)) = 200.
Proof. check. Qed.

(** ** Certificate operators *)

Example cert_mux_polarity_true :
  bvc_uint (denote_op Op_MuxBool 4 [mk_bv 1 1; mk_bv 4 5; mk_bv 4 9]) = 9.
Proof. check. Qed.

Example cert_mux_polarity_false :
  bvc_uint (denote_op Op_MuxBool 4 [mk_bv 1 0; mk_bv 4 5; mk_bv 4 9]) = 5.
Proof. check. Qed.

Example cert_muxn_selects_third :
  bvc_uint (denote_op Op_MuxN 4 [mk_bv 2 2; mk_bv 4 1; mk_bv 4 2; mk_bv 4 3]) = 3.
Proof. check. Qed.

(** Out-of-range n-way select is totalised to 0, not stuck. *)
Example cert_muxn_out_of_range :
  bvc_uint (denote_op Op_MuxN 4 [mk_bv 4 9; mk_bv 4 1; mk_bv 4 2]) = 0.
Proof. check. Qed.

Example cert_sum_adds_then_subtracts :
  bvc_uint (denote_op (Op_Sum 2) 4 [mk_bv 4 7; mk_bv 4 6; mk_bv 4 3]) = 10.
Proof. check. Qed.

(** Signed and unsigned compare must give different answers on the same bits. *)
Example cert_ult_unsigned :
  bvc_uint (denote_op Op_ULT 1 [mk_bv 4 12; mk_bv 4 3]) = 0.
Proof. check. Qed.

Example cert_slt_signed :
  bvc_uint (denote_op Op_SLT 1 [mk_bv 4 12; mk_bv 4 3]) = 1.
Proof. check. Qed.

Example cert_sext :
  bvc_uint (denote_op Op_Sext 8 [mk_bv 8 11; mk_bv 8 4]) = 251.
Proof. check. Qed.

(** When the sign position is at or above the output width, both branches of the
    general formula agree with a plain truncation -- which is why [denote_op]
    short-circuits there instead of computing 2^n.  Locking the equality in:
    sign-extending 4-bit 0b1011 into 4 bits is the identity. *)
Example cert_sext_at_output_width :
  bvc_uint (denote_op Op_Sext 4 [mk_bv 8 11; mk_bv 8 4]) = 11.
Proof. check. Qed.

(** ...and that short-circuit is what makes a nonsense amount TERMINATE rather
    than try to build a two-billion-bit integer. *)
Example cert_sext_huge_amount_terminates :
  bvc_uint (denote_op Op_Sext 8 [mk_bv 8 11; mk_bv 64 2147483648]) = 11.
Proof. check. Qed.

(** Same hazard on the n-way mux selector: the index is range-checked in Z, so
    Z.to_nat is never applied to a 32-bit-wide selector value. *)
Example cert_muxn_huge_selector_terminates :
  bvc_uint (denote_op Op_MuxN 4 [mk_bv 64 4000000000; mk_bv 4 1; mk_bv 4 2]) = 0.
Proof. check. Qed.

Example cert_getmask_all_ones :
  bvc_uint (denote_op Op_GetMask 4 [mk_bv 4 11; mk_bv 4 15]) = 11.
Proof. check. Qed.

Example cert_shl_huge_amount_terminates :
  bvc_uint (denote_op Op_SHL 8 [mk_bv 8 255; mk_bv 64 2147483648]) = 0.
Proof. check. Qed.

(** ** Fast model and certificate agree

    These are the pairs the bridge theorems will eventually have to prove in
    general.  Checking them concretely first is cheap and catches the dominant
    bug class (the two emitters disagreeing on an operand width) early. *)

Example agree_sra_negative :
  bvc_uint (denote_op Op_SRA 4 [mk_bv 4 12; mk_bv 4 1])
  = bv_uint (sem_sra_z (bv_norm 4 12) 1).
Proof. check. Qed.

Example agree_sdiv :
  bvc_uint (denote_op Op_SDiv 4 [mk_bv 4 9; mk_bv 4 2])
  = bv_uint (sem_sdiv (bv_norm 4 9) (bv_norm 4 2)).
Proof. check. Qed.

Example agree_getmask :
  bvc_uint (denote_op Op_GetMask 2 [mk_bv 4 12; mk_bv 4 10])
  = bv_uint (sem_get_mask 2 (bv_norm 4 12) (bv_norm 4 10)).
Proof. check. Qed.

Example agree_setmask :
  bvc_uint (denote_op Op_SetMask 4 [mk_bv 4 0; mk_bv 4 10; mk_bv 2 3])
  = bv_uint (sem_set_mask (bv_norm 4 0) (bv_norm 4 10) (bv_norm 2 3)).
Proof. check. Qed.

Example agree_not :
  bvc_uint (denote_op Op_Not 4 [mk_bv 4 5]) = bv_uint (bv_not (bv_norm 4 5)).
Proof. check. Qed.

(** ** The generic evaluator on a two-source graph *)

(** NOTE for the emitter: [Z_scope] is open here, and a list literal in a record
    field does NOT get the element scope pushed into it from the field type.
    Every emitted id list must therefore carry an explicit [%N]. *)
Definition test_certs : list NodeCert :=
  [ {| nc_nid := 1; nc_op := Op_Sum 2; nc_width := 4; nc_deps := [100; 101]%N |}
  ; {| nc_nid := 2; nc_op := Op_Not;   nc_width := 4; nc_deps := [1]%N |} ].

Definition test_graph : GraphCert :=
  {| gc_topo := [1; 2]%N; gc_sources := [100; 101]%N;
     gc_nodes := nodes_of_list test_certs |}.

Definition test_env (n : N) : BV :=
  if N.eqb n 100 then mk_bv 4 5
  else if N.eqb n 101 then mk_bv 4 6
  else mk_bv 1 0.

Example eval_graph_sum : bvc_uint (evalGraph (gc_topo test_graph) test_graph test_env 1) = 11.
Proof. check. Qed.

Example eval_graph_not : bvc_uint (evalGraph (gc_topo test_graph) test_graph test_env 2) = 4.
Proof. check. Qed.

(** Sources are passed through untouched. *)
Example eval_graph_source : bvc_uint (evalGraph (gc_topo test_graph) test_graph test_env 100) = 5.
Proof. check. Qed.

(** The certificate is well formed by computation — this is what
    [--set formal.rocq.cert_wf=eval] emits per design. *)
Example test_graph_wf : graphCertWfBool test_certs (gc_sources test_graph) = true.
Proof. check. Qed.

(** And the keystone specialises to this graph without any new proof. *)
Example test_graph_eval_correct :
  envCorrectOn (gc_topo test_graph)
    (evalGraph (gc_topo test_graph) test_graph test_env)
    (graphDenotation (gc_topo test_graph) test_graph test_env).
Proof. apply evalGraphCorrectForCert. Qed.
