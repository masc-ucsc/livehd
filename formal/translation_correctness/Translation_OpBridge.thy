theory Translation_OpBridge
  imports
    Translation_LGraph_Model
    "DINO_Semantic_Primitives_Test.SemanticPrimitives"
begin

text \<open>
  Piece A of the certificate-equivalence bridge: per-operator lemmas relating the
  certificate evaluator @{const eval_op} (untyped @{typ bv}) to the fast model's
  native word operations (@{typ "'w::len word"}).

  The encoding is \<open>bvenc\<close> below.  Isabelle's word widths are types, so
  type-class polymorphism over \<^class>\<open>len\<close> gives the same uniformity
  Lean gets from a term-indexed \<open>BitVec w\<close>: one lemma per operator
  covers every width.

  Two facts about the emitter shape drive everything here:

  \<^item> A certificate dependency carries the \emph{driver's} width, not the
    consumer's --- \<open>cert_dep_id\<close> only applies the consumer width to
    \emph{constants}, which become synthetic \<open>Op_Const\<close> nodes at
    exactly that width.  Every other dep is a node/source id whose certificate
    width is its own.  The fast model meanwhile \<open>ucast\<close>s each operand
    to the width it wants.  So the bridge lemmas must be width-polymorphic, with
    an explicit resize --- they cannot assume operands already share a type.

  \<^item> That resize is exactly @{const bv_resize}: see
    \<open>bvenc_ucast\<close> below.  \<open>ucast\<close> on the fast side and
    @{const bv_resize} on the certificate side are the same operation under the
    encoding, which is what lets a per-node proof discharge by rewriting.
\<close>

section \<open>The encoding\<close>

definition bvenc :: "'w::len word \<Rightarrow> bv" where
  "bvenc (x :: 'w::len word) = BV LENGTH('w) (uint x)"

lemma bv_width_bvenc [simp]:
  "bv_width (bvenc (x :: 'w::len word)) = LENGTH('w)"
  by (simp add: bvenc_def)

lemma bv_uint_bvenc [simp]:
  "bv_uint (bvenc (x :: 'w::len word)) = uint x"
  by (simp add: bvenc_def uint_lt2p)

lemma bvenc_inject:
  "bvenc (x :: 'w::len word) = bvenc (y :: 'w word) \<longleftrightarrow> x = y"
proof
  assume "bvenc x = bvenc y"
  then have "uint x = uint y" by (simp add: bvenc_def)
  then have "word_of_int (uint x) = (word_of_int (uint y) :: 'w word)" by simp
  then show "x = y" by (simp add: word_of_int_uint)
qed simp

text \<open>
  The two normalization lemmas the per-node proofs live on.
\<close>

lemma bvenc_word_of_int:
  "bvenc (word_of_int v :: 'w::len word) = mk_bv LENGTH('w) v"
  by (simp add: bvenc_def mk_bv_def uint_word_of_int)

text \<open>
  \<^bold>\<open>Do not add \<open>ucast_eq\<close> to a simp set.\<close>
  \<open>ucast\<close>, \<open>uint\<close> and \<open>unat\<close> are all
  abbreviations for the same constant \<open>unsigned\<close>, differing only in
  the result type.  So \<open>ucast_eq : ucast ?w = word_of_int (uint ?w)\<close>
  is really \<open>unsigned ?w = word_of_int (unsigned ?w)\<close> --- a rule
  whose right-hand side re-introduces the head constant its left-hand side
  matches, with the result type schematic.  Measured: adding it to a simp set
  does not terminate (killed at 400 s), while plain \<open>simp\<close> proves
  the same goal, and \<open>rule ucast_eq\<close> closes it instantly.

  Use \<open>unsigned_ucast_eq\<close> instead.  It states the composite
  directly --- \<open>unsigned (ucast ?w) = take_bit LENGTH(?'c) (unsigned ?w)\<close>
  --- so its right-hand side does not re-introduce a \<open>ucast\<close> under
  an \<open>unsigned\<close>.  Note \<open>find_theorems\<close> on the pattern
  \<open>uint (ucast _)\<close> reports \<^emph>\<open>zero\<close> matches for
  exactly the same reason the loop exists: both are \<open>unsigned\<close>, so
  the pattern does not say what it looks like it says.  Search by name.
\<close>

lemma bvenc_ucast:
  "bvenc (ucast (x :: 'a::len word) :: 'b::len word) = bv_resize LENGTH('b) (bvenc x)"
proof -
  have "bvenc (ucast x :: 'b word) = BV LENGTH('b) (take_bit LENGTH('b) (uint x))"
    by (simp add: bvenc_def unsigned_ucast_eq)
  also have "\<dots> = mk_bv LENGTH('b) (uint x)"
    by (simp add: mk_bv_def take_bit_eq_mod)
  also have "\<dots> = bv_resize LENGTH('b) (bvenc x)"
    by (simp add: bv_resize_def)
  finally show ?thesis .
qed

lemma mk_bv_uint [simp]:
  "mk_bv LENGTH('w) (uint (x :: 'w::len word)) = bvenc x"
  by (simp add: bvenc_def mk_bv_def uint_lt2p)

section \<open>Bit-level correspondence\<close>

text \<open>
  @{const bv_bit} on an encoded word is just the word's own bit.  Bits at or
  above the width are @{const False} on both sides, so no side condition is
  needed.
\<close>

lemma bv_bit_bvenc [simp]:
  "bv_bit (bvenc (x :: 'w::len word)) i = bit x i"
proof -
  have u: "nat (uint x) = unat x" by (simp add: unsigned_def)
  have "bv_bit (bvenc x) i = bit (unat x) i" by (simp add: bv_bit_def u)
  also have "\<dots> = bit x i" by (simp add: bit_unsigned_iff)
  finally show ?thesis .
qed

text \<open>
  Packing a word's own bits back up recovers its @{const uint}.  This is the
  gateway lemma for every bitwise operator: it turns a
  @{const pack_bits}-shaped certificate value into an encoded word.
\<close>

lemma pack_bits_take_bit:
  fixes m :: int
  shows "pack_bits w (bit m) = take_bit w m"
proof (induct w)
  case 0
  show ?case by (simp add: pack_bits_def)
next
  case (Suc w)
  have "pack_bits (Suc w) (bit m)
          = pack_bits w (bit m) + (if bit m w then 2 ^ w else 0)"
    by (simp add: pack_bits_def)
  also have "\<dots> = take_bit w m + (if bit m w then 2 ^ w else 0)"
    using Suc by simp
  also have "\<dots> = take_bit (Suc w) m"
    by (simp add: take_bit_Suc_from_most)
  finally show ?case .
qed

lemma pack_bits_cong:
  assumes "\<And>i. i < w \<Longrightarrow> P i = Q i"
  shows "pack_bits w P = pack_bits w Q"
  unfolding pack_bits_def
  by (rule arg_cong[where f = sum_list], rule map_cong) (auto simp: assms)

text \<open>
  The workhorse: if a predicate agrees with a word's bits below the width, then
  the certificate's @{const mk_bv}/@{const pack_bits} value \emph{is} that word,
  encoded.
\<close>

lemma mk_bv_pack_bits_eq_bvenc:
  fixes z :: "'w::len word"
  assumes "\<And>i. i < LENGTH('w) \<Longrightarrow> P i = bit z i"
  shows "mk_bv LENGTH('w) (pack_bits LENGTH('w) P) = bvenc z"
proof -
  have "pack_bits LENGTH('w) P = pack_bits LENGTH('w) (bit (uint z))"
    by (rule pack_bits_cong) (simp add: assms bit_uint_iff)
  also have "\<dots> = take_bit LENGTH('w) (uint z)"
    by (rule pack_bits_take_bit)
  also have "\<dots> = uint z"
    by (simp add: take_bit_eq_mod uint_lt2p)
  finally show ?thesis by (simp add: bvenc_def mk_bv_def uint_lt2p)
qed

section \<open>Bitwise operators\<close>

text \<open>
  @{const bv_bitwise} reads bits of both operands below the output width and
  treats everything above as @{const False}, which is exactly what the fast
  model's \<open>ucast\<close> to the output width does.  So the lemmas below need
  no width side conditions at all.
\<close>

text \<open>
  Concrete instances.  Each is stated in the shape the emitter produces: the
  certificate side has the dependencies at their own widths, the fast side
  \<open>ucast\<close>s them to the node width.
\<close>

lemma and2_bridge:
  fixes a :: "'a::len word" and b :: "'b::len word"
  shows "bv_bitwise LENGTH('w) (\<and>) (bvenc a) (bvenc b)
           = bvenc (Bit_Operations.and (ucast a :: 'w::len word) (ucast b))"
  by (simp add: bv_bitwise_def
      , rule mk_bv_pack_bits_eq_bvenc
      , simp add: bit_and_iff bit_ucast_iff)

lemma or2_bridge:
  fixes a :: "'a::len word" and b :: "'b::len word"
  shows "bv_bitwise LENGTH('w) (\<or>) (bvenc a) (bvenc b)
           = bvenc (Bit_Operations.or (ucast a :: 'w::len word) (ucast b))"
  by (simp add: bv_bitwise_def
      , rule mk_bv_pack_bits_eq_bvenc
      , simp add: bit_or_iff bit_ucast_iff)

lemma xor2_bridge:
  fixes a :: "'a::len word" and b :: "'b::len word"
  shows "bv_bitwise LENGTH('w) (\<lambda>x y. x \<noteq> y) (bvenc a) (bvenc b)
           = bvenc (Bit_Operations.xor (ucast a :: 'w::len word) (ucast b))"
  by (simp add: bv_bitwise_def
      , rule mk_bv_pack_bits_eq_bvenc
      , simp add: bit_xor_iff bit_ucast_iff)

lemma not_bridge:
  fixes a :: "'a::len word"
  shows "bv_not LENGTH('w) (bvenc a)
           = bvenc (Bit_Operations.not (ucast a :: 'w::len word))"
  by (simp add: bv_not_def
      , rule mk_bv_pack_bits_eq_bvenc
      , simp add: bit_not_iff bit_ucast_iff)

section \<open>Constants\<close>

text \<open>
  A synthesized \<open>Op_Const\<close> node is materialized at the consumer's
  width, and the fast model spells the same value as a word literal at that same
  width.  Both sides therefore reduce to @{const mk_bv} of the integer.
\<close>

lemma const_bridge:
  "eval_op (Op_Const c) LENGTH('w) [] = bvenc (word_of_int c :: 'w::len word)"
  by (simp add: bvenc_word_of_int)

section \<open>Word helpers\<close>

lemma uint_eq_iff_word:
  "(uint (x :: 'w::len word) = uint y) = (x = y)"
proof
  assume "uint x = uint y"
  then have "word_of_int (uint x) = (word_of_int (uint y) :: 'w word)" by simp
  then show "x = y" by (simp add: word_of_int_uint)
qed simp

lemma bv_nonzero_bvenc [simp]:
  "bv_nonzero (bvenc (x :: 'w::len word)) \<longleftrightarrow> x \<noteq> 0"
  by (simp add: bv_nonzero_def uint_0_iff)

lemma mk_bv_bool_bridge:
  "mk_bv LENGTH('w) (if P then 1 else 0) = bvenc (if P then (1::'w::len word) else 0)"
  by (simp add: mk_bv_def bvenc_def)

lemma ror2_bridge:
  fixes a :: "'a::len word" and b :: "'b::len word"
  shows "eval_op Op_Ror LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if a \<noteq> 0 \<or> b \<noteq> 0 then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge)

lemma muxbool_bridge:
  fixes s :: "'s::len word" and f :: "'f::len word" and t :: "'t::len word"
  shows "eval_op Op_MuxBool LENGTH('w) [bvenc s, bvenc f, bvenc t]
           = bvenc (if s \<noteq> 0 then (ucast t :: 'w::len word) else ucast f)"
  by (simp add: bvenc_ucast)

lemma ult_bridge:
  fixes a :: "'a::len word" and b :: "'a::len word"
  shows "eval_op Op_ULT LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if a < b then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge word_less_def)

lemma ugt_bridge:
  fixes a :: "'a::len word" and b :: "'a::len word"
  shows "eval_op Op_UGT LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if b < a then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge word_less_def)

lemma eq2_bridge:
  fixes a :: "'a::len word" and b :: "'a::len word"
  shows "eval_op Op_EQ LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if b = a then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge uint_eq_iff_word)

lemma sum2_bridge:
  fixes a :: "'w::len word" and b :: "'w::len word"
  shows "eval_op (Op_Sum 2) LENGTH('w) [bvenc a, bvenc b] = bvenc (a + b)"
  by (simp add: bvenc_def mk_bv_def uint_word_ariths)

lemma sub_bridge:
  fixes a :: "'w::len word" and b :: "'w::len word"
  shows "eval_op (Op_Sum 1) LENGTH('w) [bvenc a, bvenc b] = bvenc (a - b)"
  by (simp add: bvenc_def mk_bv_def uint_word_ariths)

lemma bv_sint_bvenc:
  "bv_sint (bvenc (x :: 'w::len word)) = sint x"
proof -
  obtain n where wn: "LENGTH('w) = Suc n"
    using len_gt_0[where 'a='w] by (cases "LENGTH('w)") auto
  let ?u = "uint x"
  have u0: "0 \<le> ?u" by simp
  have uw: "?u < 2 ^ Suc n" using uint_lt2p[of x] wn by simp
  have sx: "sint x = (?u + 2 ^ n) mod 2 ^ Suc n - 2 ^ n"
    by (simp add: sint_uint signed_take_bit_eq_take_bit_shift take_bit_eq_mod wn)
  show ?thesis
  proof (cases "?u < 2 ^ n")
    case True
    then have "(?u + 2 ^ n) mod 2 ^ Suc n = ?u + 2 ^ n" using u0 by simp
    then show ?thesis using sx True by (simp add: bv_sint_def wn)
  next
    case False
    have pw: "(2::int) ^ Suc n = 2 * 2 ^ n" by simp
    have pn: "(0::int) < 2 ^ n" by simp
    from False have ge: "(2::int) ^ n \<le> ?u" by simp
    have lt: "?u - 2 ^ n < 2 * 2 ^ n" using uw[unfolded pw] pn by linarith
    have sx': "sint x = (?u + 2 ^ n) mod (2 * 2 ^ n) - 2 ^ n"
      using sx unfolding pw .
    have "(?u + 2 ^ n) mod (2 * 2 ^ n) = (?u - 2 ^ n) mod (2 * 2 ^ n)"
      by (simp add: mod_eq_dvd_iff)
    also have "\<dots> = ?u - 2 ^ n" using ge lt by simp
    finally have m2: "(?u + 2 ^ n) mod (2 * 2 ^ n) = ?u - 2 ^ n" .
    show ?thesis using sx' m2 False by (simp add: bv_sint_def wn)
  qed
qed

lemma slt_bridge:
  fixes a :: "'a::len word" and b :: "'a::len word"
  shows "eval_op Op_SLT LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if sint a < sint b then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge bv_sint_bvenc)

lemma sgt_bridge:
  fixes a :: "'a::len word" and b :: "'a::len word"
  shows "eval_op Op_SGT LENGTH('w) [bvenc a, bvenc b]
           = bvenc (if sint b < sint a then (1::'w::len word) else 0)"
  by (simp add: mk_bv_bool_bridge bv_sint_bvenc)

section \<open>Sanity examples\<close>

text \<open>
  Each bridge is exercised in the shape a generated per-node proof would use it,
  so the lemma and its intended use are checked together and cannot drift.
\<close>

lemma example_and_node:
  fixes a :: "'a::len word" and b :: "'b::len word"
  shows "eval_op Op_And LENGTH('w) [bvenc a, bvenc b]
           = bvenc (Bit_Operations.and (ucast a :: 'w::len word) (ucast b))"
proof -
  have "eval_op Op_And LENGTH('w) [bvenc a, bvenc b]
          = bv_bitwise LENGTH('w) (\<and>) (bvenc b) (bv_resize LENGTH('w) (bvenc a))"
    by simp
  also have "\<dots> = bv_bitwise LENGTH('w) (\<and>) (bvenc b) (bvenc (ucast a :: 'w word))"
    by (simp add: bvenc_ucast)
  also have "\<dots> = bvenc (Bit_Operations.and (ucast b :: 'w word)
                                (ucast (ucast a :: 'w word) :: 'w word))"
    by (rule and2_bridge)
  also have "\<dots> = bvenc (Bit_Operations.and (ucast a :: 'w word) (ucast b))"
    by (simp add: ucast_id ac_simps)
  finally show ?thesis .
qed

lemma example_const_node:
  "eval_op (Op_Const 12) 6 [] = bvenc (word_of_int 12 :: 6 word)"
  by (simp add: bvenc_def mk_bv_def)

end
