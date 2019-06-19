//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Lnast_ntype_id = uint8_t;
using Scope_id       = uint8_t;

static inline constexpr int   CFG_NODE_NAME_POS           =  1;
static inline constexpr int   CFG_SCOPE_ID_POS            =  3;
static inline constexpr int   CFG_TOKEN_POS_BEG           =  4;
static inline constexpr int   CFG_TOKEN_POS_END           =  5;
static inline constexpr int   CFG_OP_POS_BEG              =  6;
static inline constexpr int   CFG_SCOPE_OP_TOKEN1ST_RANGE = -7; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_OP_FUNC_ROOT_RANGE      =  2; //K9  K14  0 59 96 ::{  ___e  K11  $a  $b  %o
static inline constexpr int   CFG_OP_FUNC_TMP_REF_RANGE   =  2; //K14 K15  0 59 96   =  fun1  \___e


struct Lnast_node {
  const Lnast_ntype_id   node_type;
  Scope_id               scope;
  Token                  node_token;
  Lnast_node(Lnast_ntype_id node_type, Token token, Scope_id scope):node_type(node_type), node_token(token), scope(scope) {
    I(node_type);
  }
};


class Language_neutral_ast : public Tree<Lnast_node> {
public:
  Language_neutral_ast() = default;
  Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top);
  ssa_transform();


private:
  const std::string_view buffer;  // const because it can not change at runtime
  add_phi_nodes();
  renaming();
protected:
};


class Lnast_parser : public Elab_scanner {
public:
  Lnast_parser() : line_num(0) { setup_ntype_str_mapping();};
  const std::unique_ptr<Language_neutral_ast>&  get_ast(){return lnast;};
  std::string     ntype_dbg(Lnast_ntype_id ntype);
protected:

  enum Lnast_node_type : Lnast_ntype_id {
    Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype_id
    Lnast_ntype_statement,
    Lnast_ntype_pure_assign,  // =
    Lnast_ntype_dp_assign,    // :=, dp = deprecate
    Lnast_ntype_as,           // as
    Lnast_ntype_label,        // :
    Lnast_ntype_dot,          // .
    Lnast_ntype_logical_and,  // and
    Lnast_ntype_logical_or,   // or
    Lnast_ntype_and,
    Lnast_ntype_or,
    Lnast_ntype_xor,
    Lnast_ntype_plus,
    Lnast_ntype_minus,
    Lnast_ntype_mult,
    Lnast_ntype_div,
    Lnast_ntype_eq,
    Lnast_ntype_lt,
    Lnast_ntype_le,
    Lnast_ntype_gt,
    Lnast_ntype_ge,
    Lnast_ntype_tuple,        // ()
    Lnast_ntype_ref,
    Lnast_ntype_const,
    Lnast_ntype_input,
    Lnast_ntype_output,
    Lnast_ntype_reg,
    Lnast_ntype_attr_bits,    // __bits
    Lnast_ntype_assert,       // I
    Lnast_ntype_if,
    Lnast_ntype_uif,
    Lnast_ntype_for,
    Lnast_ntype_while,
    Lnast_ntype_func_call,    // .()
    Lnast_ntype_func_def,     // ::{
    Lnast_ntype_sub,
    Lnast_ntype_top
  };

  void            elaborate() override;
  void            build_statements                  (const Tree_index& tree_idx_top, Scope_id scope);
  Scope_id        add_statement                     (const Tree_index& tree_idx_sts, Scope_id cur_scope);
  Scope_id        process_scope                     (const Tree_index& tree_idx_sts, Scope_id cur_scope );
  void            add_subgraph                      (const Tree_index& tree_idx_std, Scope_id new_scope, Scope_id cur_scope);
  void            process_assign_like_op            (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_function_name_replacement (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_label_op                  (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_binary_op                 (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_func_call_op              (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_func_def_op               (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  void            process_if_op                     (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id);
  Tree_index      add_operator_node                 (const Tree_index& tree_idx_sts, Token node_token, Lnast_ntype_id node_type, Scope_id cur_scope);
  void            add_operator_subtree              (const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope);
  Lnast_ntype_id  operand_analysis();
  Lnast_ntype_id  operator_analysis(int& line_tkcnt);
  void            subgraph_scope_sync();
  void            setup_ntype_str_mapping();

private:
  std::unique_ptr<Language_neutral_ast>             lnast;
  int                                               line_num;
  absl::flat_hash_map<Lnast_ntype_id , std::string> ntype2str;
};
