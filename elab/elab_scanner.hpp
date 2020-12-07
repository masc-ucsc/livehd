//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "explicit_type.hpp"
#include "fmt/format.h"
#include "iassert.hpp"

#include "map_col.hpp"

using Token_id = uint8_t;

using Token_entry = Explicit_type<uint32_t, struct Token_entry_struct, 0>;

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
constexpr Token_id Token_id_register      = 16;   // #asd #_asd
constexpr Token_id Token_id_pipe          = 2;   // |>
constexpr Token_id Token_id_alnum         = 3;   // a..z..0..9.._
constexpr Token_id Token_id_ob            = 123;   // {
constexpr Token_id Token_id_cb            = 125;   // }
constexpr Token_id Token_id_colon         = 58;   // :
constexpr Token_id Token_id_gt            = 62;   // >
constexpr Token_id Token_id_or            = 124;   // |
constexpr Token_id Token_id_at            = 64;  // @
constexpr Token_id Token_id_tilde         = 126;  // ~
constexpr Token_id Token_id_output        = 4;  // %outasd
constexpr Token_id Token_id_input         = 5;  // $asd
constexpr Token_id Token_id_dollar        = 36;  // $
constexpr Token_id Token_id_percent       = 37;  // %
constexpr Token_id Token_id_dot           = 46;  // .
constexpr Token_id Token_id_div           = 47;  // /
constexpr Token_id Token_id_string        = 34;  // "asd23*.v" string (double quote)
constexpr Token_id Token_id_semicolon     = 59;  // ;
constexpr Token_id Token_id_comma         = 44;  // ,
constexpr Token_id Token_id_op            = 40;  // (
constexpr Token_id Token_id_cp            = 41;  // )
constexpr Token_id Token_id_pound         = 35;  // #
constexpr Token_id Token_id_mult          = 42;  // *
constexpr Token_id Token_id_num           = 6;  // 0123123 or 123123 or 0123ubits
constexpr Token_id Token_id_backtick      = 96;  // `ifdef
constexpr Token_id Token_id_synopsys      = 7;  // synopsys... directive in comment
constexpr Token_id Token_id_plus          = 43;  // +
constexpr Token_id Token_id_minus         = 45;  // -
constexpr Token_id Token_id_bang          = 33;  // !
constexpr Token_id Token_id_lt            = 60;  // <
constexpr Token_id Token_id_eq            = 61;  // =
constexpr Token_id Token_id_same          = 8;  // ==
constexpr Token_id Token_id_diff          = 9;  // !=
constexpr Token_id Token_id_coloneq       = 10;  // :=
constexpr Token_id Token_id_ge            = 11;  // >=
constexpr Token_id Token_id_le            = 12;  // <=
constexpr Token_id Token_id_and           = 38;  // &
constexpr Token_id Token_id_xor           = 94;  // ^
constexpr Token_id Token_id_qmark         = 63;  // ?
constexpr Token_id Token_id_tick          = 39;  // '
constexpr Token_id Token_id_obr           = 91;  // [
constexpr Token_id Token_id_cbr           = 93;  // ]
constexpr Token_id Token_id_backslash     = 92;  // \ back slash
constexpr Token_id Token_id_reference     = 13;  // \foo
constexpr Token_id Token_id_keyword_first = 14;
constexpr Token_id Token_id_keyword_last  = 254;


class Token {
protected:
  std::string_view text;
public:
  Token() {
    tok   = Token_id_nop;
    pos1  = 0;
    pos2  = 0;
    line  = 0;
    text  = std::string_view{""};
  }
  Token(Token_id _tok, uint64_t _pos1, uint64_t _pos2, uint32_t _line, std::string_view _text) {
    tok   = _tok;
    pos1  = _pos1;
    pos2  = _pos2;
    line  = _line;
    text  = _text;
  }
  void reset(Token_id _tok, uint64_t _pos1, uint32_t _line, std::string_view _text) {
    tok   = _tok;
    pos1  = _pos1;
    pos2  = _pos1; //FIXME->sh: you reset the token so the length of the token should be 0, pos2 follows pos1
    line  = _line;
    text  = _text;
  }
  void clear(uint64_t _pos1, uint32_t _line, std::string_view _text) {
    tok   = Token_id_nop;
    pos1  = _pos1;
    pos2  = _pos1; //FIXME->sh: you clear the token so the length of the token should be 0, pos2 follows pos1
    line  = _line;
    text  = _text;
  }

