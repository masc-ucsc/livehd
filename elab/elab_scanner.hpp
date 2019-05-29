//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"

#include "explicit_type.hpp"
#include "fmt/format.h"
#include "iassert.hpp"

using Token_id = uint8_t;

using Token_entry = Explicit_type<uint32_t, struct Token_entry_struct>;

// WARNING:
//
// We can not use enum or enum class cleanly because they do not support
// inheritance in C++. It is just cleaner (less static_cast conversions) using
// constexpr
//
// Suggested way to extend tokens for other languages: (replace pyrope for your language)
//
// constexpr Token_id Pyrope_id_if   = 128; // Bigger than Token_id_keyword_first
// constexpr Token_id Pyrope_id_else = 129;
// constexpr Token_id Pyrope_id_for  = 130;
//
constexpr Token_id Token_id_nop           = 0;   // invalid token
constexpr Token_id Token_id_comment       = 1;   // c-like comments
constexpr Token_id Token_id_register      = 2;   // @asd @_asd
constexpr Token_id Token_id_pipe          = 3;   // |>
constexpr Token_id Token_id_alnum         = 4;   // a..z..0..9.._
constexpr Token_id Token_id_ob            = 5;   // {
constexpr Token_id Token_id_cb            = 6;   // }
constexpr Token_id Token_id_colon         = 7;   // :
constexpr Token_id Token_id_gt            = 8;   // >
constexpr Token_id Token_id_or            = 9;   // |
constexpr Token_id Token_id_at            = 10;  // @
constexpr Token_id Token_id_label         = 11;  // foo:
constexpr Token_id Token_id_output        = 12;  // %outasd
constexpr Token_id Token_id_input         = 13;  // $asd
constexpr Token_id Token_id_dollar        = 14;  // $
constexpr Token_id Token_id_percent       = 15;  // %
constexpr Token_id Token_id_dot           = 16;  // .
constexpr Token_id Token_id_div           = 17;  // /
constexpr Token_id Token_id_string        = 18;  // "asd23*.v" string (double quote)
constexpr Token_id Token_id_semicolon     = 19;  // ;
constexpr Token_id Token_id_comma         = 20;  // ,
constexpr Token_id Token_id_op            = 21;  // (
constexpr Token_id Token_id_cp            = 22;  // )
constexpr Token_id Token_id_pound         = 23;  // #
constexpr Token_id Token_id_mult          = 24;  // *
constexpr Token_id Token_id_num           = 25;  // 0123123 or 123123 or 0123ubits
constexpr Token_id Token_id_backtick      = 26;  // `ifdef
constexpr Token_id Token_id_synopsys      = 27;  // synopsys... directive in comment
constexpr Token_id Token_id_plus          = 28;  // +
constexpr Token_id Token_id_minus         = 29;  // -
constexpr Token_id Token_id_bang          = 30;  // !
constexpr Token_id Token_id_lt            = 31;  // <
constexpr Token_id Token_id_eq            = 32;  // =
constexpr Token_id Token_id_same          = 33;  // ==
constexpr Token_id Token_id_diff          = 34;  // !=
constexpr Token_id Token_id_coloneq       = 35;  // :=
constexpr Token_id Token_id_ge            = 37;  // >=
constexpr Token_id Token_id_le            = 38;  // <=
constexpr Token_id Token_id_and           = 39;  // &
constexpr Token_id Token_id_xor           = 40;  // ^
constexpr Token_id Token_id_qmark         = 41;  // ?
constexpr Token_id Token_id_tick          = 42;  // '
constexpr Token_id Token_id_obr           = 43;  // [
constexpr Token_id Token_id_cbr           = 44;  // ]
constexpr Token_id Token_id_keyword_first = 64;
constexpr Token_id Token_id_keyword_last  = 254;

class Token {
public:
  Token() {
    tok  = Token_id_nop;
    pos  = 0;
    line = 0;
    len  = 0;
  }
  void clear(uint32_t p, uint32_t lno) {
    tok  = Token_id_nop;
    pos  = p;
    line = lno;
    len  = 0;
  }
  void adjust(Token_id t, uint32_t p) {
    tok = t;
    pos = p;
    len = 1;
  }
  void adjust_inc() { len++; }
  void adjust_len(uint32_t new_pos) {
    GI(tok != Token_id_nop, new_pos >= pos);
    len = new_pos - pos;
  }

  Token_id tok;  // Token (identifier, if, while...)
  uint32_t pos;  // Position in buffer
  uint32_t line; // line of code
  uint16_t len;  // length in buffer

  std::string_view get_text(std::string_view buffer) const {
    I(buffer.size() > (pos + len));
    return buffer.substr(pos, len);
  }
};

class Elab_scanner {
protected:
  typedef std::vector<Token> Token_list;

  std::string      buffer_name;
  std::string_view buffer;
  Token_list       token_list;

  void scan_token_append(Token_list &toklist) const {
    assert(scanner_pos < token_list.size());
    toklist.push_back(token_list[scanner_pos]);
  }

