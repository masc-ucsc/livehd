theory Translation_Op_Lemmas
  imports
    Translation_LGraph_Model
    "DINO_Semantic_Primitives_Test.SemanticPrimitives"
begin

text \<open>
  Reusable local correctness/congruence lemmas for emitted node expressions.
  Generated per-node lemmas should reduce to these facts plus simplification of
  the design-specific denotation definitions.
\<close>

lemma local_const_correct:
  assumes "x = y"
  shows "x = y"
  using assms .

lemma local_sum_correct:
  assumes "a = a'" "b = b'"
  shows "((a :: 'w::len word) + b) = (a' + b')"
  using assms by simp

lemma local_sub_correct:
  assumes "a = a'" "b = b'"
  shows "((a :: 'w::len word) - b) = (a' - b')"
  using assms by simp

lemma local_mult_correct:
  assumes "a = a'" "b = b'"
  shows "((a :: 'w::len word) * b) = (a' * b')"
  using assms by simp

lemma local_udiv_correct:
  assumes "a = a'" "b = b'"
  shows "sem_udiv (a :: 'w::len word) b = sem_udiv a' b'"
  using assms by simp

lemma local_and_correct:
  assumes "a = a'" "b = b'"
  shows "Bit_Operations.and (a :: 'w::len word) b =
         Bit_Operations.and a' b'"
  using assms by simp

lemma local_or_correct:
  assumes "a = a'" "b = b'"
  shows "Bit_Operations.or (a :: 'w::len word) b =
         Bit_Operations.or a' b'"
  using assms by simp

lemma local_xor_correct:
  assumes "a = a'" "b = b'"
  shows "Bit_Operations.xor (a :: 'w::len word) b =
         Bit_Operations.xor a' b'"
  using assms by simp

lemma local_not_correct:
  assumes "a = a'"
  shows "Bit_Operations.not (a :: 'w::len word) =
         Bit_Operations.not a'"
  using assms by simp

lemma local_ror_correct:
  assumes "xs = ys"
  shows "sem_ror_bool xs = sem_ror_bool ys"
  using assms by simp

lemma local_lt_correct:
  assumes "a = a'" "b = b'"
  shows "((if (a :: 'w::len word) < b then (1 :: 1 word) else 0) =
         (if a' < b' then (1 :: 1 word) else 0))"
  using assms by simp

lemma local_gt_correct:
  assumes "a = a'" "b = b'"
  shows "((if (a :: 'w::len word) > b then (1 :: 1 word) else 0) =
         (if a' > b' then (1 :: 1 word) else 0))"
  using assms by simp

lemma local_eq_correct:
  assumes "a = a'" "b = b'"
  shows "((if (a :: 'w::len word) = b then (1 :: 1 word) else 0) =
         (if a' = b' then (1 :: 1 word) else 0))"
  using assms by simp

lemma local_shl_correct:
  assumes "a = a'" "b = b'"
  shows "push_bit (unat (b :: 'n::len word)) (a :: 'w::len word) =
         push_bit (unat b') a'"
  using assms by simp

lemma local_sra_correct:
  assumes "a = a'" "b = b'"
  shows "sem_sra (a :: 'w::len word) (b :: 'n::len word) =
         sem_sra a' b'"
  using assms by simp

lemma local_mux_bool_correct:
  assumes "sel = sel'" "a = a'" "b = b'"
  shows "((if (sel :: 1 word) \<noteq> 0 then (a :: 'w::len word) else b) =
         (if sel' \<noteq> 0 then a' else b'))"
  using assms by simp

lemma local_sext_correct:
  assumes "a = a'" "amount = amount'"
  shows "((word_of_int (signed_take_bit (unat (amount :: 'n::len word))
            (uint (a :: 'w::len word)))) :: 'o::len word) =
         ((word_of_int (signed_take_bit (unat amount') (uint a'))) :: 'o word)"
  using assms by simp

lemma local_get_mask_correct:
  assumes "x = x'" "m = m'"
  shows "sem_get_mask (x :: 'x::len word) (m :: 'm::len word) =
         (sem_get_mask x' m' :: 'o::len word)"
  using assms by simp

lemma local_set_mask_correct:
  assumes "a = a'" "m = m'" "v = v'"
  shows "sem_set_mask (a :: 'a::len word) (m :: 'm::len word) (v :: 'v::len word) =
         sem_set_mask a' m' v'"
  using assms by simp

lemma source_input_correct:
  assumes "x = y"
  shows "x = y"
  using assms .

lemma source_flop_correct:
  assumes "x = y"
  shows "x = y"
  using assms .

end
