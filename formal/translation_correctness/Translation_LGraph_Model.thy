theory Translation_LGraph_Model
  imports Main
begin

text \<open>
  Certificate model for pass.isabelle output.

  The generated DINO theories still provide fixed-width word definitions for
  execution and debugging.  The verified translation path, however, should use
  this small certificate language: a graph is data, and one generic evaluator
  interprets that data using the mathematical LGraph denotation below.
\<close>

datatype lgraph_op =
    Op_Const int
  | Op_Sum nat
  | Op_Sub
  | Op_Mult
  | Op_Div
  | Op_UDiv
  | Op_SDiv
  | Op_And
  | Op_Or
  | Op_Xor
  | Op_Ror
  | Op_Not
  | Op_LT
  | Op_GT
  | Op_ULT
  | Op_UGT
  | Op_SLT
  | Op_SGT
  | Op_EQ
  | Op_SHL
  | Op_SRA
  | Op_MuxBool
  | Op_MuxN
  | Op_Sext
  | Op_GetMask
  | Op_SetMask

datatype bv = BV nat int

fun bv_width :: "bv \<Rightarrow> nat" where
  "bv_width (BV w _) = w"

fun bv_uint :: "bv \<Rightarrow> int" where
  "bv_uint (BV w v) = v mod (2 ^ w)"

definition mk_bv :: "nat \<Rightarrow> int \<Rightarrow> bv" where
  "mk_bv w v = BV w (v mod (2 ^ w))"

definition bv_resize :: "nat \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_resize w x = mk_bv w (bv_uint x)"

definition bv_nonzero :: "bv \<Rightarrow> bool" where
  "bv_nonzero x \<longleftrightarrow> bv_uint x \<noteq> 0"

definition bv_bit :: "bv \<Rightarrow> nat \<Rightarrow> bool" where
  "bv_bit x i \<longleftrightarrow> bit (nat (bv_uint x)) i"

definition pack_bits :: "nat \<Rightarrow> (nat \<Rightarrow> bool) \<Rightarrow> int" where
  "pack_bits w f =
     sum_list (map (\<lambda>i. if f i then 2 ^ i else 0) [0..<w])"

definition bv_bitwise :: "nat \<Rightarrow> (bool \<Rightarrow> bool \<Rightarrow> bool) \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_bitwise w f a b =
     mk_bv w (pack_bits w (\<lambda>i. f (bv_bit a i) (bv_bit b i)))"

definition bv_not :: "nat \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_not w a = mk_bv w (pack_bits w (\<lambda>i. \<not> bv_bit a i))"

definition bv_sint :: "bv \<Rightarrow> int" where
  "bv_sint x =
     (let w = bv_width x; u = bv_uint x in
      if w = 0 then 0
      else if u < 2 ^ (w - 1) then u else u - 2 ^ w)"

definition bv_sra :: "nat \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_sra w x shamt =
     mk_bv w (bv_sint x div 2 ^ nat (bv_uint shamt))"

definition trunc_div_int :: "int \<Rightarrow> int \<Rightarrow> int" where
  "trunc_div_int a b =
     (let q = \<bar>a\<bar> div \<bar>b\<bar>
      in if (a < 0) \<noteq> (b < 0) then - q else q)"

definition bv_sdiv :: "nat \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_sdiv w a b =
     mk_bv w (if bv_uint b = 0 then 0 else trunc_div_int (bv_sint a) (bv_sint b))"

definition mask_indices_bv :: "bv \<Rightarrow> nat list" where
  "mask_indices_bv m = filter (\<lambda>i. bv_bit m i) [0..<bv_width m]"

primrec pack_low_bv :: "bv \<Rightarrow> nat list \<Rightarrow> int" where
  "pack_low_bv x [] = 0"
| "pack_low_bv x (i # is) =
     (if bv_bit x i then 2 ^ length is else 0) + pack_low_bv x is"

definition bv_get_mask :: "nat \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_get_mask w x m = mk_bv w (pack_low_bv x (rev (mask_indices_bv m)))"

