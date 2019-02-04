//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <ctype.h>

#include <cctype>
#include <iostream>
#include <limits>
#include <string>

#include "iassert.hpp"

#include "elab_scanner.hpp"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

void Elab_scanner::setup_translate() {
  translate.resize(256);

  for (int i = 0; i < 256; i++) {
    if (isalnum(i) || i == '_') {
      translate[i] = Translate_item(Token_id_alnum, true);
    }
  }

  translate['{']  = Token_id_ob;
  translate['}']  = Token_id_cb;
  translate[':']  = Translate_item(Token_id_colon, true);  // Token_id_label
  translate['|']  = Token_id_or;
  translate['.']  = Token_id_dot;
  translate[';']  = Token_id_semicolon;
  translate[',']  = Token_id_comma;
  translate['(']  = Token_id_op;
  translate[')']  = Token_id_cp;
  translate['#']  = Token_id_pound;
  translate['>']  = Translate_item(Token_id_gt, true);  // Token_id_pipe
  translate['*']  = Token_id_mult;
  translate['/']  = Token_id_div;
  translate['"']  = Token_id_string;
  translate['+']  = Token_id_plus;
  translate['-']  = Token_id_minus;
  translate['!']  = Token_id_bang;
  translate['<']  = Token_id_lt;
  translate['=']  = Translate_item(Token_id_eq, true);  // <= >= ==
  translate['&']  = Token_id_and;
  translate['^']  = Token_id_xor;
  translate['?']  = Token_id_qmark;
  translate['\''] = Token_id_tick;

  translate['@'] = Token_id_at;
  translate['$'] = Token_id_dollar;
  translate['%'] = Token_id_percent;

  translate['`'] = Token_id_backtick;

  translate['['] = Token_id_obr;
  translate[']'] = Token_id_cbr;
}

void Elab_scanner::add_token(Token &t) {
  if (t.tok == Token_id_nop) {
    token_list_spaced = true;
    I(!trying_merge);
    return;
  }

  if (likely(!trying_merge || token_list.size() <= 1 || (t.tok == Token_id_nop) || token_list_spaced)) {
    trying_merge      = false;
    token_list_spaced = false;
    token_list.push_back(t);
    return;
  }

  trying_merge    = false;
  Token &last_tok = token_list.back();

  if (last_tok.tok == Token_id_or && t.tok == Token_id_gt) {
    token_list.back().tok = Token_id_pipe;
    token_list.back().len += t.len;
    return;
  } else if (t.tok == Token_id_eq) {    // <=
    if (last_tok.tok == Token_id_lt) {  // <=
      token_list.back().tok = Token_id_le;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_gt) {  // >=
      token_list.back().tok = Token_id_ge;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_eq) {  // ==
      token_list.back().tok = Token_id_same;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_bang) {  // !=
      token_list.back().tok = Token_id_diff;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_colon) {  // :=
      token_list.back().tok = Token_id_coloneq;
      token_list.back().len += t.len;
      return;
    }
  } else if (t.tok == Token_id_alnum) {
    if (last_tok.tok == Token_id_at) {  // @foo
      token_list.back().tok = Token_id_register;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_percent) {  // %foo
      token_list.back().tok = Token_id_output;
      token_list.back().len += t.len;
      return;
    } else if (last_tok.tok == Token_id_dollar) {  // $foo
      token_list.back().tok = Token_id_input;
      token_list.back().len += t.len;
      return;
    }
  } else if (last_tok.tok == Token_id_alnum && t.tok == Token_id_colon) {
    last_tok.tok = Token_id_label;
    // token_list.back().len += t.len;
    return;
  }

  token_list_spaced = false;
  token_list.push_back(t);
}

void Elab_scanner::patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords) {
  for (auto &t : token_list) {
    if (t.tok != Token_id_alnum) continue;

    if (isdigit(buffer[t.pos])) {
      t.tok = Token_id_num;
      continue;
    }

    std::string alnum(&buffer[t.pos], t.len);
    auto        it = keywords.find(alnum);
    if (it == keywords.end()) continue;

    assert(it->second >= static_cast<Token_id>(Token_id_keyword_first));
    assert(it->second <= static_cast<Token_id>(Token_id_keyword_last));

    t.tok = it->second;
  }
}

