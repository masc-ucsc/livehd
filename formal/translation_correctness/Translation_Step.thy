theory Translation_Step
  imports Translation_Flops
begin

text \<open>
  Final composition theorem for one-cycle generated semantics.

  The generated step is correct once generated combinational outputs and
  generated next-state are each shown equivalent to their mathematical LGraph
  counterparts.
\<close>

definition generated_step ::
    "('i \<Rightarrow> 's \<Rightarrow> 's) \<Rightarrow> ('i \<Rightarrow> 's \<Rightarrow> 'o) \<Rightarrow>
     'i \<Rightarrow> 's \<Rightarrow> 's \<times> 'o" where
  "generated_step gen_next gen_comb i s = (gen_next i s, gen_comb i s)"

definition lgraph_step ::
    "('i \<Rightarrow> 's \<Rightarrow> 's) \<Rightarrow> ('i \<Rightarrow> 's \<Rightarrow> 'o) \<Rightarrow>
     'i \<Rightarrow> 's \<Rightarrow> 's \<times> 'o" where
  "lgraph_step spec_next spec_comb i s = (spec_next i s, spec_comb i s)"

theorem generated_step_equals_lgraph_step:
  assumes next_correct: "gen_next i s = spec_next i s"
  assumes comb_correct: "gen_comb i s = spec_comb i s"
  shows
    "generated_step gen_next gen_comb i s =
     lgraph_step spec_next spec_comb i s"
  using assms by (simp add: generated_step_def lgraph_step_def)

end
