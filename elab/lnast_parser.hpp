//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"

static inline constexpr int   CFG_IDX_POS              =  1;
static inline constexpr int   CFG_PARENT_POS           =  2;
static inline constexpr int   CFG_CHILD_POS            =  3;
static inline constexpr int   CFG_TOKEN_POS_BEG        =  4;
static inline constexpr int   CFG_TOKEN_POS_END        =  5;
static inline constexpr int   CFG_OP_POS               =  6;
static inline constexpr int   CFG_OP_FUNC_ROOT_RANGE   =  1; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_TARGET_TMP_REF_RANGE =  1; //K14 K15  0 59 96   =  fun1  \___e


class Lnast_parser : public Elab_scanner {
protected:
  void set_module_name(std::string_view filename);

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
  bool         function_name_correction (Lnast_ntype type, const Token& target_name);
  mmap_lib::Tree_index process_operator_node (const mmap_lib::Tree_index& opr_parent_sts, Lnast_ntype type, uint32_t , const Token& target_name);
  void         walk_next_token() {scan_next(); line_tkcnt += 1;              };
  void         walk_next_line()  {scan_next(); line_tkcnt = 1 ; line_num += 1; };

private:
  Lnast                  lnast;
  uint32_t               line_num;
  uint8_t                line_tkcnt;
  Token                  buffer_if_condition;
  bool                   buffer_if_condition_used;
  mmap_lib::Tree_index   buffer_tmp_func_name_idx;
  absl::flat_hash_map<uint32_t, mmap_lib::Tree_index> cfg_parent_id2lnast_node; //translate the parent column idx to corresponding sts node

  std::string get_module_name(std::string_view filename);

public:
  Lnast_parser();
  Lnast_parser(std::string_view file);
  Lnast_parser(std::string_view _top_module_name, std::string_view _text);

  Lnast *ref_lnast() { return &lnast;};
};

