//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_parser.hpp"

#include "lhtree.hpp"
#include "lnast.hpp"
#include "lnast_lexer.hpp"

Lnast_parser::Lnast_parser(std::istream &_is) : is(_is) {}

std::shared_ptr<Lnast> Lnast_parser::parse_all() {
  lnast = std::make_unique<Lnast>();
  lexer = std::make_unique<Lnast_lexer>(is);

  read_all_tokens();
  token_index = 0;
  parse_top();

  lexer = nullptr;
  return lnast;
}

void Lnast_parser::parse_top() {
  // std::cout << "parse_top\n";
  lnast->set_root(Lnast_node::create_top());
  tree_index.push(lh::Tree_index::root());
  parse_stmts();
}

void Lnast_parser::parse_stmts() {
  // std::cout << "parse_stmts\n";
  start_tree(Lnast_node::create_stmts());
  forward_token();  // '{'
  while (cur_kind() != Lnast_token::rbrace) {
    parse_stmt();
  }
  end_tree();
  forward_token();
}

void Lnast_parser::parse_stmt() {
  // std::cout << "parse_stmt\n";
  switch (cur_kind()) {
    case Lnast_token::id_var: return parse_var_stmt();
    case Lnast_token::id_fun: return parse_fun_stmt();
    case Lnast_token::kw_if: return parse_if_stmt();
    default: error();
  }
}

void Lnast_parser::parse_var_stmt() {
  // std::cout << "parse_var_stmt\n";
  auto lhs_node = Lnast_node::create_ref(cur_text());
  forward_token();
  switch (cur_kind()) {
    case Lnast_token::equal: {
      forward_token();
      switch (cur_kind()) {
        case Lnast_token::lparen: {
          // LNAST - tuple_add
          start_tree(Lnast_node::create_tuple_add());
          add_leaf(lhs_node);
          parse_list();
          end_tree();
          break;
        }
        case Lnast_token::id_var: {
          auto rhs_node = Lnast_node::create_ref(cur_text());
          forward_token();
          if (cur_kind() == Lnast_token::lbrack) {
            // LNAST - tuple_get
            start_tree(Lnast_node::create_tuple_get());
            add_leaf(lhs_node);
            add_leaf(rhs_node);
            parse_selects();
            end_tree();
          } else if (cur_kind() == Lnast_token::dot) {
            // LNAST - attr_get
            start_tree(Lnast_node::create_attr_get());
            add_leaf(lhs_node);
            add_leaf(rhs_node);
            parse_attr();
            end_tree();
          } else {
            // LNAST - assign
            start_tree(Lnast_node::create_assign());
            add_leaf(lhs_node);
            add_leaf(rhs_node);
            end_tree();
          }
          // TODO: add attr_set
          break;
        }
        case Lnast_token::id_fun: {
          // LNAST - func_call
          start_tree(Lnast_node::create_func_call());
          add_leaf(lhs_node);
          add_leaf(Lnast_node::create_ref(cur_text()));
          forward_token();
          parse_list();
          end_tree();
          break;
        }
        case Lnast_token::number:
        case Lnast_token::string: {
          auto rhs_node = Lnast_node::create_const(cur_text());
          forward_token();
          if (cur_kind() == Lnast_token::lparen) {
            // LNAST - func_call
            start_tree(Lnast_node::create_func_call());
            add_leaf(lhs_node);
            add_leaf(rhs_node);
            parse_list();
            end_tree();
          } else {
            // LNAST - assign
            start_tree(Lnast_node::create_assign());
            add_leaf(lhs_node);
            add_leaf(rhs_node);
            end_tree();
          }
          break;
        }
#define TOKEN_FN(NAME, SPELLING)             \
  case Lnast_token::fn_##NAME: {             \
    start_tree(Lnast_node::create_##NAME()); \
    add_leaf(lhs_node);                      \
    forward_token();                         \
    parse_list();                            \
    end_tree();                              \
    break;                                   \
  }
#include "lnast_tokens.def"
        default: error();
      }
      break;
    }
    case Lnast_token::lbrack: {
      // LNAST - tuple_set
      start_tree(Lnast_node::create_tuple_set());
      add_leaf(lhs_node);
      parse_selects();
      if (cur_kind() != Lnast_token::equal) {
        error();
      }
      forward_token();  // '='
      parse_prim();
      end_tree();
      break;
    }
    case Lnast_token::dot: {
      start_tree(Lnast_node::create_attr_set());
      add_leaf(lhs_node);
      parse_dot_selects();
      if (cur_kind() != Lnast_token::equal) {
        error();
      }
      forward_token();
      parse_prim();
      end_tree();
      break;
    }
    case Lnast_token::colon: {
      // LNAST - type_spec
      start_tree(Lnast_node::create_type_spec());
      add_leaf(lhs_node);
      forward_token();
      parse_type();
      end_tree();
      break;
    }

    default: error();
  }
}

