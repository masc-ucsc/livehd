//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Lnast_ntype_id = uint8_t;
using Scope_id       = uint8_t;
using Rename_table = absl::flat_hash_map<std::string_view, u_int8_t >;
using Phi_tree = std::unique_ptr<Tree<std::vector<Token>>>;

struct Lnast_node {
  Lnast_ntype_id type; //not const as possible fake function call ...
  Token          token;
  Scope_id       scope;//SH:FIXME: deprecated, could rely on tree structure
  uint32_t       knum; //SH:FIXME: deprecated, record K number in cfg_text
  uint32_t       loc;  //SH:FIXME: wait for Akash
  uint16_t       subs; //ssa subscript

  Lnast_node(Lnast_ntype_id type, Token token)
    :type(type), token(token), scope(0), knum(0), loc(0), subs(0) { I(type);}

  Lnast_node(Lnast_ntype_id type, Token token, uint32_t knum)
    :type(type), token(token), scope(0), knum(knum), loc(0), subs(0) { I(type);}
};


class Language_neutral_ast : public Tree<Lnast_node> {
public:
  Language_neutral_ast() = default;
  explicit Language_neutral_ast(std::string_view _buffer): buffer(_buffer) { I(!buffer.empty());}
  void ssa_trans();

private:
  const std::string_view buffer;  // const because it can not change at runtime
  void do_ssa_trans(const Tree_index& top);
  void ssa_normal_subtree(const Tree_index& opr_node, Rename_table& rename_table, Phi_tree& phi_tree);
  void ssa_if_subtree    (const Tree_index& if_node,  Rename_table& rename_table, Phi_tree& phi_tree);
  bool elder_sibling_is_label(const Tree_index&);
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
  Lnast_ntype_for,
  Lnast_ntype_while,
  Lnast_ntype_func_call,    // .()
  Lnast_ntype_func_def,     // ::{   func_def = sub-graph in lgraph
  Lnast_ntype_top
};