  std::string_view get_buffer() { return buffer; }

private:
  struct Translate_item {
    Translate_item() : tok(Token_id_nop), try_merge(false) {}
    Translate_item(Token_id t, bool tm = false) : tok(t), try_merge(tm) {}
    Translate_item &operator=(const Translate_item &other) {
      *const_cast<Token_id *>(&tok)   = other.tok;
      *const_cast<bool *>(&try_merge) = other.try_merge;
      return *this;
    }
    Translate_item &operator=(const Token_id &other_tok) {
      *const_cast<Token_id *>(&tok)   = other_tok;
      *const_cast<bool *>(&try_merge) = false;
      return *this;
    }
    const Token_id tok;
    const bool     try_merge;
  };
  std::vector<Translate_item> translate;
  bool                        token_list_spaced = true;
  bool                        trying_merge      = false;

  // Fields updated for each chunk processed
  Token_entry scanner_pos;

  int         max_errors;
  int         max_warnings;
  mutable int n_errors;  // NOTE: mutable to allow const methods for error/warning reporting
  mutable int n_warnings;

  void setup_translate();

  void add_token(Token &t);

  void chunked(std::string_view buffer);

  void scan_raw_msg(std::string_view cat, std::string_view text, bool third) const;

  void lex_error(std::string_view text);

public:
  Elab_scanner();

  void scan_error(std::string_view text) const;  // Not really const, but to allow to be used by const methods
  void scan_warn(std::string_view text) const;

  void parser_error(std::string_view text) const;
  void parser_warn(std::string_view text) const;
  void parser_info(std::string_view text) const;

  template <typename... Args>
  void scan_error(std::string_view format, const Args &... args) const {
    scan_error(fmt::vformat(format, fmt::make_format_args(args...)));
  }
  template <typename... Args>
  void scan_warn(std::string_view format, const Args &... args) const {
    scan_warn(fmt::vformat(format, fmt::make_format_args(args...)));
  }
  template <typename... Args>
  void parser_error(std::string_view format, const Args &... args) const {
    parser_error(fmt::vformat(format, fmt::make_format_args(args...)));
  }
  template <typename... Args>
  void parser_warn(std::string_view format, const Args &... args) const {
    parser_warn(fmt::vformat(format, fmt::make_format_args(args...)));
  }
  template <typename... Args>
  void parser_info(std::string_view format, const Args &... args) const {
    parser_info(fmt::vformat(format, fmt::make_format_args(args...)));
  }

  bool scan_next();
  bool scan_prev();

  void set_max_errors(int n) { max_errors = n; }
  void set_max_warning(int n) { max_warnings = n; }

  bool scan_is_end() const { return scanner_pos >= token_list.size(); }

  bool scan_is_token(Token_id tok) const {
    if (scanner_pos < token_list.size()) return token_list[scanner_pos].tok == tok;
    return false;
  }

  Token_entry scan_token() const {
    I(scanner_pos != 0);
    I(scanner_pos < token_list.size());
    return scanner_pos;
  }

  void scan_format_append(std::string &text) const;

  std::string_view scan_sview() const {
    I(scanner_pos < token_list.size());
    return std::string_view(&buffer[token_list[scanner_pos].pos], token_list[scanner_pos].len);
  }

  std::string_view scan_prev_sview() const {
    size_t p = scanner_pos - 1;
    if (p < 0) p = 0;
    return std::string_view(&buffer[token_list[p].pos], token_list[p].len);
  }

  std::string_view scan_next_sview() const {
    //ori
    //size_t p = scanner_pos - 1;
    //if (p >= token_list.size())
    //  p = token_list.size() - 1;
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      p = token_list.size()-1;
    return std::string_view(&buffer[token_list[p].pos], token_list[p].len);
  }
  void scan_append(std::string &text) const;
  void scan_prev_append(std::string &text) const;
  void scan_next_append(std::string &text) const;

  inline std::string_view scan_text(const Token_entry te) const {
    I(token_list.size() > te);
    return buffer.substr(token_list[te].pos, token_list[te].len);
  }

  std::string scan_text() const;
  int         scan_calc_lineno() const;

  size_t get_token_pos() const { return token_list[scan_token()].pos; }

  bool scan_is_prev_token(Token_id tok) const {
    if (scanner_pos == 0) return false;
    assert(scanner_pos < token_list.size());
    return token_list[scanner_pos - 1].tok == tok;
  }
  bool scan_is_next_token(int pos, Token_id tok) const {
    if ((scanner_pos + pos) >= token_list.size()) return false;
    return token_list[scanner_pos + pos].tok == tok;
  }

  bool scan_next_token_is(Token_id tok){
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      p = token_list.size() - 1;
    return token_list[p].tok == tok;;
  }

  void patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords);

  void parse(std::string_view name, std::string_view str);
  void parse(std::string_view name, std::string_view str, Token_list &tlist);

  void dump_token() const;

  virtual void elaborate() = 0;

  bool has_errors() const { return n_errors > 0; }
};
