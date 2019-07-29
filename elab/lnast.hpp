//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Scope_id       = uint8_t;
using Rename_table   = absl::flat_hash_map<std::string_view, u_int8_t >;
using Lnast_ntype_id = uint8_t;
using Lnast_index    = Tree_index;
using Phi_tree_table = absl::flat_hash_map<std::string_view, Lnast_index>;
using Phi_tree       = Tree<Phi_tree_table>;
using Phi_tree_index = Tree_index; //SH:FIXME:buggy but ....

struct Lnast_node {
  Lnast_ntype_id type; //not const as possible fake function call ...
  Token          token;
  Scope_id       scope;//SH:FIXME: deprecated, could rely on tree structure
  uint32_t       knum; //SH:FIXME: deprecated, record K number in cfg_text
  uint32_t       loc;  //SH:FIXME: wait for Akash
  uint16_t       subs; //ssa subscript

  Lnast_node(Lnast_ntype_id type, Token token)
    :type(type), token(token), scope(0), knum(0), loc(0), subs(0) { I(type);}

  Lnast_node(Lnast_ntype_id type, Token token, uint16_t subs)
    :type(type), token(token), scope(0), knum(0), loc(0), subs(subs) { I(type);}

  //Lnast_node(Lnast_ntype_id type, Token token, uint32_t knum)
  //  :type(type), token(token), scope(0), knum(knum), loc(0), subs(0) { I(type);}
};


class Lnast : public Tree<Lnast_node> {
public:
  Lnast() = default;
  explicit Lnast(std::string_view _buffer): buffer(_buffer) { I(!buffer.empty());}
  void ssa_trans();

private:
  const std::string_view buffer;  // const because it can not change at runtime
  void do_ssa_trans          (const Lnast_index &top);
  void ssa_normal_subtree    (const Lnast_index &opr_node, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_sts_idx);
  void ssa_if_subtree        (const Lnast_index &if_node,  Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx);
  void phi_node_insertion    (const Lnast_index &if_node,  Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx);
  bool check_else_block_existence(const Lnast_index &if_node);
  Lnast_index get_complement_lnast_idx (std::string_view lnast_var, const Phi_tree_table &phi_complement_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx);
  Lnast_index get_complement_lnast_idx_from_parent (std::string_view lnast_var, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx);
  bool        elder_sibling_is_label(const Lnast_index &self);
  bool        elder_sibling_is_cond (const Lnast_index &self);
  Lnast_index get_elder_sibling     (const Lnast_index &self);
protected:
};


enum Lnast_node_type : Lnast_ntype_id {
  Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype_id
  Lnast_ntype_statements,
  Lnast_ntype_cstatements,  // statement for condition determination, ex: if ((foo+1) > 3) { ... }
  Lnast_ntype_pure_assign,  // =
  Lnast_ntype_dp_assign,    // :=, dp = deprecate
  Lnast_ntype_as,           // as
  Lnast_ntype_label,        // :
  Lnast_ntype_dot,          // .
  Lnast_ntype_logical_and,  // and
  Lnast_ntype_logical_or,   // or
  Lnast_ntype_and,          // &
  Lnast_ntype_or,           // |
  Lnast_ntype_xor,          // ^
  Lnast_ntype_plus,
  Lnast_ntype_minus,
  Lnast_ntype_mult,
  Lnast_ntype_div,
  Lnast_ntype_eq,
  Lnast_ntype_same,
  Lnast_ntype_lt,
  Lnast_ntype_le,
  Lnast_ntype_gt,
  Lnast_ntype_ge,
  Lnast_ntype_tuple,        // ()
  Lnast_ntype_ref,
  Lnast_ntype_const,
  Lnast_ntype_attr_bits,    // __bits
  Lnast_ntype_assert,       // I
  Lnast_ntype_if,
  Lnast_ntype_cond,
  Lnast_ntype_else,
  Lnast_ntype_uif,
  Lnast_ntype_phi,
  Lnast_ntype_for,
  Lnast_ntype_while,
  Lnast_ntype_func_call,    // .()
  Lnast_ntype_func_def,     // ::{   func_def = sub-graph in lgraph
  Lnast_ntype_top
};