  void fuse_token(Token_id new_tok, const Token &t2) {
    I(text.data() + text.size() == t2.text.data()); // t2 must be continuous (otherwise, create new token)
    tok = new_tok;

    auto new_len = text.size() + t2.text.size();
    text = std::string_view{text.data(), new_len};
  }

  void append_token(const Token &t2) {
    I(text.data() + text.size() == t2.text.data()); // t2 must be continuous (otherwise, create new token)

    auto new_len = text.size() + t2.text.size();
    text = std::string_view{text.data(), new_len};
  }

  void adjust_token_size(uint64_t end_pos) {
    GI(tok != Token_id_nop, end_pos >= pos2); //FIXME->sh: check correctness of pos2
    auto new_len = end_pos - pos2;
    text = std::string_view{text.data(), new_len};
  }

  Token_id tok;  // Token (identifier, if, while...)
  uint64_t pos1; // start position in original memblock for debugging
  uint64_t pos2; // end position in original memblock for debugging
  uint32_t line; // line of code

  std::string_view get_text() const { return text; }
};


class Elab_scanner {
public:
  typedef std::vector<Token> Token_list; 
  typedef SourceMap::ColMapArr SrcMap_Token_list; // std::vector<shared_ptr<ColMap>>
protected:

  Token_list       token_list;
  SrcMap_Token_list       SrcMap_token_list; 

private:
  std::string      buffer_name;

  std::string_view memblock;
  int              memblock_fd;
  void unregister_memblock();

  struct Translate_item {
    Translate_item() : tok(Token_id_nop), try_merge(false) {}
    Translate_item(Token_id t, bool tm = false) : tok(t), try_merge(tm) {}
    Translate_item(const Translate_item &t) : tok(t.tok), try_merge(t.try_merge) {}
    Translate_item(const Token_id &other_tok) : tok(other_tok), try_merge(false) {}
#if 1
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
#endif
    const Token_id tok;
    const bool     try_merge;
  };
  std::vector<Translate_item> translate;
  bool                        token_list_spaced = true;
  bool                        trying_merge      = false;

  // Fields updated for each chunk processed
  Token_entry scanner_pos;
  // Token_entry NEW_scanner_pos;

  int         max_errors;
  int         max_warnings;
  mutable int n_errors;  // NOTE: mutable to allow const methods for error/warning reporting
  mutable int n_warnings;

  void setup_translate();

  void SrcMap_fuse_token(SourceMap::ColMapSP &t, Token_id new_tok, const SourceMap::ColMapSP &t2); 
  void SrcMap_append_token(SourceMap::ColMapSP &t, const SourceMap::ColMapSP &t2); 
  void SrcMap_adjust_token_size(SourceMap::ColMapSP &t, uint64_t end_pos); 
  SourceMap::ColMapSP SrcMap_clear(SourceMap::ColMapSP &t, uint64_t _pos1, uint32_t _line, std::string_view _text); 
  SourceMap::ColMapSP SrcMap_reset(SourceMap::ColMapSP &t, Token_id _tok, uint64_t _pos1, uint32_t _line, std::string_view _text); 

  void add_token(Token &t);
  void SrcMap_add_token(SourceMap::ColMapSP &t); 

  void scan_raw_msg(std::string_view cat, std::string_view text, bool third) const;
  void Token_scan_raw_msg(std::string_view cat, std::string_view text, bool third) const; 
  void SrcMap_scan_raw_msg(std::string_view cat, std::string_view text, bool third) const; 

