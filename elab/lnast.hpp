//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

//using Lnast_ntype_id = Explicit_type<int8_t, struct Lnast_ntype_id_struct>;
using Lnast_ntype_id = uint8_t ;

struct Lnast_node {
  const Lnast_ntype_id   node_type;
  const Token_entry      token_entry;
  Lnast_node(Lnast_ntype_id node_type, Token_entry te) : node_type(node_type), token_entry(te) {
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
  Lnast_parser() = default;
private:
  std::unique_ptr<Language_neutral_ast> lnast;
  void elaborate();

  enum Lnast_node_type : Lnast_ntype_id {
    Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype_id
    Lnast_ntype_statement,
    Lnast_ntype_pure_assign,
    Lnast_ntype_dp_assign,    //dp = deprecate
    Lnast_ntype_as,
    Lnast_ntype_logical_and,
    Lnast_ntype_logical_or,
    Lnast_ntype_arith,
    Lnast_ntype_ref,
    Lnast_ntype_const,
    Lnast_ntype_attr_bits,
    Lnast_ntype_assert,
    Lnast_ntype_if,
    Lnast_ntype_uif,
    Lnast_ntype_tuple,
    Lnast_ntype_funccall,
    Lnast_ntype_funcdef,
    Lnast_ntype_sub,
    Lnast_ntype_top
  };
private:
};
