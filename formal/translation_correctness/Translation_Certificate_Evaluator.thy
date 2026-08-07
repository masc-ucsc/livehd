theory Translation_Certificate_Evaluator
  imports
    Translation_LGraph_Model
    Translation_Combinational
begin

text \<open>
  Generic graph-certificate evaluator theorem.

  The certificate denotation is a separate topological fold that interprets
  nodes through @{const denote_op}.  The executable evaluator uses
  @{const eval_op}.  The generic theorem below connects the two once and avoids
  generated per-node local correctness lemmas.
\<close>

definition graph_denotation ::
  "nat list \<Rightarrow> graph_cert \<Rightarrow> (nat \<Rightarrow> bv) \<Rightarrow> nat \<Rightarrow> bv" where
  "graph_denotation order G source_env = denote_graph order G source_env"

lemma eval_graph_eq_eval_nodes:
  "eval_graph xs G rho = eval_nodes xs (eval_node G) rho"
  by (induct xs arbitrary: rho) (auto simp: bind_node_def)

theorem eval_graph_correct:
  "env_correct_on (set order)
     (eval_graph order G source_env)
     (graph_denotation order G source_env)"
  by (simp add: env_correct_on_def graph_denotation_def eval_graph_eq_denote_graph)

theorem eval_graph_correct_for_cert:
  "env_correct_on (set (topo G))
     (eval_graph (topo G) G source_env)
     (graph_denotation (topo G) G source_env)"
  by (rule eval_graph_correct)

end
