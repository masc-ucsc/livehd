//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// S-expression reader for the rule DSL (.rules files and analyses.decl).
//
//   atoms     any run of characters other than whitespace, ( ) [ ] { } " ;
//   strings   "..." with \" and \\ escapes
//   lists     ( ... ) or [ ... ]
//   attrs     Name{a,b}: braces glued to an atom attach to that atom
//   comments  ; to the end of the line
//
// Every node remembers its source line, so a diagnostic and the generated
// `#line` directives point back into the .rules file.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::aegraph::rulegen {

struct Sexp {
  enum class Kind : uint8_t { atom, str, list };

  Kind                     kind = Kind::atom;
  char                     open = '(';  // list: '(' or '['
  std::string              text;        // atom / string
  std::vector<std::string> attrs;       // atom{a,b}
  std::vector<Sexp>        items;       // list
  int                      line = 0;

  [[nodiscard]] bool is_atom() const { return kind == Kind::atom; }
  [[nodiscard]] bool is_list() const { return kind == Kind::list; }
  [[nodiscard]] bool is_atom(std::string_view t) const { return kind == Kind::atom && text == t; }
  // The head atom of a list ("" when it has none).
  [[nodiscard]] std::string_view head() const {
    return (kind == Kind::list && !items.empty() && items.front().kind == Kind::atom) ? std::string_view{items.front().text}
                                                                                          : std::string_view{};
  }
};

// Thrown for a malformed file ("file:line: error: message").
struct Syntax_error {
  std::string message;
};

[[nodiscard]] std::vector<Sexp> parse_sexps(std::string_view text, std::string_view file);

// Canonical one-line spelling (for diagnostics, RULES.md and the generated tests).
[[nodiscard]] std::string to_text(const Sexp& s);

}  // namespace livehd::aegraph::rulegen