definition bv_set_bit :: "bv \<Rightarrow> nat \<Rightarrow> bool \<Rightarrow> bv" where
  "bv_set_bit x i b =
     mk_bv (bv_width x)
       (if b then bv_uint x + (if bv_bit x i then 0 else 2 ^ i)
        else bv_uint x - (if bv_bit x i then 2 ^ i else 0))"

definition bv_set_mask :: "nat \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv \<Rightarrow> bv" where
  "bv_set_mask w a m v =
     bv_resize w
       (fold
         (\<lambda>p acc. bv_set_bit acc (snd p) (bv_bit v (fst p)))
         (zip [0..<length (mask_indices_bv m)] (mask_indices_bv m))
         a)"

fun denote_op :: "lgraph_op \<Rightarrow> nat \<Rightarrow> bv list \<Rightarrow> bv" where
  "denote_op (Op_Const c) w _ = mk_bv w c"
| "denote_op (Op_Sum n_add) w args =
     mk_bv w
       (sum_list (map bv_uint (take n_add args)) -
        sum_list (map bv_uint (drop n_add args)))"
| "denote_op Op_Sub w [a, b] = mk_bv w (bv_uint a - bv_uint b)"
| "denote_op Op_Mult w args = mk_bv w (prod_list (map bv_uint args))"
| "denote_op Op_Div w [a, b] =
     mk_bv w (if bv_uint b = 0 then 0 else bv_uint a div bv_uint b)"
| "denote_op Op_UDiv w [a, b] =
     mk_bv w (if bv_uint b = 0 then 0 else bv_uint a div bv_uint b)"
| "denote_op Op_SDiv w [a, b] = bv_sdiv w a b"
| "denote_op Op_And w [] = mk_bv w 0"
| "denote_op Op_And w (a # args) = fold (bv_bitwise w (\<and>)) args (bv_resize w a)"
| "denote_op Op_Or w args = fold (bv_bitwise w (\<or>)) args (mk_bv w 0)"
| "denote_op Op_Xor w args = fold (bv_bitwise w (\<lambda>x y. x \<noteq> y)) args (mk_bv w 0)"
| "denote_op Op_Ror w xs = mk_bv w (if list_ex bv_nonzero xs then 1 else 0)"
| "denote_op Op_Not w [a] = bv_not w a"
| "denote_op Op_LT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0)"
| "denote_op Op_GT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0)"
| "denote_op Op_ULT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0)"
| "denote_op Op_UGT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0)"
| "denote_op Op_SLT w [a, b] = mk_bv w (if bv_sint a < bv_sint b then 1 else 0)"
| "denote_op Op_SGT w [a, b] = mk_bv w (if bv_sint a > bv_sint b then 1 else 0)"
| "denote_op Op_EQ w [] = mk_bv w 1"
| "denote_op Op_EQ w (a # args) =
     mk_bv w (if list_all (\<lambda>b. bv_uint b = bv_uint a) args then 1 else 0)"
| "denote_op Op_SHL w [] = mk_bv w 0"
| "denote_op Op_SHL w (a # bs) =
     fold (bv_bitwise w (\<or>))
       (map (\<lambda>b. mk_bv w (bv_uint a * 2 ^ nat (bv_uint b))) bs)
       (mk_bv w 0)"
| "denote_op Op_SRA w [a, b] = bv_sra w a b"
| "denote_op Op_MuxBool w [sel, false_v, true_v] =
     (if bv_nonzero sel then bv_resize w true_v else bv_resize w false_v)"
| "denote_op Op_MuxN w [] = mk_bv w 0"
| "denote_op Op_MuxN w (sel # args) =
     (let idx = nat (bv_uint sel) in
      if idx < length args then bv_resize w (args ! idx) else mk_bv w 0)"
| "denote_op Op_Sext w [a, amount] =
     mk_bv w
       (let n = nat (bv_uint amount) in
        if n = 0 then 0
        else let u = bv_uint a mod 2 ^ n
             in if u < 2 ^ (n - 1) then u else u - 2 ^ n)"
| "denote_op Op_GetMask w [a, m] = bv_get_mask w a m"
| "denote_op Op_SetMask w [a, m, v] = bv_set_mask w a m v"
| "denote_op _ w _ = mk_bv w 0"

