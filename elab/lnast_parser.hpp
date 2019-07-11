//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "lnast.hpp"

using Lnast_ntype_id = uint8_t;
using Scope_id       = uint8_t;

static inline constexpr int   CFG_KNUM1_POS            =  1;
static inline constexpr int   CFG_KNUM2_POS            =  2;
static inline constexpr int   CFG_SCOPE_ID_POS         =  3;
static inline constexpr int   CFG_TOKEN_POS_BEG        =  4;
static inline constexpr int   CFG_TOKEN_POS_END        =  5;
static inline constexpr int   CFG_OP_POS_BEG           =  6;
static inline constexpr int   CFG_OP_FUNC_ROOT_RANGE   =  1; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_TARGET_TMP_REF_RANGE =  1; //K14 K15  0 59 96   =  fun1  \___e


class Lnast_parser : public Elab_scanner {
public:
  Lnast_parser() : line_num(0), line_tkcnt(0), knum1(0), knum2(0) { setup_ntype_str_mapping();};
  const std::unique_ptr<Language_neutral_ast<Lnast_node>>&  get_ast(){return lnast;};
  std::string                                               ntype_dbg(Lnast_ntype_id ntype);

protected:
  void       elaborate() override;
  void       build_top_statements              (const Tree_index& tree_idx_top);
  void       add_statement                     (const Tree_index& tree_top_sts);
  void       process_assign_like_op            (const Tree_index& tree_idx_opr, const Token& target_name);
  void       process_label_op                  (const Tree_index& tree_idx_opr, const Token& target_name);
  void       process_binary_op                 (const Tree_index& tree_idx_opr, const Token& target_name);
  void       process_func_call_op              (const Tree_index& tree_idx_opr, const Token& target_name);
  void       process_func_def_op               (const Tree_index& tree_idx_opr, const Token& target_name);
  void       process_if_op                     (const Tree_index& tree_idx_opr, const Token& target_name);
  void       add_operator_subtree              (const Tree_index& tree_idx_opr, const Token& target_name);
  Tree_index process_operator_node             (const Tree_index& opr_parent_sts, Lnast_ntype_id type);
  Scope_id        process_scope();
  Lnast_ntype_id  operand_analysis();
  Lnast_ntype_id  operator_analysis();
  bool            token_is_valid_ref();
  void            setup_ntype_str_mapping();
  void            function_name_correction(Lnast_ntype_id type, const Tree_index& sts_idx);
  void            process_if_else_sts_range(const Tree_index& tree_idx_if);
  void            final_else_sts_type_correction();
private:
  std::unique_ptr<Language_neutral_ast<Lnast_node>>   lnast;
  absl::flat_hash_map<Lnast_ntype_id , std::string>   ntype2str;
  uint32_t line_num;
  uint8_t  line_tkcnt;
  uint32_t knum1;
  uint32_t knum2;//for if-else or fdef chunk
  std::vector<std::tuple<Tree_index, Lnast_ntype_id, uint32_t, uint32_t>> range_stack; //(parent_sts, sts_type, min, max)
};

