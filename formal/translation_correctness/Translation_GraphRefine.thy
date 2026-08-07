theory Translation_GraphRefine
  imports Translation_LGraph_Model
begin

text \<open>
  Piece B of the certificate-equivalence bridge: the design-independent
  refinement theorem.

  @{const eval_graph} evaluates a certificate by a \emph{sequential fold} over
  the topological order --- the source environment with one function update
  stacked on it per node.  Asking ``what is the value at node n?'' means peeling
  those updates, and every dependency lookup peels them again.  A generated fast
  model is the opposite shape: one standalone definition per node, all values
  available simultaneously and independently.

  Rather than simulate the fold, observe that on a dependency-ordered graph
  @{const eval_graph} computes the unique fixpoint of the node recurrence.  So it
  suffices that the encoded fast environment \<open>phi\<close> \emph{satisfies}
  that recurrence:

    \<open>phi n = eval_node G phi n\<close>   for every n in the order

  which is a \emph{local, per-node} statement.  It mentions neither
  @{const eval_graph} nor any other node's proof, so a generator can emit one
  small independent lemma per node and Isabelle can check them in parallel.

  Companion: \<open>eval_graph_not_mem\<close> below.  An id outside the order is
  never updated by the fold, so it reads through to the source environment.  That
  is what makes an output (or flop din) driven \emph{directly} by a constant,
  input, or flop provable --- such an id has no node value and no recurrence, so
  the main theorem does not apply to it.
\<close>

subsection \<open>Dependency ordering\<close>

text \<open>
  \<open>dep_ordered G order\<close>: no dependency of a node lies in the
  not-yet-evaluated suffix that starts at that node.  Equivalently, every
  dependency has already been evaluated or is a source.  Note this also rules
  out a self-loop, since \<open>n\<close> itself is in its own suffix.
\<close>

fun dep_ordered :: "graph_cert \<Rightarrow> nat list \<Rightarrow> bool" where
  "dep_ordered G [] = True"