fun eval_op :: "lgraph_op \<Rightarrow> nat \<Rightarrow> bv list \<Rightarrow> bv" where
  "eval_op (Op_Const c) w _ = mk_bv w c"
| "eval_op (Op_Sum n_add) w args =
     mk_bv w
       (sum_list (map bv_uint (take n_add args)) -
        sum_list (map bv_uint (drop n_add args)))"
| "eval_op Op_Sub w [a, b] = mk_bv w (bv_uint a - bv_uint b)"
| "eval_op Op_Mult w args = mk_bv w (prod_list (map bv_uint args))"
| "eval_op Op_Div w [a, b] =
     mk_bv w (if bv_uint b = 0 then 0 else bv_uint a div bv_uint b)"
| "eval_op Op_UDiv w [a, b] =
     mk_bv w (if bv_uint b = 0 then 0 else bv_uint a div bv_uint b)"
| "eval_op Op_SDiv w [a, b] = bv_sdiv w a b"
| "eval_op Op_And w [] = mk_bv w 0"
| "eval_op Op_And w (a # args) = fold (bv_bitwise w (\<and>)) args (bv_resize w a)"
| "eval_op Op_Or w args = fold (bv_bitwise w (\<or>)) args (mk_bv w 0)"
| "eval_op Op_Xor w args = fold (bv_bitwise w (\<lambda>x y. x \<noteq> y)) args (mk_bv w 0)"
| "eval_op Op_Ror w xs = mk_bv w (if list_ex bv_nonzero xs then 1 else 0)"
| "eval_op Op_Not w [a] = bv_not w a"
| "eval_op Op_LT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0)"
| "eval_op Op_GT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0)"
| "eval_op Op_ULT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0)"
| "eval_op Op_UGT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0)"
| "eval_op Op_SLT w [a, b] = mk_bv w (if bv_sint a < bv_sint b then 1 else 0)"
| "eval_op Op_SGT w [a, b] = mk_bv w (if bv_sint a > bv_sint b then 1 else 0)"
| "eval_op Op_EQ w [] = mk_bv w 1"
| "eval_op Op_EQ w (a # args) =
     mk_bv w (if list_all (\<lambda>b. bv_uint b = bv_uint a) args then 1 else 0)"
| "eval_op Op_SHL w [] = mk_bv w 0"
| "eval_op Op_SHL w (a # bs) =
     fold (bv_bitwise w (\<or>))
       (map (\<lambda>b. mk_bv w (bv_uint a * 2 ^ nat (bv_uint b))) bs)
       (mk_bv w 0)"
| "eval_op Op_SRA w [a, b] = bv_sra w a b"
| "eval_op Op_MuxBool w [sel, false_v, true_v] =
     (if bv_nonzero sel then bv_resize w true_v else bv_resize w false_v)"
| "eval_op Op_MuxN w [] = mk_bv w 0"
| "eval_op Op_MuxN w (sel # args) =
     (let idx = nat (bv_uint sel) in
      if idx < length args then bv_resize w (args ! idx) else mk_bv w 0)"
| "eval_op Op_Sext w [a, amount] =
     mk_bv w
       (let n = nat (bv_uint amount) in
        if n = 0 then 0
        else let u = bv_uint a mod 2 ^ n
             in if u < 2 ^ (n - 1) then u else u - 2 ^ n)"
| "eval_op Op_GetMask w [a, m] = bv_get_mask w a m"
| "eval_op Op_SetMask w [a, m, v] = bv_set_mask w a m v"
| "eval_op _ w _ = mk_bv w 0"

record node_cert =
  nid :: nat
  op :: lgraph_op
  width :: nat
  deps :: "nat list"

record graph_cert =
  topo :: "nat list"
  sources :: "nat list"
  nodes :: "nat \<Rightarrow> node_cert option"

definition nodes_of_list :: "node_cert list \<Rightarrow> nat \<Rightarrow> node_cert option" where
  "nodes_of_list cs = map_of (map (\<lambda>c. (nid c, c)) cs)"

definition deps_of :: "graph_cert \<Rightarrow> nat \<Rightarrow> nat list" where
  "deps_of G n =
     (case nodes G n of
        None \<Rightarrow> []
      | Some c \<Rightarrow> deps c)"

