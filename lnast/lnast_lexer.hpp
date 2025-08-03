//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cctype>
#include <format>
#include <iostream>

#include "absl/strings/str_cat.h"

class Lnast_token {
public:
  enum Kind : uint16_t {
#define TOKEN_MK(NAME)           NAME,
#define TOKEN_PN(NAME, SPELLING) NAME,
#define TOKEN_LT(NAME)           NAME,
#define TOKEN_ID(NAME)           id_##NAME,
#define TOKEN_KW(SPELLING)       kw_##SPELLING,
#define TOKEN_TY(SPELLING)       ty_##SPELLING,
#define TOKEN_FN(NAME, SPELLING) fn_##NAME,
#include "lnast_tokens.def"
  };

  explicit Lnast_token(Kind k, std::string_view s) : kind(k), text(s) {}

  bool is(Kind k) { return kind == k; }

  std::string get_string() {
    switch (kind) {
#define KIND_STR(KIND) \
  case Kind::KIND:     \
    if (text.empty())  \
      return #KIND;    \
    else               \
      return #KIND ", " + text;
#define TOKEN_MK(NAME)           KIND_STR(NAME)
#define TOKEN_PN(NAME, SPELLING) KIND_STR(NAME)
#define TOKEN_LT(NAME)           KIND_STR(NAME)
#define TOKEN_ID(NAME)           KIND_STR(id_##NAME)
#define TOKEN_KW(SPELLING)       KIND_STR(kw_##SPELLING)
#define TOKEN_TY(SPELLING)       KIND_STR(ty_##SPELLING)
#define TOKEN_FN(NAME, SPELLING) KIND_STR(fn_##NAME)
#include "lnast_tokens.def"
#undef KIND_STR
    }
    return "";
  }

  bool is_ty() {
    switch (kind) {
#define TOKEN_TY(SPELLING) \
  case Kind::ty_##SPELLING: return true;
#include "lnast_tokens.def"
      default: return false;
    }
    return false;
  }

  std::string_view get_text() { return text; }
  Kind             get_kind() { return kind; }

private:
  Kind        kind;
  std::string text;
};

class Lnast_lexer {
public:
  explicit Lnast_lexer(std::istream&);
  Lnast_token lex_token();

protected:
  std::istream& is;

  int  get_char();
  void put_char(char);

  Lnast_token form_token(Lnast_token::Kind kind, std::string_view str) { return Lnast_token(kind, str); }

  Lnast_token form_token(Lnast_token::Kind kind) { return Lnast_token(kind, ""); }

  void lex_comment();

  Lnast_token lex_identifier(char);
  Lnast_token lex_type();
  Lnast_token lex_keyword_or_function_or_identifier(char);
  Lnast_token lex_number(char);
  Lnast_token lex_string();
};
