theory Translation_Combinational
  imports Main
begin

text \<open>
  Generic correctness skeleton for the combinational part of pass.isabelle.

  The generator emits a topologically ordered let-chain.  This theory proves
  that if each emitted node expression is locally correct whenever its internal
  dependencies are already correct, then evaluating the whole let-chain makes
  every emitted node equal to its mathematical LGraph denotation.
\<close>

type_synonym ('n, 'v) env = "'n \<Rightarrow> 'v"

definition env_correct_on ::
    "'n set \<Rightarrow> ('n, 'v) env \<Rightarrow> ('n \<Rightarrow> 'v) \<Rightarrow> bool" where
  "env_correct_on ns rho denote \<longleftrightarrow> (\<forall>n \<in> ns. rho n = denote n)"

definition bind_node ::
    "((('n, 'v) env) \<Rightarrow> 'n \<Rightarrow> 'v) \<Rightarrow> 'n \<Rightarrow>
     ('n, 'v) env \<Rightarrow> ('n, 'v) env" where
  "bind_node eval n rho = rho(n := eval rho n)"

fun eval_nodes ::
    "'n list \<Rightarrow> ((('n, 'v) env) \<Rightarrow> 'n \<Rightarrow> 'v) \<Rightarrow>
     ('n, 'v) env \<Rightarrow> ('n, 'v) env" where
  "eval_nodes [] eval rho = rho"
| "eval_nodes (n # ns) eval rho =
     eval_nodes ns eval (bind_node eval n rho)"

fun topo_acc_ok :: "('n \<Rightarrow> 'n list) \<Rightarrow> 'n list \<Rightarrow> 'n list \<Rightarrow> bool" where
  "topo_acc_ok deps prefix [] = True"
| "topo_acc_ok deps prefix (n # ns) =
     (set (deps n) \<subseteq> set prefix \<and> topo_acc_ok deps (prefix @ [n]) ns)"

definition topo_ok :: "('n \<Rightarrow> 'n list) \<Rightarrow> 'n list \<Rightarrow> bool" where
  "topo_ok deps topo \<longleftrightarrow> topo_acc_ok deps [] topo"

lemma env_correct_on_mono:
  assumes "env_correct_on b rho denote"
  assumes "a \<subseteq> b"
  shows "env_correct_on a rho denote"
  using assms by (auto simp: env_correct_on_def)

lemma env_correct_on_bind_fresh:
  assumes corr: "env_correct_on a rho denote"
  assumes fresh: "n \<notin> a"
  shows "env_correct_on a (bind_node eval n rho) denote"
  using assms by (auto simp: env_correct_on_def bind_node_def)

lemma env_correct_on_bind_insert:
  assumes corr: "env_correct_on a rho denote"
  assumes fresh: "n \<notin> a"
  assumes node: "eval rho n = denote n"
  shows "env_correct_on (insert n a) (bind_node eval n rho) denote"
  using assms by (auto simp: env_correct_on_def bind_node_def)

lemma eval_nodes_correct_acc:
  assumes topo: "topo_acc_ok deps prefix todo"
  assumes distinct: "distinct (prefix @ todo)"
  assumes prefix_corr: "env_correct_on (set prefix) rho denote"
  assumes local_correct:
    "\<And>prefix' rho' n.
       n \<in> set todo \<Longrightarrow>
       set (deps n) \<subseteq> set prefix' \<Longrightarrow>
       env_correct_on (set (deps n)) rho' denote \<Longrightarrow>
       eval rho' n = denote n"
  shows "env_correct_on (set prefix \<union> set todo)
           (eval_nodes todo eval rho) denote"
  using topo distinct prefix_corr local_correct
proof (induct todo arbitrary: prefix rho)
  case Nil
  then show ?case
    by (simp add: env_correct_on_def)
next
  case (Cons n ns)
  have deps_subset: "set (deps n) \<subseteq> set prefix"
    using Cons.prems(1) by simp
  have n_fresh: "n \<notin> set prefix"
    using Cons.prems(2) by simp
  have deps_corr: "env_correct_on (set (deps n)) rho denote"
    using Cons.prems(3) deps_subset by (rule env_correct_on_mono)
  have n_eval: "eval rho n = denote n"
    using Cons.prems(4)[of n prefix rho] deps_subset deps_corr by simp
  have prefix_n_corr:
    "env_correct_on (set (prefix @ [n])) (bind_node eval n rho) denote"
    using Cons.prems(3) n_fresh n_eval
    by (auto simp: env_correct_on_def bind_node_def)
  have rec:
    "env_correct_on (set (prefix @ [n]) \<union> set ns)
       (eval_nodes ns eval (bind_node eval n rho)) denote"
  proof (rule Cons.hyps)
    show "topo_acc_ok deps (prefix @ [n]) ns"
      using Cons.prems(1) by simp
    show "distinct ((prefix @ [n]) @ ns)"
      using Cons.prems(2) by simp
    show "env_correct_on (set (prefix @ [n])) (bind_node eval n rho) denote"
      using prefix_n_corr .
    show "\<And>prefix' rho' m.
       m \<in> set ns \<Longrightarrow>
       set (deps m) \<subseteq> set prefix' \<Longrightarrow>
       env_correct_on (set (deps m)) rho' denote \<Longrightarrow>
       eval rho' m = denote m"
      using Cons.prems(4) by simp
  qed
  then show ?case
    by (simp add: Un_assoc Un_commute Un_left_commute)
qed

theorem generated_comb_equals_lgraph_denotation:
  assumes topo: "topo_ok deps topo"
  assumes distinct: "distinct topo"
  assumes local_correct:
    "\<And>prefix rho n.
       n \<in> set topo \<Longrightarrow>
       set (deps n) \<subseteq> set prefix \<Longrightarrow>
       env_correct_on (set (deps n)) rho denote \<Longrightarrow>
       eval rho n = denote n"
  shows "env_correct_on (set topo)
           (eval_nodes topo eval source_env) denote"
proof -
  have "env_correct_on (set [] \<union> set topo)
          (eval_nodes topo eval source_env) denote"
  proof (rule eval_nodes_correct_acc)
    show "topo_acc_ok deps [] topo"
      using topo by (simp add: topo_ok_def)
    show "distinct ([] @ topo)"
      using distinct by simp
    show "env_correct_on (set []) source_env denote"
      by (simp add: env_correct_on_def)
    show "\<And>prefix rho n.
       n \<in> set topo \<Longrightarrow>
       set (deps n) \<subseteq> set prefix \<Longrightarrow>
       env_correct_on (set (deps n)) rho denote \<Longrightarrow>
       eval rho n = denote n"
      using local_correct by blast
  qed
  then show ?thesis by simp
qed

lemma generated_comb_equals_lgraph_denotation_from_certificates:
  assumes node_certificates:
    "\<And>n. n \<in> set topo \<Longrightarrow>
       eval_nodes topo eval source_env n = denote n"
  shows "env_correct_on (set topo)
           (eval_nodes topo eval source_env) denote"
  using node_certificates by (simp add: env_correct_on_def)

lemma generated_comb_outputs_correct:
  assumes nodes:
    "env_correct_on (set topo) (eval_nodes topo eval source_env) denote"
  assumes output_correct:
    "\<And>rho. env_correct_on (set topo) rho denote \<Longrightarrow>
          generated_outputs rho = lgraph_outputs"
  shows "generated_outputs (eval_nodes topo eval source_env) = lgraph_outputs"
  using assms by blast

end