definition node_width_of :: "graph_cert \<Rightarrow> nat \<Rightarrow> nat option" where
  "node_width_of G n =
     (case nodes G n of
        None \<Rightarrow> None
      | Some c \<Rightarrow> Some (width c))"

definition node_op_of :: "graph_cert \<Rightarrow> nat \<Rightarrow> lgraph_op option" where
  "node_op_of G n =
     (case nodes G n of
        None \<Rightarrow> None
      | Some c \<Rightarrow> Some (op c))"

definition graph_cert_wf :: "graph_cert \<Rightarrow> bool" where
  "graph_cert_wf G \<longleftrightarrow>
     distinct (topo G) \<and> distinct (sources G) \<and>
     set (topo G) \<inter> set (sources G) = {} \<and>
     (\<forall>n \<in> set (topo G).
        (case nodes G n of
           None \<Rightarrow> False
         | Some c \<Rightarrow> nid c = n \<and> width c > 0 \<and>
             set (deps c) \<subseteq> set (topo G) \<union> set (sources G))) \<and>
     (\<forall>n \<in> set (sources G). nodes G n = None)"

fun deps_before :: "nat list \<Rightarrow> node_cert list \<Rightarrow> bool" where
  "deps_before seen [] = True"
| "deps_before seen (c # cs) =
     (set (deps c) \<subseteq> set seen \<and> deps_before (seen @ [nid c]) cs)"

definition graph_cert_wf_bool :: "node_cert list \<Rightarrow> nat list \<Rightarrow> bool" where
  "graph_cert_wf_bool cs srcs \<longleftrightarrow>
     distinct (map nid cs) \<and>
     distinct srcs \<and>
     set (map nid cs) \<inter> set srcs = {} \<and>
     (\<forall>c \<in> set cs. width c > 0 \<and> set (deps c) \<subseteq> set (map nid cs) \<union> set srcs) \<and>
     deps_before srcs cs"

definition node_cert_chunk_wf_bool :: "nat list \<Rightarrow> nat list \<Rightarrow> node_cert list \<Rightarrow> bool" where
  "node_cert_chunk_wf_bool all_ids srcs cs \<longleftrightarrow>
     distinct (map nid cs) \<and>
     set (map nid cs) \<subseteq> set all_ids \<and>
     set (map nid cs) \<inter> set srcs = {} \<and>
     (\<forall>c \<in> set cs. width c > 0 \<and> set (deps c) \<subseteq> set all_ids \<union> set srcs)"

definition const_node_cert_wf_bool :: "node_cert \<Rightarrow> bool" where
  "const_node_cert_wf_bool c \<longleftrightarrow>
     (case op c of
        Op_Const _ \<Rightarrow> width c > 0 \<and> deps c = []
      | _ \<Rightarrow> False)"

lemma const_node_cert_wf_boolD:
  assumes "const_node_cert_wf_bool c"
  shows "width c > 0 \<and> deps c = []"
  using assms
  by (cases "op c"; simp add: const_node_cert_wf_bool_def)

lemma const_node_cert_chunk_wf_bool_sound:
  assumes const_nodes: "list_all const_node_cert_wf_bool cs"
  assumes distinct_ids: "distinct (map nid cs)"
  assumes ids_subset: "set (map nid cs) \<subseteq> set all_ids"
  assumes ids_disjoint: "set (map nid cs) \<inter> set srcs = {}"
  shows "node_cert_chunk_wf_bool all_ids srcs cs"
proof -
  have props: "\<And>c. c \<in> set cs \<Longrightarrow> width c > 0 \<and> deps c = []"
    using const_nodes
    by (auto simp: list_all_iff dest: const_node_cert_wf_boolD)
  show ?thesis
    using distinct_ids ids_subset ids_disjoint props
    by (auto simp: node_cert_chunk_wf_bool_def)
qed

definition valid_deps_bool :: "(nat \<Rightarrow> bool) \<Rightarrow> nat list \<Rightarrow> bool" where
  "valid_deps_bool valid_ref ds \<longleftrightarrow> list_all valid_ref ds"

