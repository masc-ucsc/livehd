//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"

using Lnast_ntype_id = uint8_t;
using Scope_id       = uint8_t;

static inline constexpr int   CFG_NODE_NAME_POS           =  1;
static inline constexpr int   CFG_SCOPE_ID_POS            =  3;
static inline constexpr int   CFG_TOKEN_POS_BEG           =  4;
static inline constexpr int   CFG_TOKEN_POS_END           =  5;
static inline constexpr int   CFG_OP_POS_BEG              =  6;
static inline constexpr int   CFG_OP_FUNC_ROOT_RANGE      =  1; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_TARGET_TMP_REF_RANGE    =  1; //K14 K15  0 59 96   =  fun1  \___e


class Lnast_parser : public Elab_scanner {
public:
  Lnast_parser() : line_num(0) { setup_ntype_str_mapping();};
  const std::unique_ptr<Language_neutral_ast<Lnast_node>>&  get_ast(){return lnast;};
  std::string                                               ntype_dbg(Lnast_ntype_id ntype);
protected:


  void            elaborate() override;
  void            build_statements                  (const Tree_index& tree_idx_top, Scope_id scope);
  Scope_id        add_statement                     (const Tree_index& tree_idx_sts, Scope_id cur_scope);
  Scope_id        process_scope                     (const Tree_index& tree_idx_sts, Scope_id cur_scope );
  void            add_subgraph                      (const Tree_index& tree_idx_std, Scope_id new_scope, Scope_id cur_scope);
  void            process_assign_like_op            (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_function_name_replacement (const Tree_index& tree_idx_op, int& line_tkcnt);
  void            process_label_op                  (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_binary_op                 (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_func_call_op              (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_func_def_op               (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_if_op                     (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  Tree_index      add_operator_node                 (const Tree_index& tree_idx_sts, Token token, Lnast_ntype_id type, Scope_id cur_scope, uint32_t knum);
  void            add_operator_subtree              (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope);
  Lnast_ntype_id  operand_analysis();
  Lnast_ntype_id  operator_analysis(int& line_tkcnt);
  void            subgraph_scope_sync();
  void            setup_ntype_str_mapping();


private:
  std::unique_ptr<Language_neutral_ast<Lnast_node>>   lnast;
  absl::flat_hash_map<Lnast_ntype_id , std::string>   ntype2str;
  int  line_num;
};

