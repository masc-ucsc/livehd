theory Translation_BT
  imports Main
begin

text \<open>
  Design-independent machinery for the emitted certificate bridge.

  Two unrelated jobs live here, both of which a generator would otherwise be
  tempted to re-emit per design:

  \<^item> a binary-tree lookup (@{text BT}, @{text bt_find}, @{text bt_keys}),
    used to define the per-design \<open>phi\<close> and \<open>nodes\<close>
    functions, together with the one lemma the off-topo side condition needs;

  \<^item> two lemmas for splitting a bounded quantifier over a list literal.

  \<^bold>\<open>Why these are stated here rather than looked up.\<close>  The
  obvious library names for the splitting lemmas do not exist
  (\<open>list_all_simps\<close>, \<open>list_all.simps\<close>), and
  \<open>find_theorems\<close> by \emph{pattern} returns zero matches for
  \<open>list_all P (x # xs)\<close>, \<open>Ball (insert a A) P\<close> and
  \<open>Ball (set (x # xs)) P\<close> alike.  Proving them locally costs two
  lines and removes the dependency on getting a name right.
\<close>

section \<open>Binary-tree lookup\<close>

text \<open>
  Keys are node ids.  The tree is built as a balanced BST literal by the
  generator, but nothing here depends on it being sorted or balanced --
  \<open>bt_find_eq_none\<close> below holds for any shape.
\<close>

datatype 'a BT = Lf | Nd nat 'a "'a BT" "'a BT"

fun bt_find :: "'a BT \<Rightarrow> nat \<Rightarrow> 'a option" where
  "bt_find BT.Lf _ = None"
| "bt_find (BT.Nd k v lo hi) m =
     (if m < k then bt_find lo m else if m = k then Some v else bt_find hi m)"

fun bt_keys :: "'a BT \<Rightarrow> nat list" where
  "bt_keys BT.Lf = []"
| "bt_keys (BT.Nd k _ lo hi) = k # (bt_keys lo @ bt_keys hi)"

text \<open>
  The lemma the off-topo side condition rests on.  A generated \<open>phi\<close>
  falls through to the source environment exactly when the tree misses, so
  proving \<open>keys \<subseteq> topo\<close> \emph{once} discharges the
  condition for every off-topo id at a stroke.

  The alternative -- deciding \<open>d \<notin> set topo\<close> per dependency
  against an N-element set literal -- is quadratic, and measured as 86\% of a
  DINO-scale build (491 s of 580 s at N=400) before it was replaced.
\<close>

lemma bt_find_eq_none:
  "d \<notin> set (bt_keys t) \<Longrightarrow> bt_find t d = None"
  by (induct t) auto

lemma bt_find_some_in_keys:
  "bt_find t d = Some v \<Longrightarrow> d \<in> set (bt_keys t)"
  by (induct t) (auto split: if_splits)

section \<open>Splitting a bounded quantifier over a list literal\<close>

text \<open>
  Used by the emitted combiner.  Discharging
  \<open>\<forall>m \<in> set topo. P m\<close> with a single \<open>simp\<close>
  carrying N rewrite rules against an N-conjunct goal is quadratic and, being one
  declaration, single-threaded: measured at 7 h 19 m of an 8 h 54 m run at
  N = 4912.  Splitting the quantifier once with these two lemmas and then
  discharging each small goal with a directed \<open>rule\<close> is O(1) per node.

  This is the Isabelle counterpart of the term-mode
  \<open>List.forall_mem_cons\<close> right fold used by \<open>pass.lean\<close>.
\<close>

lemma ball_set_nil: "(\<forall>x \<in> set []. P x) = True"
  by simp

lemma ball_set_cons:
  "(\<forall>x \<in> set (a # xs). P x) = (P a \<and> (\<forall>x \<in> set xs. P x))"
  by simp

section \<open>Sanity example\<close>

text \<open>
  The intended use, end to end, so the lemmas and the way a generator applies
  them are checked together and cannot drift apart.
\<close>

definition ex_tree :: "nat BT" where
  "ex_tree = BT.Nd 2 20 (BT.Nd 1 10 BT.Lf BT.Lf) (BT.Nd 3 30 BT.Lf BT.Lf)"

lemma ex_hit:  "bt_find ex_tree 3 = Some 30" by (simp add: ex_tree_def)
lemma ex_miss: "bt_find ex_tree 9 = None"
  by (rule bt_find_eq_none) (simp add: ex_tree_def)

lemma ex_split: "\<forall>x \<in> set [1, 2, 3::nat]. x < 4"
  apply (simp only: ball_set_cons ball_set_nil)
  apply (intro conjI TrueI)
  apply simp
  apply simp
  apply simp
  done

end