definition node_cert_deps :: "node_cert list \<Rightarrow> nat list" where
  "node_cert_deps cs = concat (map deps cs)"

definition simple_op_cert_wf_bool :: "lgraph_op \<Rightarrow> nat \<Rightarrow> nat list \<Rightarrow> bool" where
  "simple_op_cert_wf_bool opc w ds \<longleftrightarrow>
     (case opc of
        Op_Const _ \<Rightarrow> ds = []
      | Op_Sum n_add \<Rightarrow> length ds > 0 \<and> n_add \<le> length ds
      | Op_And \<Rightarrow> length ds > 0
      | Op_Or \<Rightarrow> length ds > 0
      | Op_Xor \<Rightarrow> length ds > 0
      | Op_Ror \<Rightarrow> length ds > 0 \<and> w = 1
      | Op_Not \<Rightarrow> length ds = 1
      | Op_EQ \<Rightarrow> length ds = 2 \<and> w = 1
      | Op_ULT \<Rightarrow> length ds = 2 \<and> w = 1
      | Op_UGT \<Rightarrow> length ds = 2 \<and> w = 1
      | Op_SLT \<Rightarrow> length ds = 2 \<and> w = 1
      | Op_SGT \<Rightarrow> length ds = 2 \<and> w = 1
      | Op_GetMask \<Rightarrow> length ds = 2
      | Op_MuxBool \<Rightarrow> length ds = 3
      | Op_MuxN \<Rightarrow> length ds > 1
      | Op_SHL \<Rightarrow> length ds = 2
      | Op_SRA \<Rightarrow> length ds = 2
      | Op_Sext \<Rightarrow> length ds = 2
      | _ \<Rightarrow> False)"

definition simple_node_cert_shape_wf_bool :: "node_cert \<Rightarrow> bool" where
  "simple_node_cert_shape_wf_bool c \<longleftrightarrow>
     width c > 0 \<and>
     simple_op_cert_wf_bool (op c) (width c) (deps c)"

definition simple_node_cert_wf_bool :: "(nat \<Rightarrow> bool) \<Rightarrow> node_cert \<Rightarrow> bool" where
  "simple_node_cert_wf_bool valid_ref c \<longleftrightarrow>
     width c > 0 \<and>
     valid_deps_bool valid_ref (deps c) \<and>
     simple_op_cert_wf_bool (op c) (width c) (deps c)"

lemma simple_node_cert_wf_boolD:
  assumes "simple_node_cert_wf_bool valid_ref c"
  assumes valid_ref_sound: "\<And>x. valid_ref x \<Longrightarrow> x \<in> set all_ids \<or> x \<in> set srcs"
  shows "width c > 0 \<and> set (deps c) \<subseteq> set all_ids \<union> set srcs"
  using assms
  by (auto simp: simple_node_cert_wf_bool_def valid_deps_bool_def list_all_iff)

lemma simple_node_cert_chunk_wf_bool_sound:
  assumes simple_nodes: "list_all (simple_node_cert_wf_bool valid_ref) cs"
  assumes valid_ref_sound: "\<And>x. valid_ref x \<Longrightarrow> x \<in> set all_ids \<or> x \<in> set srcs"
  assumes distinct_ids: "distinct (map nid cs)"
  assumes ids_subset: "set (map nid cs) \<subseteq> set all_ids"
  assumes ids_disjoint: "set (map nid cs) \<inter> set srcs = {}"
  shows "node_cert_chunk_wf_bool all_ids srcs cs"
proof -
  have props: "\<And>c. c \<in> set cs \<Longrightarrow>
      width c > 0 \<and> set (deps c) \<subseteq> set all_ids \<union> set srcs"
    using simple_nodes
    by (auto simp: list_all_iff dest!: simple_node_cert_wf_boolD[OF _ valid_ref_sound])
  show ?thesis
    using distinct_ids ids_subset ids_disjoint props
    by (auto simp: node_cert_chunk_wf_bool_def)
qed

lemma simple_node_cert_shape_wf_boolD:
  assumes "simple_node_cert_shape_wf_bool c"
  shows "width c > 0"
  using assms
  by (simp add: simple_node_cert_shape_wf_bool_def)

