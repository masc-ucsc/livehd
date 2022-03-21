//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_parser.hpp"
#include "lnast_lexer.hpp"

Lnast_parser::Lnast_parser(std::istream &_is) : is(_is) {}

std::unique_ptr<Lnast> Lnast_parser::parse_all() {
  Lnast_lexer lexer(is);

  while (true) {
    auto token = lexer.lex_token();
    if (token.is(Lnast_token::eof)) break;
    fmt::print("{}\n", token.get_string());
  }

  return std::make_unique<Lnast>();
}
