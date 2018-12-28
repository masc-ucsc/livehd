#pragma once

#include <cstdint>
#include <cassert>
#include <string>
#include <vector>
#include <map>

#include "fmt/format.h"

#define MIN_CHUNK_SIZE 4

// No token
#define TOK_NOP        0x00
// # foo
#define TOK_COMMENT    0x01
// @asd @_asd
#define TOK_REGISTER   0x03
// |>
#define TOK_PIPE       0x04
// a..z..0..9.._
#define TOK_ALNUM      0x05
// {
#define TOK_OB         0x06
// }
#define TOK_CB         0x07
// :
#define TOK_COLON      0x08
// >
#define TOK_GT         0x09
// |
#define TOK_OR         0x0a
// @
#define TOK_AT         0x0b
// foo:
#define TOK_LABEL      0x0c
// %outasd
#define TOK_OUTPUT     0x0d
// $asd
#define TOK_INPUT      0x0e
// $
#define TOK_DOLLAR     0x0f
// %
#define TOK_PERCENT    0x10
// .
#define TOK_DOT        0x11
// /
#define TOK_DIV        0x12
// "asd23*.v" string (double quote)
#define TOK_STRING     0x13
// ;
#define TOK_SEMICOLON  0x14
// ,
#define TOK_COMMA      0x15
// (
#define TOK_OP         0x16
// )
#define TOK_CP         0x17
// #
#define TOK_POUND      0x18
// *
#define TOK_MUL        0x19
// 0123123 or 123123 or 0123ubits
#define TOK_NUM        0x1a
// 0123123 or 123123 or 0123ubits
#define TOK_BACKTICK   0x1b
// synopsys... directive in comment
#define TOK_SYNOPSYS   0x1c
// +
#define TOK_PLUS       0x1d
// -
#define TOK_MINUS      0x1e
// !
#define TOK_BANG       0x1d
// <
#define TOK_LT         0x1e
// =
#define TOK_EQ         0x1f
// ==
#define TOK_SAME       0x20
// !=
#define TOK_DIFF       0x21
// :=
#define TOK_COLONEQ    0x22
// >=
#define TOK_GE         0x23
// <=
#define TOK_LE         0x24
// &
#define TOK_AND        0x25
// ^
#define TOK_XOR        0x26
// ?
#define TOK_QMARK      0x27
// '
#define TOK_TICK       0x28
// [
#define TOK_OBR        0x29
// ]
#define TOK_CBR        0x30

#define TOK_KEYWORD_FIRST   0x40
#define TOK_KEYWORD_LAST    0x7F
#define TOK_TRYMERGE   0x80

class Elab_scanner {
protected:
  typedef uint8_t Token_id;

  struct Token {
    Token() {
      tok = TOK_NOP;
      pos = 0;
      len = 0;
    }
    Token_id tok; // Token (identifier, if, while...)
    uint32_t pos; // Position in buffer
    uint16_t len; // length in buffer
  };
  typedef std::vector<Token> Token_list;

  std::string buffer_name;
  std::string_view buffer;
  Token_list token_list;

  void scan_token_append(Token_list &toklist) const {
    assert(scanner_pos < token_list.size());
    toklist.push_back(token_list[scanner_pos]);
  }

private:
  std::vector<Token_id> translate;
  bool token_list_spaced = true;

  // Fields updated for each chunk processed
  size_t scanner_pos;

  int max_errors;
  int max_warnings;
  mutable int n_errors;   // NOTE: mutable to allow const methods for error/warning reporting
  mutable int n_warnings;

  void setup_translate();

  void add_token(Token &t);

  void chunked(std::string_view buffer);

  void scan_raw_msg(std::string_view cat, std::string_view text, bool third) const;

  void lex_error(std::string_view text);
public:
  Elab_scanner();

  void scan_error(std::string_view text) const; // Not really const, but to allow to be used by const methods
  void scan_warn(std::string_view text) const;

  void parser_error(std::string_view text) const;
  void parser_warn(std::string_view text) const;
  void parser_info(std::string_view text) const;

  bool scan_next();

  void set_max_errors(int n) {
    max_errors = n;
  }
  void set_max_warning(int n) {
    max_warnings = n;
  }

  bool scan_is_end() const {
    return scanner_pos >= token_list.size();
  }

  bool scan_is_token(Token_id tok) const {
    if(scanner_pos < token_list.size())
      return token_list[scanner_pos].tok == tok;
    return false;
  }

  void scan_format_append(std::string &text) const;

  void scan_append(std::string &text) const;
  void scan_prev_append(std::string &text) const;
  void scan_next_append(std::string &text) const;

  std::string scan_text() const;
  int scan_calc_lineno() const;

  size_t get_token_pos() const {
    if (scanner_pos==0)
      return 0;
    assert(scanner_pos< token_list.size());
    return token_list[scanner_pos-1].pos;
  }

  bool scan_is_prev_token(Token_id tok) const {
    if (scanner_pos==0)
      return false;
    assert(scanner_pos< token_list.size());
    return token_list[scanner_pos-1].tok == tok;
  }
  bool scan_is_next_token(int pos, Token_id tok) const {
    if ((scanner_pos+pos)>=token_list.size())
      return false;
    return token_list[scanner_pos+pos].tok == tok;
  }

  void patch_pass(const std::map<std::string, uint8_t> &keywords);

  void parse(std::string_view name, std::string_view str, bool chunking=false);

  void dump_token() const;

  virtual void elaborate() = 0;

  bool has_errors() const { return n_errors > 0; }
};