lemma simple_node_cert_chunk_wf_bool_sound':
  assumes simple_nodes: "list_all simple_node_cert_shape_wf_bool cs"
  assumes deps_subset: "set (node_cert_deps cs) \<subseteq> set all_ids \<union> set srcs"
  assumes distinct_ids: "distinct (map nid cs)"
  assumes ids_subset: "set (map nid cs) \<subseteq> set all_ids"
  assumes ids_disjoint: "set (map nid cs) \<inter> set srcs = {}"
  shows "node_cert_chunk_wf_bool all_ids srcs cs"
proof -
  have widths: "\<And>c. c \<in> set cs \<Longrightarrow> width c > 0"
    using simple_nodes
    by (auto simp: list_all_iff dest: simple_node_cert_shape_wf_boolD)
  have deps: "\<And>c. c \<in> set cs \<Longrightarrow> set (deps c) \<subseteq> set all_ids \<union> set srcs"
    using deps_subset
    by (auto simp: node_cert_deps_def)
  show ?thesis
    using distinct_ids ids_subset ids_disjoint widths deps
    by (auto simp: node_cert_chunk_wf_bool_def)
qed

lemma map_of_node_certs_some:
  assumes "distinct (map nid cs)"
  assumes "c \<in> set cs"
  shows "nodes_of_list cs (nid c) = Some c"
  using assms
  by (auto simp: nodes_of_list_def map_of_eq_Some_iff distinct_map inj_on_def)

lemma map_of_node_certs_none:
  assumes "n \<notin> set (map nid cs)"
  shows "nodes_of_list cs n = None"
  using assms
  by (auto simp: nodes_of_list_def map_of_eq_None_iff)

lemma nodes_of_list_some_from_nid:
  assumes "distinct (map nid cs)"
  assumes "n \<in> set (map nid cs)"
  shows "\<exists>c. nodes_of_list cs n = Some c \<and> nid c = n \<and> c \<in> set cs"
proof -
  from assms(2) obtain c where c: "c \<in> set cs" "nid c = n"
    by auto
  then have "nodes_of_list cs n = Some c"
    using map_of_node_certs_some[OF assms(1), of c] by simp
  then show ?thesis
    using c by auto
qed

lemma nodes_of_list_some_in_set:
  assumes "nodes_of_list cs n = Some c"
  shows "c \<in> set cs \<and> nid c = n"
  using assms
  by (auto simp: nodes_of_list_def dest!: map_of_SomeD)

lemma graph_cert_wf_bool_sound:
  assumes "graph_cert_wf_bool cs srcs"
  shows "graph_cert_wf
    \<lparr>topo = map nid cs, sources = srcs, nodes = nodes_of_list cs\<rparr>"
proof -
  have distinct_nodes: "distinct (map nid cs)"
    using assms by (simp add: graph_cert_wf_bool_def)
  have distinct_srcs: "distinct srcs"
    using assms by (simp add: graph_cert_wf_bool_def)
  have disjoint: "set (map nid cs) \<inter> set srcs = {}"
    using assms by (simp add: graph_cert_wf_bool_def)
  have node_props:
    "\<And>c. c \<in> set cs \<Longrightarrow>
      width c > 0 \<and> set (deps c) \<subseteq> set (map nid cs) \<union> set srcs"
    using assms by (auto simp: graph_cert_wf_bool_def)
  have source_none:
    "\<And>n. n \<in> set srcs \<Longrightarrow> nodes_of_list cs n = None"
    using disjoint by (auto simp: disjoint_iff intro: map_of_node_certs_none)
  show ?thesis
    using distinct_nodes distinct_srcs disjoint node_props source_none
          map_of_node_certs_some[OF distinct_nodes]
    by (auto simp: graph_cert_wf_def)
qed

lemma list_all_node_cert_chunk_wfD:
  assumes "list_all (node_cert_chunk_wf_bool all_ids srcs) chunks"
  assumes "c \<in> set (concat chunks)"
  shows "width c > 0 \<and> set (deps c) \<subseteq> set all_ids \<union> set srcs"
  using assms
  by (auto simp: node_cert_chunk_wf_bool_def list_all_iff)