void Elab_scanner::parse(std::string_view name, std::string_view memblock, bool chunking) {
  buffer_name = name;
  buffer      = memblock;  // To allow error reporting before chunking

  token_list.clear();
  token_list.emplace_back();  // bogus zero entry
  scanner_pos = 1;            // Skip zero to catch bugs

  int  nlines                = 0;
  char last_c                = 0;
  bool in_string_pos         = false;
  bool in_comment            = false;
  bool in_singleline_comment = false;
  int  in_multiline_comment  = 0;  // Nesting support

  std::string_view ptr_section = memblock;

  Token t;
  t.clear(0);

  bool starting_comment  = false;  // Only for comments to avoid /*/* nested back to back */*/
  bool finishing_comment = false;  // Only for comments to avoid /*/* nested back to back */*/
  for (size_t pos = 0; pos < memblock.size(); pos++) {
    char c = memblock[pos];
    // int pos = (&memblock[i] - ptr_section); // same as "i" unless chunking

    t.adjust_len(pos);
    if (c == '\n' || c == '\r' || c == '\f') {
      nlines++;
      if (!in_comment && t.tok != Token_id_nop) {
        add_token(t);
        t.clear(pos);
        trying_merge = false;
      } else {
        starting_comment      = false;
        finishing_comment     = false;
        in_singleline_comment = false;
        in_comment            = in_singleline_comment | in_multiline_comment;
        if (!in_comment) {
          if (t.tok == Token_id_synopsys) {
            scan_warn("synopsys directive (most likely ignored)");
          }

          add_token(t);
          t.clear(pos);
          trying_merge = false;
        }
      }
      if (in_string_pos) {
        add_token(t);
        t.clear(pos);
        trying_merge = false;

        in_string_pos = false;
      }
    } else if (unlikely(last_c == '/' && c == '/')) {
      t.tok        = Token_id_comment;
      trying_merge = false;

      // in the works!!
      if (!in_comment) {
        constexpr size_t len1 = std::char_traits<char>::length("synopsys ");
        size_t           npos = pos + 1;
        while (buffer[npos] == ' ' && npos < memblock.size()) npos++;
        if ((npos + len1) < memblock.size()) {
          if (strncmp(&buffer[npos], "synopsys ", len1) == 0) {
            t.tok = Token_id_synopsys;
          }
        }
      }
      in_singleline_comment = true;
      in_comment            = true;
      assert(!starting_comment);
      assert(!finishing_comment);
    } else if (unlikely(!finishing_comment && ((last_c == '/' && c == '*') ||
                                               (last_c == '(' && c == '*' && (memblock.size() > pos) && buffer[pos + 1] != ')' &&
                                                token_list.size() && token_list.back().tok != Token_id_at)))) {
      t.tok        = Token_id_comment;
      trying_merge = false;

      in_multiline_comment++;
      in_comment       = true;
      starting_comment = true;
      assert(!finishing_comment);
      // The (* foo *) are attributes - not comments - in verilog. Must be handled in the grammar
    } else if (unlikely(!starting_comment && ((last_c == '*' && c == '/') || (in_comment && last_c == '*' && c == ')')))) {
      t.tok        = Token_id_comment;
      trying_merge = false;

      in_multiline_comment--;
      if (in_multiline_comment < 0) {
        scan_error(fmt::format("{}:{} found end of comment without matching beginning of comment", name, nlines));
      } else if (in_multiline_comment == 0) {
        in_singleline_comment = false;
        in_comment            = false;
      }
      assert(!starting_comment);
      finishing_comment = true;
    } else if (in_comment) {
      starting_comment  = false;
      finishing_comment = false;

    } else if (in_string_pos) {
      if (c == '"' && last_c != '\\') {
        add_token(t);
        t.clear(pos);
        trying_merge = false;

        in_string_pos = false;
      }
    } else if (c == '"' && last_c != '\\') {
      add_token(t);
      t.set(Token_id_string, pos + 1);
      trying_merge = false;

      in_string_pos = true;
    } else {
      Token_id nt = translate[c].tok;
      if (t.tok != nt) {
        finishing_comment = false;

        add_token(t);
        t.set(nt, pos);

        trying_merge = translate[c].try_merge;
      }
    }

    last_c = c;
  }
  if (t.tok != Token_id_nop) {
    t.adjust_len(memblock.size());
    add_token(t);
  }

  if (in_multiline_comment) {
    scan_error("scanner reached end of file with a multi-line comment");
  }

  chunked(ptr_section);
}

Elab_scanner::Elab_scanner() {
  setup_translate();
  max_errors   = 1;
  max_warnings = 1024;
  n_errors     = 0;
  n_warnings   = 0;

  buffer = 0;  // just to be clean
}

void Elab_scanner::chunked(std::string_view _buffer) {
  buffer = _buffer;

  elaborate();

  token_list.clear();
  token_list.emplace_back();
  scanner_pos = 1;
}

bool Elab_scanner::scan_next() {
  if (scanner_pos >= token_list.size()) return false;

  scanner_pos = scanner_pos + 1;

  return true;
}

bool Elab_scanner::scan_prev() {
  if (scanner_pos <= 1) return false;
  
  scanner_pos = scanner_pos - 1;
  
  return true;
}

void Elab_scanner::scan_append(std::string &text) const {
  assert(scanner_pos < token_list.size());

  text.append(&buffer[token_list[scanner_pos].pos], token_list[scanner_pos].len);
}

