//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Lnast_ntype_id = uint8_t;
using Scope_id       = uint8_t;


//SH:FIXME: temporarily replace Token to std::string for lnast nested if-else golden construction
//SH:FIXME: should be deprecated after Akash finish his part.
struct Lnast_node_str {
  const Lnast_ntype_id   type;
  std::string            token;
  Scope_id               scope;
  uint32_t               sbs; //ssa subscript
  Lnast_node_str(Lnast_ntype_id type, std::string token, Scope_id scope):type(type), token(token), scope(scope), sbs(0) {
    I(type);
  }
};

struct Lnast_node {
  const Lnast_ntype_id   type;
  Token                  token;
  Scope_id               scope;
  uint32_t               sbs; //ssa subscript
  Lnast_node(Lnast_ntype_id type, Token token, Scope_id scope):type(type), token(token), scope(scope), sbs(0) {
    I(type);
  }
};


template <typename X>
class Language_neutral_ast : public Tree<X> {
public:
  Language_neutral_ast() = default;
  explicit Language_neutral_ast(std::string_view _buffer): buffer(_buffer) {I(!buffer.empty());}
  void ssa_trans();

private:
  const std::string_view buffer;  // const because it can not change at runtime
  void            add_phi_nodes();
  void            renaming();
protected:
};

template <typename X>
void Language_neutral_ast<X>::ssa_trans() {
  add_phi_nodes();
  renaming();
}

template <typename X>
void Language_neutral_ast<X>::add_phi_nodes() {
  ;
}

template <typename X>
void Language_neutral_ast<X>::renaming() {
  ;
}


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
  Lnast_ntype_cond,
  Lnast_ntype_else,
  Lnast_ntype_uif,
  Lnast_ntype_for,
  Lnast_ntype_while,
  Lnast_ntype_func_call,    // .()
  Lnast_ntype_func_def,     // ::{
  Lnast_ntype_sub,
  Lnast_ntype_top
};