lemma list_all_node_cert_chunk_disjointD:
  assumes "list_all (node_cert_chunk_wf_bool all_ids srcs) chunks"
  assumes "n \<in> set (concat (map (map nid) chunks))"
  shows "n \<notin> set srcs"
  using assms
  by (auto simp: node_cert_chunk_wf_bool_def list_all_iff)

lemma graph_cert_wf_from_chunk_checks:
  assumes chunks_ids: "map (map nid) chunks = id_chunks"
  assumes distinct_ids: "distinct (concat id_chunks)"
  assumes distinct_srcs: "distinct srcs"
  assumes chunk_checks: "list_all (node_cert_chunk_wf_bool (concat id_chunks) srcs) chunks"
  shows "graph_cert_wf
    \<lparr>topo = concat id_chunks, sources = srcs, nodes = nodes_of_list (concat chunks)\<rparr>"
proof -
  have map_concat0: "map nid (concat chunks) = concat (map (map nid) chunks)"
    by (induct chunks) auto
  have map_concat: "map nid (concat chunks) = concat id_chunks"
    using chunks_ids map_concat0 by simp
  have distinct_nodes: "distinct (map nid (concat chunks))"
    using distinct_ids map_concat by simp
  have disjoint0: "set (map nid (concat chunks)) \<inter> set srcs = {}"
    using chunk_checks
    by (auto simp: node_cert_chunk_wf_bool_def list_all_iff)
  have disjoint: "set (concat id_chunks) \<inter> set srcs = {}"
    using disjoint0 map_concat by simp
  have source_none:
    "\<And>n. n \<in> set srcs \<Longrightarrow> nodes_of_list (concat chunks) n = None"
    using disjoint
    by (auto simp: map_concat disjoint_iff
             intro!: map_of_node_certs_none)
  have node_props:
    "\<And>c. c \<in> set (concat chunks) \<Longrightarrow>
      width c > 0 \<and> set (deps c) \<subseteq> set (concat id_chunks) \<union> set srcs"
    using chunk_checks by (rule list_all_node_cert_chunk_wfD)
  have node_lookup_props:
    "\<And>n c. n \<in> set (concat id_chunks) \<Longrightarrow>
      nodes_of_list (concat chunks) n = Some c \<Longrightarrow>
      nid c = n \<and> width c > 0 \<and>
      set (deps c) \<subseteq> set (concat id_chunks) \<union> set srcs"
    using node_props nodes_of_list_some_in_set[of "concat chunks"]
    by blast
  show ?thesis
    unfolding graph_cert_wf_def
  proof (intro conjI ballI)
    show "distinct (topo \<lparr>topo = concat id_chunks, sources = srcs,
                         nodes = nodes_of_list (concat chunks)\<rparr>)"
      using distinct_ids by simp
  next
    show "distinct (sources \<lparr>topo = concat id_chunks, sources = srcs,
                            nodes = nodes_of_list (concat chunks)\<rparr>)"
      using distinct_srcs by simp
  next
    show "set (topo \<lparr>topo = concat id_chunks, sources = srcs,
                     nodes = nodes_of_list (concat chunks)\<rparr>) \<inter>
          set (sources \<lparr>topo = concat id_chunks, sources = srcs,
                         nodes = nodes_of_list (concat chunks)\<rparr>) = {}"
      using disjoint by simp
  next
    fix n
    assume n: "n \<in> set (topo \<lparr>topo = concat id_chunks, sources = srcs,
                              nodes = nodes_of_list (concat chunks)\<rparr>)"
    then have n_map: "n \<in> set (map nid (concat chunks))"
      using map_concat by simp
    then obtain c where c:
      "nodes_of_list (concat chunks) n = Some c" "nid c = n" "c \<in> set (concat chunks)"
      using nodes_of_list_some_from_nid[OF distinct_nodes] by auto
    then have props:
      "nid c = n \<and> width c > 0 \<and>
       set (deps c) \<subseteq> set (concat id_chunks) \<union> set srcs"
      using node_lookup_props n by simp
    show "case nodes \<lparr>topo = concat id_chunks, sources = srcs,
                       nodes = nodes_of_list (concat chunks)\<rparr> n of
            None \<Rightarrow> False
          | Some c \<Rightarrow>
              nid c = n \<and> 0 < width c \<and>
              set (deps c) \<subseteq>
                set (topo \<lparr>topo = concat id_chunks, sources = srcs,
                           nodes = nodes_of_list (concat chunks)\<rparr>) \<union>
                set (sources \<lparr>topo = concat id_chunks, sources = srcs,
                              nodes = nodes_of_list (concat chunks)\<rparr>)"
      using c props by simp
  next
    fix n
    assume "n \<in> set (sources \<lparr>topo = concat id_chunks, sources = srcs,
                               nodes = nodes_of_list (concat chunks)\<rparr>)"
    then show "nodes \<lparr>topo = concat id_chunks, sources = srcs,
                       nodes = nodes_of_list (concat chunks)\<rparr> n = None"
      using source_none by simp
  qed