void Lnast_parser::parse_list() {
  // std::cout << "parse_list\n";
  forward_token();
  while (cur_kind() != Lnast_token::rparen) {
    switch (cur_kind()) {
      case Lnast_token::comma: {
        forward_token();
        continue;
      }
      case Lnast_token::id_var: {
        auto name      = cur_text();
        bool has_value = false;
        bool has_type  = false;
        forward_token();
        if (cur_kind() == Lnast_token::equal) {
          has_value = true;
          forward_token();
          start_tree(Lnast_node::create_assign());
          add_leaf(Lnast_node::create_ref(name));
          parse_prim();
        }
        if (cur_kind() == Lnast_token::colon) {
          has_type = true;
          forward_token();
          if (has_value) {
            parse_type();
          } else {
            start_tree(Lnast_node::create_type_spec());
            add_leaf(Lnast_node::create_ref(name));
            parse_type();
          }
        }
        if (has_value || has_type) {
          end_tree();
        } else {
          add_leaf(Lnast_node::create_ref(name));
        }
        continue;
      }
      case Lnast_token::number:
      case Lnast_token::string: {
        auto value = cur_text();
        forward_token();
        if (cur_kind() == Lnast_token::colon) {
          forward_token();
          start_tree(Lnast_node::create_type_spec());
          add_leaf(Lnast_node::create_const(value));
          parse_type();
          end_tree();
        } else {
          add_leaf(Lnast_node::create_const(value));
        }
        continue;
      }
      default:
        if (cur_token().is_ty()) {
          parse_type();
        } else {
          error();
        }
    }
  }
  forward_token();
}

void Lnast_parser::parse_selects() {
  // std::cout << "parse_selects\n";
  do {
    forward_token();  // '[' ->
    parse_prim();     // ref or const ->
    forward_token();  // ']' ->
  } while (cur_kind() == Lnast_token::lbrack);
}

void Lnast_parser::parse_dot_selects() {
  do {
    forward_token();  // '.'
    parse_prim();
  } while (cur_kind() == Lnast_token::dot);
}

void Lnast_parser::parse_attr() {
  forward_token();  // '.'
  parse_prim();
}

void Lnast_parser::parse_prim() {
  // std::cout << "parse_prim\n";
  if (cur_kind() == Lnast_token::id_var || cur_kind() == Lnast_token::id_fun) {
    add_leaf(Lnast_node::create_ref(cur_text()));
  } else if (cur_kind() == Lnast_token::number) {
    add_leaf(Lnast_node::create_const(cur_text()));
  } else if (cur_kind() == Lnast_token::string) {
    add_leaf(Lnast_node::create_const(cur_text()));
  }
  forward_token();
}

void Lnast_parser::parse_type() {
  // std::cout << "parse_type\n";
  switch (cur_kind()) {
    case Lnast_token::ty_none:
      add_leaf(Lnast_node::create_none_type());
      forward_token();
      break;
    case Lnast_token::ty_sint:
      add_leaf(Lnast_node::create_prim_type_sint());
      forward_token();
      break;
    case Lnast_token::ty_uint:
      add_leaf(Lnast_node::create_prim_type_uint());
      forward_token();
      break;
    case Lnast_token::ty_range:
      add_leaf(Lnast_node::create_prim_type_range());
      forward_token();
      break;
    case Lnast_token::ty_string:
      add_leaf(Lnast_node::create_prim_type_string());
      forward_token();
      break;
    case Lnast_token::ty_boolean:
      add_leaf(Lnast_node::create_prim_type_boolean());
      forward_token();
      break;
    case Lnast_token::ty_type:
      add_leaf(Lnast_node::create_prim_type_type());
      forward_token();
      break;
    case Lnast_token::ty_ref:
      add_leaf(Lnast_node::create_prim_type_ref());
      forward_token();
      break;
    case Lnast_token::ty_unknown:
      add_leaf(Lnast_node::create_unknown_type());
      forward_token();
      break;
    case Lnast_token::ty_s: {
      start_tree(Lnast_node::create_prim_type_sint());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    case Lnast_token::ty_u: {
      start_tree(Lnast_node::create_prim_type_uint());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    case Lnast_token::ty_tuple: {
      start_tree(Lnast_node::create_comp_type_tuple());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    case Lnast_token::ty_array: {
      start_tree(Lnast_node::create_comp_type_array());
      forward_token();
      if (cur_kind() != Lnast_token::lparen) {
        error();
      }
      forward_token();
      parse_type();
      if (cur_kind() != Lnast_token::comma) {
        error();
      }
      forward_token();
      parse_prim();
      if (cur_kind() != Lnast_token::rparen) {
        error();
      }
      end_tree();
      forward_token();
      break;
    }
    case Lnast_token::ty_mixin: {
      start_tree(Lnast_node::create_comp_type_mixin());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    case Lnast_token::ty_lambda: {
      start_tree(Lnast_node::create_comp_type_lambda());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    case Lnast_token::ty_expr: {
      start_tree(Lnast_node::create_expr_type());
      forward_token();
      parse_list();
      end_tree();
      break;
    }
    default: error();
  }
}

void Lnast_parser::parse_fun_stmt() {
  auto ref_node = Lnast_node::create_ref(cur_text());
  forward_token();
  if (cur_kind() == Lnast_token::equal) {
    // LNAST - func_def
    start_tree(Lnast_node::create_func_def());
    add_leaf(ref_node);
    forward_token();
    parse_stmts();
    end_tree();
  } else if (cur_kind() == Lnast_token::lparen) {
    // LNAST - func_call
    start_tree(Lnast_node::create_func_call());
    add_leaf(ref_node);
    parse_list();
    end_tree();
  }
}

void Lnast_parser::parse_if_stmt() {
  start_tree(Lnast_node::create_if());
  forward_token();
  if (cur_kind() != Lnast_token::lparen) {
    error();
  }
  forward_token();
  parse_prim();
  if (cur_kind() != Lnast_token::rparen) {
    error();
  }
  forward_token();
  parse_stmts();
  // TODO: Add `else if` and `else` branch
  end_tree();
}
