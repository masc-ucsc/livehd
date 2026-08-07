theory Translation_Flops
  imports
    Translation_Combinational
    "DINO_Semantic_Primitives_Test.SemanticPrimitives"
begin

text \<open>
  Generic correctness skeleton for the sequential boundary of pass.isabelle.

  A generated \<open><top>_next\<close> updates every state field using the same
  total helper as the LGraph flop semantics: reset has priority, then enable,
  then old-state preservation.
\<close>

locale flop_translation =
  fixes flops :: "'f list"
  fixes read_field :: "'f \<Rightarrow> 's \<Rightarrow> 'v"
  fixes update_all :: "('f \<Rightarrow> 'v) \<Rightarrow> 's \<Rightarrow> 's"
  assumes state_eqI:
    "\<And>s t. (\<And>f. f \<in> set flops \<Longrightarrow> read_field f s = read_field f t)
      \<Longrightarrow> s = t"
  assumes read_update_all:
    "\<And>vals s f. f \<in> set flops \<Longrightarrow>
      read_field f (update_all vals s) = vals f"
begin

definition generated_next ::
    "('i \<Rightarrow> 's \<Rightarrow> 'f \<Rightarrow> 'v) \<Rightarrow> 'i \<Rightarrow> 's \<Rightarrow> 's" where
  "generated_next gen i s = update_all (gen i s) s"

definition lgraph_flop_step ::
    "('i \<Rightarrow> 's \<Rightarrow> 'f \<Rightarrow> 'v) \<Rightarrow> 'i \<Rightarrow> 's \<Rightarrow> 's" where
  "lgraph_flop_step spec i s = update_all (spec i s) s"

theorem generated_next_equals_lgraph_flop_step:
  assumes per_flop:
    "\<And>f. f \<in> set flops \<Longrightarrow> gen i s f = spec i s f"
  shows "generated_next gen i s = lgraph_flop_step spec i s"
  unfolding generated_next_def lgraph_flop_step_def
  by (rule state_eqI) (simp add: read_update_all per_flop)

end

definition lgraph_flop_next ::
    "bool \<Rightarrow> 'w::len word \<Rightarrow> bool \<Rightarrow> 'w word \<Rightarrow> 'w word \<Rightarrow>
     'w word" where
  "lgraph_flop_next reset_active reset_value enable_active din current =
     (if reset_active then reset_value
      else if enable_active then din
      else current)"

lemma generated_flop_next_equals_lgraph_flop_next:
  "flop_next reset_active reset_value enable_active din current =
   lgraph_flop_next reset_active reset_value enable_active din current"
  by (simp add: flop_next_def lgraph_flop_next_def)

lemma generated_flop_next_reset:
  "flop_next True reset_value enable_active din current = reset_value"
  by (simp add: flop_next_def)

lemma generated_flop_next_enable:
  "flop_next False reset_value True din current = din"
  by (simp add: flop_next_def)

lemma generated_flop_next_preserve:
  "flop_next False reset_value False din current = current"
  by (simp add: flop_next_def)

end