qed

definition denote_node :: "graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "denote_node G source_env n =
     (case nodes G n of
        None \<Rightarrow> source_env n
      | Some c \<Rightarrow> denote_op (op c) (width c) (map source_env (deps c)))"

definition eval_node :: "graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "eval_node G rho n =
     (case nodes G n of
        None \<Rightarrow> rho n
      | Some c \<Rightarrow> eval_op (op c) (width c) (map rho (deps c)))"

definition denote_node_env :: "graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "denote_node_env G rho n =
     (case nodes G n of
        None \<Rightarrow> rho n
      | Some c \<Rightarrow> denote_op (op c) (width c) (map rho (deps c)))"

fun eval_graph :: "nat list \<Rightarrow> graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "eval_graph [] G rho = rho"
| "eval_graph (n # ns) G rho =
     eval_graph ns G (rho(n := eval_node G rho n))"

fun denote_graph :: "nat list \<Rightarrow> graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "denote_graph [] G rho = rho"
| "denote_graph (n # ns) G rho =
     denote_graph ns G (rho(n := denote_node_env G rho n))"

definition deps_correct :: "graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bool" where
  "deps_correct G rho denote n \<longleftrightarrow>
     (\<forall>d \<in> set (deps_of G n). rho d = denote d)"

lemma deps_of_some[simp]:
  "nodes G n = Some c \<Longrightarrow> deps_of G n = deps c"
  by (simp add: deps_of_def)

lemma node_width_of_some[simp]:
  "nodes G n = Some c \<Longrightarrow> node_width_of G n = Some (width c)"
  by (simp add: node_width_of_def)

lemma node_op_of_some[simp]:
  "nodes G n = Some c \<Longrightarrow> node_op_of G n = Some (op c)"
  by (simp add: node_op_of_def)

lemma eval_op_correct:
  "eval_op oper w args = denote_op oper w args"
  by (induct oper w args rule: denote_op.induct) simp_all

lemma eval_node_correct:
  assumes "\<And>d. d \<in> set (deps_of G n) \<Longrightarrow> rho d = source_denote d"
  shows "eval_node G rho n =
    (case nodes G n of
       None \<Rightarrow> rho n
     | Some c \<Rightarrow> denote_op (op c) (width c) (map source_denote (deps c)))"
proof (cases "nodes G n")
  case None
  then show ?thesis
    by (simp add: eval_node_def)
next
  case (Some c)
  have map_eq: "map rho (deps c) = map source_denote (deps c)"
    using assms Some
    by (intro map_cong) (auto simp: deps_of_def)
  have lhs:
    "eval_node G rho n = denote_op (op c) (width c) (map rho (deps c))"
    using Some by (simp add: eval_node_def eval_op_correct)
  then show ?thesis
    using Some map_eq by simp
qed

lemma eval_node_eq_denote_node_env:
  "eval_node G rho n = denote_node_env G rho n"
  by (simp add: eval_node_def denote_node_env_def eval_op_correct split: option.split)

lemma eval_graph_eq_denote_graph:
  "eval_graph order G rho = denote_graph order G rho"
  by (induct order arbitrary: rho) (simp_all add: eval_node_eq_denote_node_env)

end