  void lex_error(std::string_view text);

  void parse_setup(std::string_view filename);
  void Token_parse_setup(std::string_view filename); 
  void SrcMap_parse_setup(std::string_view filename); 
  void parse_setup();
  void Token_parse_setup(); 
  void SrcMap_parse_setup(); 
  void parse_step();
  void Token_parse_step(); 
  void SrcMap_parse_step(); 




protected:
  std::pair<std::string_view, int> transfer_memblock_ownership() {
    std::pair<std::string_view, int> p{memblock, memblock_fd};
    memblock_fd = -1;
    return p;
  }
  std::string_view get_memblock() const { return memblock; }
  std::string_view get_filename() const { I(memblock_fd != -1); return buffer_name; }
  bool is_parse_inline() const { return memblock_fd == -1; }

  void parser_error_int(std::string_view text) const;
  void parser_warn_int(std::string_view text) const;
  void parser_info_int(std::string_view text) const;
  void scan_error_int(std::string_view text) const;
  void scan_warn_int(std::string_view text) const;

public:
  Elab_scanner();
  virtual ~Elab_scanner();

  template <typename S, typename... Args>
  void scan_error(const S& format, Args&&... args) const {
    scan_error_int(fmt::format(format, args...));
  }
  template <typename S, typename... Args>
  void scan_warn(const S& format, Args&&... args) const {
    scan_warn_int(fmt::format(format, args...));
  }

  template <typename S, typename... Args>
  void parser_error(const S& format, Args&&... args) const {
    parser_error_int(fmt::format(format, args...));
  }
  template <typename S, typename... Args>
  void parser_warn(const S& format, Args&&... args) const {
    parser_warn_int(fmt::format(format, args...));
  }
  template <typename S, typename... Args>
  void parser_info(const S& format, Args&&... args) const {
    parser_info_int(fmt::format(format, args...));
  }

  bool scan_next();
  bool Token_scan_next(); 
  bool SrcMap_scan_next(); 

  bool scan_prev();
  bool Token_scan_prev(); 
  bool SrcMap_scan_prev(); 

  void set_max_errors(int n) { max_errors = n; }
  void set_max_warning(int n) { max_warnings = n; }

  bool use_Token = true; 

  bool scan_is_end() {
    if (use_Token)
      return Token_scan_is_end();
    else
      return SrcMap_scan_is_end();
  }
  bool scan_is_token(Token_id tok) const {
    if (use_Token)
      return Token_scan_is_token(tok);
    else
      return SrcMap_scan_is_token(tok);
  }
  Token_entry scan_token() const {
    if (use_Token)
      return Token_scan_token();
    else
      return SrcMap_scan_token();
  }
  std::string_view scan_prev_text() const {
    if (use_Token)
      return Token_scan_prev_text();
    else
      return SrcMap_scan_prev_text();
  }
  std::string_view scan_next_text() const {
    if (use_Token)
      return Token_scan_next_text();
    else
      return SrcMap_scan_next_text();
  }
  bool scan_next_is_token(Token_id tok) const {
    if (use_Token)
      return Token_scan_next_is_token(tok);   
    else
      return SrcMap_scan_next_is_token(tok);
  }
  std::string_view scan_peep_text(int offset) const {
    if (use_Token)
      return Token_scan_peep_text(offset);    
    else
      return SrcMap_scan_peep_text(offset);
  }
  inline std::string_view scan_text(const Token_entry te) const {
    if (use_Token)
      return Token_scan_text(te);
    else
      return SrcMap_scan_text(te);
  }
  std::string_view scan_text() const {
    if (use_Token)
      return Token_scan_text();
    else
      return SrcMap_scan_text();
  }
  size_t get_token_pos() const {
    if (use_Token)
      return Token_get_token_pos();
    else
      return SrcMap_get_token_pos();
  }
  bool scan_is_prev_token(Token_id tok) const {
    if (use_Token)
      return Token_scan_is_prev_token(tok);
    else
      return SrcMap_scan_is_prev_token(tok);
  }
  bool scan_peep_is_token(Token_id tok, int offset) const {
    if (use_Token)
      return Token_scan_peep_is_token(tok, offset);
    else
      return SrcMap_scan_peep_is_token(tok, offset);
  }
  bool scan_is_next_token(int pos, Token_id tok) const {
    if (use_Token)
      return Token_scan_is_next_token(pos, tok);
    else
      return SrcMap_scan_is_next_token(pos, tok);
  }
  const Token get_token(Token_entry entry){
    if (use_Token)
      return Token_get_token(entry);
    else{
      return SrcMap_get_token(entry);
    }
  }
  Token scan_get_token(int offset = 0) const {
    if (use_Token)
      return Token_scan_get_token(offset);
    else
      return SrcMap_scan_get_token(offset);
  }

