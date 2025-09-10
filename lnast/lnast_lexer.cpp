//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_lexer.hpp"

#include <string>

Lnast_lexer::Lnast_lexer(std::istream &_is) : is(_is) {}

int Lnast_lexer::get_char() { return is.get(); }

void Lnast_lexer::put_char(char ch) { is.putback(ch); }

Lnast_token Lnast_lexer::lex_token() {
  while (true) {
    char ch = get_char();
    if (is.eof()) {
      return form_token(Lnast_token::eof);
    }
    switch (ch) {
      case 0: return form_token(Lnast_token::eof);
      case ' ':
      case '\t':
      case '\r':
      case '\n': continue;
      case '/': lex_comment(); continue;
#define TOKEN_PN(NAME, SPELLING) \
  case SPELLING: return form_token(Lnast_token::NAME);
#include "lnast_tokens.def"
      case '%':
      case '!': return lex_identifier(ch);
      case '#': return lex_type();
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': return lex_number(ch);
      case '"': return lex_string();
      default: return lex_keyword_or_function_or_identifier(ch);
    }
  }
}

void Lnast_lexer::lex_comment() {
  char second = get_char();
  if (second == '*') {
    while (true) {
      if (get_char() == '*') {
        char ch = get_char();
        if (ch == '/') {
          return;
        }
        put_char(ch);
      }
      if (is.eof()) {
        return;
      }
    }
  } else if (second == '/') {
    while (get_char() != '\n');
  }
}

Lnast_token Lnast_lexer::lex_identifier(char type) {
  if (get_char() != '{') {
    // TODO: emit error
  }
  std::string str = "";
  while (true) {
    char ch = get_char();
    if (ch == '}') {
      break;
    }
    str += ch;
  }
  return form_token((type == '%') ? Lnast_token::id_var : Lnast_token::id_fun, str);
}

Lnast_token Lnast_lexer::lex_type() {
  std::string str = "";
  while (true) {
    char ch = get_char();
    if (!isalpha(ch)) {
      put_char(ch);
      break;
    }
    str += ch;
  }
#define TOKEN_TY(SPELLING) \
  if (str == #SPELLING)    \
    return form_token(Lnast_token::ty_##SPELLING);
#include "lnast_tokens.def"
  return form_token(Lnast_token::id_var, str);
}

Lnast_token Lnast_lexer::lex_keyword_or_function_or_identifier(char first) {
  std::string str(1, first);
  while (true) {
    char ch = get_char();
    if (!isalnum(ch) && ch != '_') {
      put_char(ch);
      break;
    }
    str += ch;
  }
#define TOKEN_KW(SPELLING) \
  if (str == #SPELLING)    \
    return form_token(Lnast_token::kw_##SPELLING);
#define TOKEN_FN(NAME, SPELLING) \
  if (str == #SPELLING)          \
    return form_token(Lnast_token::fn_##NAME);
#include "lnast_tokens.def"
  return form_token(Lnast_token::id_var, str);
}

Lnast_token Lnast_lexer::lex_number(char first) {
  // FIXME: currently only recognizes simple numbers
  std::string str(1, first);
  while (true) {
    char ch = get_char();
    if (!(isalnum(ch) || ch == '?')) {
      put_char(ch);
      break;
    }
    str += ch;
  }
  return form_token(Lnast_token::number, str);
}

Lnast_token Lnast_lexer::lex_string() {
  std::string str = "";
  while (true) {
    char ch = get_char();
    switch (ch) {
      case '"':
        // str += '"';
        return form_token(Lnast_token::string, str);
      case 0: continue;
      case '\\':
        str += ch;
        str += get_char();
        continue;
      default: str += ch; continue;
    }
  }
}