void Elab_scanner::scan_format_append(std::string &text) const {
  assert(scanner_pos < token_list.size());

  int start_pos = token_list[scanner_pos].pos;
  int len       = token_list[scanner_pos].len;
  if (scanner_pos > 0) {
    int last_end_pos = token_list[scanner_pos - 1].pos + token_list[scanner_pos - 1].len;
    len += (start_pos - last_end_pos);
    start_pos = last_end_pos;
  }
  if (len > 0) text.append(&buffer[start_pos], len);
}

void Elab_scanner::scan_prev_append(std::string &text) const {
  assert(scanner_pos < token_list.size());
  int p = scanner_pos - 1;
  if (p < 0) p = 0;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

void Elab_scanner::scan_next_append(std::string &text) const {
  assert(scanner_pos < token_list.size());
  size_t p = scanner_pos + 1;
  if (p >= token_list.size()) p = token_list.size() - 1;
  ;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

std::string Elab_scanner::scan_text() const {
  std::string text;

  scan_append(text);

  return text;
}

int Elab_scanner::scan_calc_lineno() const {
  size_t max_pos = scanner_pos;
  if (max_pos >= token_list.size()) max_pos = token_list.size() - 1;

  int nlines = 0;
  for (size_t i = 0; i < token_list[max_pos].pos; i++) {
    char c = buffer[i];
    nlines += (c == '\n' || c == '\r' || c == '\f') ? 1 : 0;
  }

  return nlines;
}

void Elab_scanner::lex_error(std::string_view text) {
  // lexer can not look at token list

  fmt::print("{}\n", text);
  n_errors++;
  if (n_errors > max_errors) exit(-3);
}
void Elab_scanner::scan_error(std::string_view text) const {
  scan_raw_msg("error", text, true);
  n_errors++;
  if (n_errors > max_errors) exit(-3);
}

void Elab_scanner::scan_warn(std::string_view text) const {
  scan_raw_msg("warning", text, true);
  n_warnings++;
  if (n_warnings > max_warnings) exit(-3);
}

void Elab_scanner::parser_info(std::string_view text) const { scan_raw_msg("info", text, true); }

void Elab_scanner::parser_error(std::string_view text) const {
  scan_raw_msg("error", text, false);
  n_errors++;
  if (n_errors > max_errors) exit(-3);
  // throw std::runtime_error(text);
}

void Elab_scanner::parser_warn(std::string_view text) const {
  scan_raw_msg("warning", text, false);
  n_warnings++;
  if (n_warnings > max_warnings) exit(-3);
}

void Elab_scanner::scan_raw_msg(std::string_view cat, std::string_view text, bool third) const {
  // Look at buffer for previous line change

  if (token_list.size() <= 1) {
    fmt::print(fmt::format("{}:{}:{} {}: {}\n", buffer_name, 0, 0, cat, text));
    return;
  }

  size_t max_pos = scanner_pos;
  if (max_pos >= token_list.size() || scanner_pos == 0) max_pos = token_list.size() - 1;

  size_t line_pos_start = 0;
  for (int i = token_list[max_pos].pos; i > 0; i--) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_start = i;
      break;
    }
  }
  size_t line_pos_end = buffer.size();
  for (size_t i = token_list[max_pos].pos; i < buffer.size(); i++) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_end = i;
      break;
    }
  }

  int line = scan_calc_lineno();
  int col  = token_list[max_pos].pos - line_pos_start;

  std::string line_txt;

  int xtra_col = 0;
  for (size_t i = 0; i < (line_pos_end - line_pos_start); i++) {
    if (buffer[line_pos_start + i] == '\t') {
      line_txt += "  ";  // 2 spaces
      if (static_cast<int>(i) <= col) xtra_col++;
    } else {
      line_txt += buffer[line_pos_start + i];
    }
  }
  col += xtra_col;

  fmt::print("{}:{}:{} {}: ", buffer_name, line, col, cat);
  std::cout << text;  // NOTE: no fmt::print because it can contain {}

  if (buffer[line_pos_start] != '\n') std::cout << std::endl;

  assert(line_pos_end > line_pos_start);
  std::cout << line_txt << "\n";
  // NOTE: line_pos_start points to the last return
  // NOTE: no fmt::print because the text can contain {}

  if (!third) return;

  int len = token_list[max_pos].len;
  if ((token_list[max_pos].pos + len) > line_pos_end) len = line_pos_end - token_list[max_pos].pos;

  std::string third_1(col, ' ');
  std::string third_2(len, '^');
  fmt::print(third_1 + third_2 + "\n");
}

void Elab_scanner::dump_token() const {
  size_t pos = scanner_pos;
  if (pos >= token_list.size()) pos = token_list.size();

  auto &      t = token_list[pos];
  std::string raw(&buffer[t.pos], t.len);
  fmt::print("tok:{} pos:{} len:{} raw:{}\n", t.tok, t.pos, t.len, raw);
}