| "dep_ordered G (n # ns) =
     (list_all (\<lambda>d. d \<notin> set (n # ns)) (deps_of G n) \<and> dep_ordered G ns)"

lemma dep_ordered_ConsD:
  assumes "dep_ordered G (n # ns)"
  shows "\<forall>d \<in> set (deps_of G n). d \<notin> set (n # ns)"
    and "dep_ordered G ns"
  using assms by (auto simp: list_all_iff)

text \<open>
  Checking @{const dep_ordered} directly is quadratic: each node scans the whole
  remaining suffix.  This accumulator form is the one a generator should emit ---
  it carries the set of already-available ids (seeded with the sources) and only
  tests membership.
\<close>

fun dep_ordered_acc :: "graph_cert \<Rightarrow> nat set \<Rightarrow> nat list \<Rightarrow> bool" where
  "dep_ordered_acc G seen [] = True"
| "dep_ordered_acc G seen (n # ns) =
     (list_all (\<lambda>d. d \<in> seen) (deps_of G n) \<and>
      dep_ordered_acc G (insert n seen) ns)"

lemma dep_ordered_acc_sound:
  assumes "dep_ordered_acc G S order"
  assumes "S \<inter> set order = {}"
  assumes "distinct order"
  shows "dep_ordered G order"
  using assms
proof (induct order arbitrary: S)
  case Nil
  then show ?case by simp
next
  case (Cons n ns)
  have deps_in_S: "\<forall>d \<in> set (deps_of G n). d \<in> S"
    using Cons.prems(1) by (simp add: list_all_iff)
  have head: "\<forall>d \<in> set (deps_of G n). d \<notin> set (n # ns)"
    using deps_in_S Cons.prems(2) by blast
  have "dep_ordered G ns"
  proof (rule Cons.hyps)
    show "dep_ordered_acc G (insert n S) ns"
      using Cons.prems(1) by simp
    show "insert n S \<inter> set ns = {}"
      using Cons.prems(2,3) by auto
    show "distinct ns"
      using Cons.prems(3) by simp
  qed
  then show ?case
    using head by (simp add: list_all_iff)
qed

subsection \<open>Off-topo ids read through to the source environment\<close>

lemma eval_graph_not_mem:
  assumes "d \<notin> set order"
  shows "eval_graph order G rho d = rho d"
  using assms
proof (induct order arbitrary: rho)
  case Nil
  then show ?case by simp
next
  case (Cons n ns)
  have dn: "d \<noteq> n" and dns: "d \<notin> set ns"
    using Cons.prems by auto
  have "eval_graph (n # ns) G rho d
          = eval_graph ns G (rho(n := eval_node G rho n)) d"
    by simp
  also have "\<dots> = (rho(n := eval_node G rho n)) d"
    using Cons.hyps[OF dns] by blast
  also have "\<dots> = rho d"
    using dn by simp
  finally show ?case .
qed

subsection \<open>Node evaluation only reads its dependencies\<close>

lemma eval_node_cong_some:
  assumes some: "nodes G n = Some c"
  assumes agree: "\<And>d. d \<in> set (deps c) \<Longrightarrow> rho d = rho' d"
  shows "eval_node G rho n = eval_node G rho' n"
proof -
  have m: "map rho (deps c) = map rho' (deps c)"
    using agree by (intro map_cong) auto
  show ?thesis
    by (simp add: eval_node_def some m)
qed

subsection \<open>The refinement theorem\<close>

lemma eval_graph_of_local_agree_all:
  assumes ord: "dep_ordered G order"
  assumes some: "\<forall>n \<in> set order. nodes G n \<noteq> None"
  assumes rec: "\<forall>n \<in> set order. phi n = eval_node G phi n"
  assumes src: "\<forall>n \<in> set order. \<forall>d \<in> set (deps_of G n).
                   d \<notin> set order \<longrightarrow> rho d = phi d"
  shows "\<forall>n \<in> set order. eval_graph order G rho n = phi n"
  using assms
proof (induct order arbitrary: rho)
  case Nil
  then show ?case by simp
next
  case (Cons n ns)

  obtain c where c: "nodes G n = Some c"
    using Cons.prems(2) by (cases "nodes G n") auto
  have deps_c: "deps_of G n = deps c"
    using c by simp

  \<comment> \<open>Every dependency of the head is outside the remaining suffix, so
      @{term rho} already agrees with @{term phi} on all of them.\<close>
  have deps_out: "\<forall>d \<in> set (deps c). d \<notin> set (n # ns)"
    using Cons.prems(1) deps_c by (simp add: list_all_iff)
  have deps_agree: "\<And>d. d \<in> set (deps c) \<Longrightarrow> rho d = phi d"
    using Cons.prems(4) deps_out deps_c by auto

  \<comment> \<open>Hence evaluating the head under @{term rho} already lands on
      @{term "phi n"}, by the head's own recurrence.\<close>
  have head: "eval_node G rho n = phi n"
    using eval_node_cong_some[OF c deps_agree] Cons.prems(3) by simp

  define rho' where "rho' = rho(n := eval_node G rho n)"
  have rho'_n: "rho' n = phi n"
    using head by (simp add: rho'_def)

  have ih: "\<forall>m \<in> set ns. eval_graph ns G rho' m = phi m"
  proof (rule Cons.hyps)
    show "dep_ordered G ns"
      using Cons.prems(1) by simp
    show "\<forall>m \<in> set ns. nodes G m \<noteq> None"
      using Cons.prems(2) by simp
    show "\<forall>m \<in> set ns. phi m = eval_node G phi m"
      using Cons.prems(3) by simp
    show "\<forall>m \<in> set ns. \<forall>d \<in> set (deps_of G m).
            d \<notin> set ns \<longrightarrow> rho' d = phi d"
    proof (intro ballI impI)
      fix m d
      assume m: "m \<in> set ns" and d: "d \<in> set (deps_of G m)" and out: "d \<notin> set ns"
      show "rho' d = phi d"
      proof (cases "d = n")
        case True
        then show ?thesis using rho'_n by simp
      next
        case False
        then have "d \<notin> set (n # ns)" using out by simp
        then have "rho d = phi d" using Cons.prems(4) m d by simp
        then show ?thesis using False by (simp add: rho'_def)
      qed
    qed
  qed

  show ?case
  proof (intro ballI)
    fix m assume m: "m \<in> set (n # ns)"
    have unfold: "eval_graph (n # ns) G rho m = eval_graph ns G rho' m"
      by (simp add: rho'_def)
    show "eval_graph (n # ns) G rho m = phi m"
    proof (cases "m \<in> set ns")
      case True
      then show ?thesis using ih unfold by simp
    next
      case False
      then have "m = n" using m by simp
      then show ?thesis
        using unfold eval_graph_not_mem[OF False, of G rho'] rho'_n False by simp
    qed
  qed
qed

text \<open>
  The form a generator instantiates.  Each hypothesis is discharged separately:
  \<^item> \<open>ord\<close> / \<open>some\<close> by an executable check over the
    emitted certificate data (see @{thm [source] dep_ordered_acc_sound});
  \<^item> \<open>rec\<close> by one independent per-node lemma, each closed by that
    node's op bridge;
  \<^item> \<open>src\<close> by the definition of \<open>phi\<close>, which falls
    through to the source environment off-topo.
\<close>

theorem eval_graph_of_local_agree:
  assumes ord: "dep_ordered G (topo G)"
  assumes some: "\<forall>n \<in> set (topo G). nodes G n \<noteq> None"
  assumes rec: "\<forall>n \<in> set (topo G). phi n = eval_node G phi n"
  assumes src: "\<forall>n \<in> set (topo G). \<forall>d \<in> set (deps_of G n).
                   d \<notin> set (topo G) \<longrightarrow> rho d = phi d"
  assumes mem: "n \<in> set (topo G)"
  shows "eval_graph (topo G) G rho n = phi n"
  using eval_graph_of_local_agree_all[OF ord some rec src] mem by blast

text \<open>
  A convenience form for the common case where \<open>phi\<close> is defined to
  fall through to the very environment being folded, so the off-topo side
  condition is definitional.
\<close>

corollary eval_graph_of_local_agree_fallthrough:
  assumes ord: "dep_ordered G (topo G)"
  assumes some: "\<forall>n \<in> set (topo G). nodes G n \<noteq> None"
  assumes rec: "\<forall>n \<in> set (topo G). phi n = eval_node G phi n"
  assumes fall: "\<And>d. d \<notin> set (topo G) \<Longrightarrow> phi d = rho d"
  shows "\<forall>n \<in> set (topo G). eval_graph (topo G) G rho n = phi n"
  by (rule eval_graph_of_local_agree_all[OF ord some rec]) (auto simp: fall)

subsection \<open>Sanity examples\<close>

text \<open>
  A two-node certificate, checked end to end, so that the theorem and its
  intended use are validated together and cannot drift apart.

  Graph: source 100, then node 1 = Not(100), node 2 = And(1, 100).
\<close>

definition ex_certs :: "node_cert list" where
  "ex_certs =
     [\<lparr>nid = 1, op = Op_Not,  width = 4, deps = [100]\<rparr>,
      \<lparr>nid = 2, op = Op_And,  width = 4, deps = [1, 100]\<rparr>]"

definition ex_graph :: graph_cert where
  "ex_graph = \<lparr>topo = [1, 2], sources = [100], nodes = nodes_of_list ex_certs\<rparr>"

definition ex_src :: "nat \<Rightarrow> bv" where
  "ex_src n = (if n = 100 then mk_bv 4 6 else mk_bv 4 0)"

text \<open>
  Note the exact shape of the \<open>Op_And\<close> value.  @{const eval_op} folds
  the tail over @{term "bv_resize w"} of the head:
  \<open>fold (bv_bitwise w (\<and>)) args (bv_resize w a)\<close>, and
  @{term fold} applies \<open>f x acc\<close> --- so the \emph{later} dependency is
  the first argument and the accumulator the second.  Getting that order or the
  @{const bv_resize} wrong is a per-node proof failure, which is exactly the kind
  of detail the op-bridge lemmas have to pin down once.
\<close>

definition ex_phi :: "nat \<Rightarrow> bv" where
  "ex_phi n =
     (if n = 1 then bv_not 4 (ex_src 100)
      else if n = 2
        then bv_bitwise 4 (\<and>) (ex_src 100) (bv_resize 4 (bv_not 4 (ex_src 100)))
      else ex_src n)"

lemma ex_dep_ordered: "dep_ordered ex_graph (topo ex_graph)"
  by (simp add: ex_graph_def ex_certs_def nodes_of_list_def deps_of_def)

lemma ex_some: "\<forall>n \<in> set (topo ex_graph). nodes ex_graph n \<noteq> None"
  by (simp add: ex_graph_def ex_certs_def nodes_of_list_def)

lemma ex_rec: "\<forall>n \<in> set (topo ex_graph). ex_phi n = eval_node ex_graph ex_phi n"
  by (simp add: ex_graph_def ex_certs_def nodes_of_list_def
                eval_node_def ex_phi_def)

lemma ex_fallthrough: "d \<notin> set (topo ex_graph) \<Longrightarrow> ex_phi d = ex_src d"
  by (simp add: ex_graph_def ex_phi_def)

lemma ex_bridge:
  "\<forall>n \<in> set (topo ex_graph). eval_graph (topo ex_graph) ex_graph ex_src n = ex_phi n"
  by (rule eval_graph_of_local_agree_fallthrough
        [OF ex_dep_ordered ex_some ex_rec ex_fallthrough])

text \<open>The accumulator checker agrees with the quadratic predicate here.\<close>

lemma ex_dep_ordered_acc:
  "dep_ordered ex_graph (topo ex_graph)"
proof (rule dep_ordered_acc_sound[where S = "set (sources ex_graph)"])
  show "dep_ordered_acc ex_graph (set (sources ex_graph)) (topo ex_graph)"
    by (simp add: ex_graph_def ex_certs_def nodes_of_list_def deps_of_def)
  show "set (sources ex_graph) \<inter> set (topo ex_graph) = {}"
    by (simp add: ex_graph_def)
  show "distinct (topo ex_graph)"
    by (simp add: ex_graph_def)
qed

end
