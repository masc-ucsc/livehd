//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/os.h>

#include <deque>
#include <iostream>
#include <stack>

#include "lhtree.hpp"
#include "lnast.hpp"
#include "lnast_lexer.hpp"

class Lnast_parser {
public:
  explicit Lnast_parser(std::istream&);
  std::shared_ptr<Lnast> parse_all();

protected:
  std::shared_ptr<Lnast>       lnast;
  std::unique_ptr<Lnast_lexer> lexer;
  std::istream&                is;

  std::vector<Lnast_token> token_array;

  int                        token_index;
  std::stack<lh::Tree_index> tree_index;

  void read_all_tokens() {
    do {
      token_array.push_back(lexer->lex_token());
      // fmt::print("{}\n", token_array.back().get_string());
    } while (token_array.back().get_kind() != Lnast_token::eof);
  }

  inline void forward_token() { ++token_index; }
  inline void rewind_token() { --token_index; }

  inline auto cur_token() { return token_array[token_index]; }
  inline auto cur_kind() { return token_array[token_index].get_kind(); }
  inline auto cur_text() { return token_array[token_index].get_text(); }

  void add_leaf(Lnast_node n) { lnast->add_child(tree_index.top(), n); }

  void start_tree(Lnast_node n) {
    auto i = lnast->add_child(tree_index.top(), n);
    tree_index.push(i);
  }

  void end_tree() { tree_index.pop(); }

  void error() {
    // TODO: Populate error handling/diagnostic code
    fmt::print("ERROR!");
    forward_token();
  }

  void parse_top();
  void parse_stmts();
  void parse_stmt();
  void parse_var_stmt();
  void parse_fun_stmt();
  void parse_if_stmt();
  // void parse_uif_stmt();
  // void parse_for_stmt();
  void parse_list();
  void parse_type_list();
  void parse_selects();
  void parse_prim();
  void parse_type();
};
