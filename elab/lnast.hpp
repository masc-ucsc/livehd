//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "tree.hpp"

using Lnast_ntype_id = Explicit_type<int8_t, struct Lnast_ntype_id_struct>;

struct Lnast_node {
  const Lnast_ntype_id   node_type;
  const Token_entry      token_entry;
  Lnast_node(Lnast_ntype_id node_type, Token_entry te) : node_type(node_type), token_entry(te) {
    I(node_type);
  }
};


class Language_neutral_ast : public Tree<Lnast_node> {
private:
protected:
public:
  //add_parent(Token_entry te);
  //potential_functions();
};


class Lnast_parser : public Elab_scanner {
private:
  std::unique_ptr<Language_neutral_ast> lnast;
protected:

public:
  void elaborate() final;
};
