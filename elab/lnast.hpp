//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Lnast_ntype_id = uint8_t ;
//using Scope_id       = Explicit_type<uint8_t, struct Scope_ID_struct>;
using Scope_id       = uint8_t ;

static inline constexpr int   CFG_NODE_NAME_POS    = 1;
static inline constexpr int   CFG_SCOPE_ID_POS     = 3;
static inline constexpr int   CFG_TOKEN_POS_BEG    = 4;
static inline constexpr int   CFG_TOKEN_POS_END    = 5;
static inline constexpr int   CFG_OP_POS_BEG       = 6;

struct Lnast_node {
  const Lnast_ntype_id   node_type;
  const Token_entry      node_name;
  const Scope_id         scope;
  Lnast_node(Lnast_ntype_id node_type, Token_entry te, Scope_id scope):node_type(node_type), node_name(te), scope(scope) {
    I(node_type);
  }
};


class Language_neutral_ast : public Tree<Lnast_node> {
public:
  Language_neutral_ast() = default;
  Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top);
  //add_parent(Token_entry te);
  //potential_functions();
private:
  const std::string_view buffer;  // const because it can not change at runtime
protected:
};


class Lnast_parser : public Elab_scanner {
public:
  Lnast_parser() : line_num(0) {};
protected:
  enum Lnast_node_type : Lnast_ntype_id {
    Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype_id
    Lnast_ntype_statement,
    Lnast_ntype_pure_assign,  // =
    Lnast_ntype_dp_assign,    // :=, dp = deprecate
    Lnast_ntype_as,           // as
    Lnast_ntype_lable,        // :
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


  void         elaborate() override;
  void         build_statements(Scope_id scope);
  void         add_statement(const Tree_index& statements, Scope_id cur_scope);
  void         add_subgraph();
  void         add_operator_subtree(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope);
  Scope_id     process_scope(Scope_id cur_scope);
  void         operator_analysis(Lnast_ntype_id & node_type, int& line_tkcnt);

private:
  std::unique_ptr<Language_neutral_ast>  lnast;
  int                                    line_num;
};