  bool Token_scan_is_end() const { return scanner_pos >= token_list.size(); }
  bool SrcMap_scan_is_end() const { return scanner_pos >= SrcMap_token_list.size(); } 

  bool Token_scan_is_token(Token_id tok) const {
    if (scanner_pos < token_list.size()) return token_list[scanner_pos].tok == tok;
    return false;
  }
  bool SrcMap_scan_is_token(Token_id tok) const {
    if (scanner_pos < SrcMap_token_list.size()) return SrcMap_token_list[scanner_pos]->tkn_idx == tok; 
    return false;
  }
  
  
  Token_entry Token_scan_token() const {
    I(scanner_pos != 0);
    I(scanner_pos < token_list.size());
    return scanner_pos;
  }
  Token_entry SrcMap_scan_token() const {
    I(scanner_pos != 0);
    I(scanner_pos < SrcMap_token_list.size()); 
    return scanner_pos;
  }

  
  std::string_view Token_scan_prev_text() const {
    size_t p = scanner_pos - 1;
    if (scanner_pos <= 0) p = 0;

    return token_list[p].get_text();
  }
  std::string_view SrcMap_scan_prev_text() const {
    size_t p = scanner_pos - 1;
    if (scanner_pos <= 0) p = 0;
    return SrcMap_token_list[p]->text; 
  }

  
  std::string_view Token_scan_next_text() const {
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      p = token_list.size()-1;

    return token_list[p].get_text();
  }
  std::string_view SrcMap_scan_next_text() const {
    size_t p = scanner_pos + 1;
    if (p >= SrcMap_token_list.size()) 
      p = SrcMap_token_list.size()-1; 
    return SrcMap_token_list[p]->text; 
  }


  bool Token_scan_next_is_token(Token_id tok) const {
    size_t p = scanner_pos + 1;
    if (p >= token_list.size())
      return false;
    return token_list[p].tok == tok;
  }
  bool SrcMap_scan_next_is_token(Token_id tok) const {
    size_t p = scanner_pos + 1;
    if (p >= SrcMap_token_list.size()) 
      return false;
    return SrcMap_token_list[p]->tkn_idx == tok; 
  }


