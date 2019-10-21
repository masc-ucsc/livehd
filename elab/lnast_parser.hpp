//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "lnast.hpp"

using Lnast_ntype = uint8_t;

static inline constexpr int   CFG_IDX_POS              =  1;
static inline constexpr int   CFG_PARENT_POS           =  2;
static inline constexpr int   CFG_CHILD_POS            =  3;
static inline constexpr int   CFG_TOKEN_POS_BEG        =  4;
static inline constexpr int   CFG_TOKEN_POS_END        =  5;
static inline constexpr int   CFG_OP_POS_BEG           =  6;
static inline constexpr int   CFG_OP_FUNC_ROOT_RANGE   =  1; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_TARGET_TMP_REF_RANGE =  1; //K14 K15  0 59 96   =  fun1  \___e


class Lnast_parser : public Elab_scanner {
public:
  Lnast_parser() : line_num(0), line_tkcnt(0){ setup_ntype_str_mapping();};
  const std::unique_ptr<Lnast>&  get_ast(){return lnast;};
  std::string                    ntype_dbg(Lnast_ntype ntype);

protected:
  void         elaborate() override;
  void         process_statements_op   (const mmap_lib::Tree_index& tree_idx_top, uint32_t);
  void         build_lnast();
  void         process_assign_like_op  (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         process_label_op        (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         process_binary_op       (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         process_func_call_op    (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         process_func_def_op     (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         process_if_op           (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  void         add_operator_subtree    (const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name);
  Lnast_ntype  operand_analysis();
  Lnast_ntype  operator_analysis();
  bool         token_is_valid_ref();
  void         setup_ntype_str_mapping();
  void         function_name_correction(Lnast_ntype type, const mmap_lib::Tree_index& sts_idx);
  mmap_lib::Tree_index process_operator_node             (const mmap_lib::Tree_index& opr_parent_sts, Lnast_ntype type);

private:
  std::unique_ptr<Lnast> lnast;
  uint32_t               line_num;
  uint8_t                line_tkcnt;
  mmap_lib::Tree_index   buffer_next_sts_parent;
  absl::flat_hash_map<Lnast_ntype, std::string> ntype2str;
  absl::flat_hash_map<uint32_t, mmap_lib::Tree_index> cfg_idx2sts_node;
};

