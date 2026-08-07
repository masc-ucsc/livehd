theory Translation_OpBridge
  imports
    Translation_LGraph_Model
    "DINO_Semantic_Primitives_Test.SemanticPrimitives"
begin

text \<open>
  WORK IN PROGRESS --- deliberately NOT listed in this session's ROOT.

  Everything below the encoding section is unverified.  Do not add this theory
  to ROOT until it builds; including it makes the whole session hang, not fail.

  Bisected state (Isabelle2025-2, threads=4), each step measured in isolation:

  \<^item> @{const bvenc}, \<open>bv_width_bvenc\<close>,
    \<open>bv_uint_bvenc\<close>, \<open>bvenc_word_of_int\<close> --- all check,
    ~2 s.
  \<^item> \<open>bvenc_ucast\<close> --- \<^bold>\<open>HANGS\<close> (killed at
    400 s; the session otherwise builds in ~40 s).  It hangs both as
    \<open>by (simp add: bvenc_def bv_resize_def mk_bv_def ucast_eq
    uint_word_of_int)\<close> and when restructured to isolate
    \<open>by (simp add: ucast_eq)\<close> on the single subgoal
    \<open>(ucast x :: 'b word) = word_of_int (uint x)\<close>, so the loop is
    \<open>ucast_eq\<close> under \<open>simp\<close>, not an interaction between
    lemmas.  Next step is to find the right \<open>uint (ucast _)\<close>
    rewrite (candidates: \<open>unsigned_ucast_eq\<close>,
    \<open>uint_up_ucast\<close>, \<open>take_bit\<close>-based forms) rather
    than routing through \<open>ucast_eq\<close>, and to prove it by
    \<open>word_eqI\<close>/transfer instead of \<open>simp\<close>.
  \<^item> Everything after that (bit correspondence, \<open>pack_bits\<close>,
    the bitwise family, constants, the examples) is written but has never been
    checked, because the hang blocks the file.

  \<open>bvenc_ucast\<close> is load-bearing, not incidental: it is the identity
  \<open>fast ucast = certificate bv_resize\<close>, which is what every
  width-polymorphic bridge below rewrites with.  It has to land first.

  Lesson worth keeping (the Isabelle version of a documented Lean one): a
  looping \<open>simp\<close> presents as a \<^emph>\<open>hang\<close>, and an
  unfinished theory listed in ROOT hangs every downstream build --- including
  probes meant to isolate it.  The first bisection attempt here measured
  nothing for 10 minutes because the probe session's parent still listed this
  theory.  Bisect with the suspect theory removed from ROOT.
\<close>

text \<open>
  Piece A of the certificate-equivalence bridge: per-operator lemmas relating the
  certificate evaluator @{const eval_op} (untyped @{typ bv}) to the fast model's
  native word operations (@{typ "'w::len word"}).

  The encoding is @{const bvenc} below.  Isabelle's word widths are types, so
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
    @{thm [source] bvenc_ucast}.  \<open>ucast\<close> on the fast side and
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
  by (auto simp: bvenc_def)

text \<open>
  The two normalization lemmas the per-node proofs live on.
\<close>

lemma bvenc_word_of_int:
  "bvenc (word_of_int v :: 'w::len word) = mk_bv LENGTH('w) v"
  by (simp add: bvenc_def mk_bv_def uint_word_of_int)

lemma bvenc_ucast:
  "bvenc (ucast (x :: 'a::len word) :: 'b::len word) = bv_resize LENGTH('b) (bvenc x)"
  by (simp add: bvenc_def bv_resize_def mk_bv_def ucast_eq uint_word_of_int)

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
  have "bv_bit (bvenc x) i = bit (nat (uint x)) i"
    by (simp add: bv_bit_def)
  also have "\<dots> = bit (unat x) i"
    by (simp add: unsigned_def)
  also have "\<dots> = bit x i"
    by (simp add: bit_unat_iff)
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
    by (simp add: take_bit_int_eq_self uint_lt2p)
  finally show ?thesis by simp
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

section \<open>Sanity examples\<close>

text \<open>
  Each bridge is exercised in the shape a generated per-node proof would use it,
  so the lemma and its intended use are checked together and cannot drift.
\<close>

lemma example_and_node:
  fixes a :: "7 word" and b :: "9 word"
  shows "eval_op Op_And 5 [bvenc a, bvenc b]
           = bvenc (Bit_Operations.and (ucast a :: 5 word) (ucast b))"
proof -
  have "eval_op Op_And 5 [bvenc a, bvenc b]
          = bv_bitwise 5 (\<and>) (bvenc b) (bv_resize 5 (bvenc a))"
    by simp
  also have "\<dots> = bv_bitwise 5 (\<and>) (bvenc b) (bvenc (ucast a :: 5 word))"
    by (simp add: bvenc_ucast)
  also have "\<dots> = bvenc (Bit_Operations.and (ucast b :: 5 word) (ucast (ucast a :: 5 word)))"
    by (rule and2_bridge[where 'w = 5])
  also have "\<dots> = bvenc (Bit_Operations.and (ucast a :: 5 word) (ucast b))"
    by (simp add: ac_simps)
  finally show ?thesis .
qed

lemma example_const_node:
  "eval_op (Op_Const 12) 6 [] = bvenc (word_of_int 12 :: 6 word)"
  by (rule const_bridge[where 'w = 6])

end