  std::string_view Token_scan_peep_text(int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size()-1;
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return token_list[p].get_text();
  }
  std::string_view SrcMap_scan_peep_text(int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= SrcMap_token_list.size()) 
      p = SrcMap_token_list.size()-1; 
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return SrcMap_token_list[p]->text;
  }

  void scan_format_append(std::string &text) const;
  void Token_scan_format_append(std::string &text) const; 
  void SrcMap_scan_format_append(std::string &text) const; 


  inline std::string_view Token_scan_text(const Token_entry te) const {
    I(token_list.size() > te);
    return token_list[te].get_text();
  }
  inline std::string_view SrcMap_scan_text(const Token_entry te) const {
    I(SrcMap_token_list.size() > te); 
    return SrcMap_token_list[te]->text; 
  }

  
  std::string_view Token_scan_text() const {
    return token_list[scanner_pos].get_text();
  }
  std::string_view SrcMap_scan_text() const {
    return SrcMap_token_list[scanner_pos]->text; 
  }


  uint32_t    scan_line() const;
  uint32_t    Token_scan_line() const; 
  uint32_t    SrcMap_scan_line() const; 

  size_t Token_get_token_pos() const { return token_list[scan_token()].pos1; }
  size_t SrcMap_get_token_pos() const { return SrcMap_token_list[scan_token()]->src_col; } 

  bool Token_scan_is_prev_token(Token_id tok) const {
    if (scanner_pos == 0) return false;
    I(scanner_pos < token_list.size());
    return token_list[scanner_pos - 1].tok == tok;
  }
  bool SrcMap_scan_is_prev_token(Token_id tok) const {
    if (scanner_pos == 0) return false;
    I(scanner_pos < SrcMap_token_list.size()); 
    return SrcMap_token_list[scanner_pos - 1]->tkn_idx == tok; 
  }


  bool Token_scan_is_next_token(int pos, Token_id tok) const {
    if ((scanner_pos + pos) >= token_list.size()) return false;
    return token_list[scanner_pos + pos].tok == tok;
  }
  bool SrcMap_scan_is_next_token(int pos, Token_id tok) const {
    if ((scanner_pos + pos) >= SrcMap_token_list.size()) return false; 
    return  SrcMap_token_list[scanner_pos + pos]->tkn_idx == tok; 
  }

  
  bool Token_scan_peep_is_token(Token_id tok, int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size() - 1;
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return token_list[p].tok == tok;
  }
  bool SrcMap_scan_peep_is_token(Token_id tok, int offset) const {
    I(offset != 0);
    size_t p = scanner_pos + offset;
    if (p >= SrcMap_token_list.size()) 
      p = SrcMap_token_list.size() - 1; 
    else if (offset > static_cast<int>(scanner_pos))
      p = 0 ;
    return SrcMap_token_list[p]->tkn_idx == tok; 
  }

  void patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords);
  void Token_patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords); 
  void SrcMap_patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords); 

  void patch_pass() {
      absl::flat_hash_map<std::string, Token_id> no_keywords;
      patch_pass(no_keywords);
  }

  void parse_file(std::string_view filename) {
    parse_setup(filename);
    parse_step();
  }

  void parse_inline(std::string_view txt) {
    parse_setup();
    memblock = txt;
    parse_step();
  }

  void dump_token() const;
  void Token_dump_token() const;
  void SrcMap_dump_token() const;

  virtual void elaborate() = 0;

  bool has_errors() const { return n_errors > 0; }

  Token Token_scan_get_token(int offset = 0) const {
    size_t p = scanner_pos + offset;
    if (p >= token_list.size())
      p = token_list.size() - 1;

    //comment out by Sheng
    //else if (offset > static_cast<int>(scanner_pos))
    //  p = 0 ;
    return token_list[p];
  } 
  Token SrcMap_scan_get_token(int offset = 0) const {
    size_t p = scanner_pos + offset;
    if (p >= SrcMap_token_list.size()) 
      p = SrcMap_token_list.size() - 1; 

    //comment out by Sheng
    //else if (offset > static_cast<int>(scanner_pos))
    //  p = 0 ;

    // return token_list[p];
    SourceMap::ColMapSP t1 = SrcMap_token_list[p];
    Token t2(t1->tkn_idx, t1->src_col, t1->src_idx, t1->src_line, t1->text);
    return t2;
  } 

  Token &Token_get_token(Token_entry entry){
    return token_list[entry];
  }

  Token SrcMap_get_token(Token_entry entry){
    SourceMap::ColMapSP t1 = SrcMap_token_list[entry];
    Token t2(t1->tkn_idx, t1->src_col, t1->src_idx, t1->src_line, t1->text);
    return t2;
  }

};
