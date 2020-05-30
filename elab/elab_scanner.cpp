//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cctype>
#include <iostream>
#include <limits>
#include <string>

#include "iassert.hpp"

#include "elab_scanner.hpp"
#include "likely.hpp"

void Elab_scanner::setup_translate() {
  translate.resize(256);

  for (int i = 0; i < 256; i++) {
    if (isalnum(i) || i == '_') {
      translate[i] = Translate_item(Token_id_alnum, true);
    }
  }

  translate['{']  = Token_id_ob;
  translate['}']  = Token_id_cb;
  translate[':']  = Token_id_colon;
  translate['|']  = Token_id_or;
  translate['.']  = Token_id_dot;
  translate[';']  = Token_id_semicolon;
  translate[',']  = Token_id_comma;
  translate['(']  = Token_id_op;
  translate[')']  = Token_id_cp;
  translate['#']  = Translate_item(Token_id_pound, true); // @#
  translate['>']  = Translate_item(Token_id_gt, true);  // Token_id_pipe
  translate['*']  = Token_id_mult;
  translate['/']  = Token_id_div;
  translate['"']  = Token_id_string;
  translate['+']  = Token_id_plus;
  translate['-']  = Token_id_minus;
  translate['!']  = Token_id_bang;
  translate['<']  = Token_id_lt;
  translate['=']  = Translate_item(Token_id_eq, true);  // <= >= == :=
  translate['&']  = Token_id_and;
  translate['^']  = Token_id_xor;
  translate['?']  = Translate_item(Token_id_qmark, true); // 0b???
  translate['\''] = Token_id_tick;

  translate['@'] = Token_id_at;
  translate['~'] = Token_id_tilde;
  translate['$'] = Translate_item(Token_id_dollar, true); // @$
  translate['%'] = Translate_item(Token_id_percent, true); // @%

  translate['`'] = Token_id_backtick;

  translate['['] = Token_id_obr;
  translate[']'] = Token_id_cbr;
  translate['\\'] = Token_id_backslash;
}

void Elab_scanner::add_token(Token &t) {
  if (t.tok == Token_id_nop) {
    token_list_spaced = true;
    I(!trying_merge);
    return;
  }

  if (likely(!trying_merge || token_list_spaced)) {
    trying_merge      = false;
    token_list_spaced = false;
    token_list.push_back(t);
    return;
  }

  trying_merge    = false;
  Token &last_tok = token_list.back();

  if (last_tok.tok == Token_id_or && t.tok == Token_id_gt) {
    token_list.back().fuse_token(Token_id_pipe, t);
    return;
  } else if (t.tok == Token_id_eq) {    // <=
    if (last_tok.tok == Token_id_lt) {  // <=
      token_list.back().fuse_token(Token_id_le, t);
      return;
    } else if (last_tok.tok == Token_id_gt) {  // >=
      token_list.back().fuse_token(Token_id_ge, t);
      return;
    } else if (last_tok.tok == Token_id_eq) {  // ==
      token_list.back().fuse_token(Token_id_same, t);
      return;
    } else if (last_tok.tok == Token_id_bang) {  // !=
      token_list.back().fuse_token(Token_id_diff, t);
      return;
    } else if (last_tok.tok == Token_id_colon) {  // :=
      token_list.back().fuse_token(Token_id_coloneq, t);
      return;
    }
  } else if (t.tok == Token_id_qmark) {
    if (last_tok.tok == Token_id_alnum) {
      if (last_tok.text[0] == '0') {
        if (last_tok.text.size() >= 2 && (last_tok.text[1] == 'b' || last_tok.text[1] == 'B')) {
          token_list.back().fuse_token(Token_id_alnum, t);
          return;
        }
      }
    }
  } else if (t.tok == Token_id_alnum) {
    if (last_tok.tok == Token_id_pound) {  // #foo
      token_list.back().fuse_token(Token_id_register, t);
      return;
    } else if (last_tok.tok == Token_id_percent) {  // %foo
      token_list.back().fuse_token(Token_id_output, t);
      return;
    } else if (last_tok.tok == Token_id_dollar) {   // $foo
      token_list.back().fuse_token(Token_id_input, t);
      return;
    } else if (last_tok.tok == Token_id_at) {        // @foo
      token_list.back().fuse_token(Token_id_reference, t);
      return;
    } else if (last_tok.tok == Token_id_alnum || last_tok.tok == Token_id_register || last_tok.tok == Token_id_output
                                              || last_tok.tok == Token_id_input || last_tok.tok == Token_id_reference) {  // foo
      token_list.back().append_token(t);
      return;
    }
  } else if (last_tok.tok == Token_id_at) {
    if (t.tok == Token_id_dollar) {  // @$
      token_list.back().fuse_token(Token_id_reference, t);
      return;
    } else if (t.tok == Token_id_pound) {  // @#
      token_list.back().fuse_token(Token_id_reference, t);
      return;
    } else if (t.tok == Token_id_percent) {  // @%
      token_list.back().fuse_token(Token_id_reference, t);
      return;
    }
  }

  token_list_spaced = false;
  token_list.push_back(t);
}

void Elab_scanner::patch_pass(const absl::flat_hash_map<std::string, Token_id> &keywords) {

  for (size_t i=0;i<token_list.size();++i) {
    auto &t = token_list[i];
    if (t.tok != Token_id_alnum) continue;

    I(t.get_text().size()>0); // at least a character

    std::string_view txt = t.get_text();

    if (isdigit(txt[0])) {
      t.tok = Token_id_num;

#ifndef NDEBUG
      // binary accepts ? as part of the numeric constant
      if (txt.size() > 2 && (txt[1] == 'b' || txt[1] == 'B')) {
        if ((i+1)<token_list.size())
          I(token_list[i+1].tok != Token_id_qmark);
      }
#endif

      continue;
    }

    auto it = keywords.find(txt);
    if (it == keywords.end()) continue;

    assert(it->second >= static_cast<Token_id>(Token_id_keyword_first));
    assert(it->second <= static_cast<Token_id>(Token_id_keyword_last));

    t.tok = it->second;
  }
}

void Elab_scanner::parse_setup(std::string_view filename) {
  if(memblock_fd==-1) {
    unregister_memblock();
  }

  memblock_fd = open(std::string(filename).c_str(), O_RDONLY);
  if (memblock_fd < 0) {
    parser_error("::parse could not open file {}", filename);
    return;
  }

  struct stat sb;
  fstat(memblock_fd, &sb);

  char *b = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, memblock_fd, 0);
  if (b == MAP_FAILED) {
    parser_error("parse mmap failed for file {} with size {}", filename, sb.st_size);
    return;
  }

  memblock = std::string_view(b, sb.st_size);
  buffer_name = filename;

  token_list.clear();
}

void Elab_scanner::parse_setup() {
  if(memblock_fd==-1) {
    unregister_memblock();
  }

  memblock_fd = -1;
  buffer_name = "inline";

  token_list.clear();
}

static inline bool is_newline(char c) {
  return c == '\n' || c == '\r' || c == '\f';
}

void Elab_scanner::parse_step() {

  I(token_list.empty());

  token_list.emplace_back();  // bogus zero entry
  scanner_pos = 1;            // Skip zero to catch bugs

  int  nlines                = 0;
  char last_c                = 0;
  bool in_string_pos         = false;
  bool in_comment            = false;
  bool in_singleline_comment = false;
  int  in_multiline_comment  = 0;  // Nesting support

  Token t;
  t.clear(Token_id_nop, 0, std::string_view{&memblock[0],0});

  bool starting_comment  = false;  // Only for comments to avoid /*/* nested back to back */*/
  bool finishing_comment = false;  // Only for comments to avoid /*/* nested back to back */*/
  for (size_t pos = 0; pos < memblock.size(); pos++) {
    char c = memblock[pos];

    t.adjust_token_size(pos);
    if (unlikely(is_newline(c))) {
      nlines++;
      if (!in_comment && t.tok != Token_id_nop) {
        add_token(t);
        t.clear(pos, nlines, std::string_view(&memblock[pos],0));
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
          t.clear(pos, nlines, std::string_view(&memblock[pos],0));
          trying_merge = false;
        }
      }
      if (in_string_pos) {
        add_token(t);
        t.clear(pos, nlines, std::string_view(&memblock[pos],0));
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
        while (memblock[npos] == ' ' && npos < memblock.size()) npos++;
        if ((npos + len1) < memblock.size()) {
          if (strncmp(&memblock[npos], "synopsys ", len1) == 0) {
            t.tok = Token_id_synopsys;
          }
        }
      }
      in_singleline_comment = true;
      in_comment            = true;
      assert(!starting_comment);
      assert(!finishing_comment);
    } else if (unlikely(!finishing_comment && ((last_c == '/' && c == '*') ||
                                               (last_c == '(' && c == '*' && (memblock.size() > pos) && memblock[pos + 1] != ')' &&
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
        scan_error("{}:{} found end of comment without matching beginning of comment", buffer_name, nlines);
      } else if (in_multiline_comment == 0) {
        in_singleline_comment = false;
        in_comment            = false;
      }
      assert(!starting_comment);
      finishing_comment = true;
    } else if (unlikely(in_comment)) {
      starting_comment  = false;
      finishing_comment = false;

      if (in_singleline_comment) {
        if(!is_newline(memblock[pos]) && (pos+1)<memblock.size()) {
          // TODO: Convert this to a word base (not byte based) skip
          while(!is_newline(memblock[pos+1]) && (pos+3)<memblock.size())
            pos++;
        }
      }else{
        if(!is_newline(memblock[pos]) && memblock[pos]!='*' && memblock[pos]!='/' && (pos+3)<memblock.size()) {
          // TODO: Convert this to a word base (not byte based) skip
          while(!is_newline(memblock[pos+1]) && memblock[pos+1]!='*' && memblock[pos+1]!='/' && (pos+3)<memblock.size())
            pos++;
        }
      }

    } else if (unlikely(in_string_pos)) {
      if (c == '"' && last_c != '\\') {
        add_token(t);
        t.clear(pos, nlines, std::string_view(&memblock[pos],0));
        trying_merge = false;

        in_string_pos = false;
      }
    } else if (unlikely(c == '"' && last_c != '\\')) {
      add_token(t);
      t.reset(Token_id_string, pos + 1, nlines, std::string_view(&memblock[pos+1],1));
      trying_merge = false;

      in_string_pos = true;
    } else {
      Token_id nt = translate[c].tok;
      finishing_comment = false;

      add_token(t);
      t.reset(nt, pos, nlines, std::string_view(&memblock[pos],1));
      trying_merge = translate[c].try_merge;
    }

    last_c = c;
  }
  if (t.tok != Token_id_nop) {
    t.adjust_token_size(memblock.size());
    add_token(t);
  }

  if (in_multiline_comment) {
    scan_error("scanner reached end of file with a multi-line comment");
  }

  elaborate(); //build ast

  scanner_pos = 1;
}

Elab_scanner::Elab_scanner() {
  setup_translate();
  max_errors   = 1;
  max_warnings = 0; // Unlimited 1024;
  n_errors     = 0;
  n_warnings   = 0;

  memblock = "";
  memblock_fd = -1;
}

void Elab_scanner::unregister_memblock() {
  if (memblock_fd==-1)
    return;

  int ok = munmap((void *)memblock.data(), memblock.size());
  I(ok==0);
  close(memblock_fd);

  memblock = "";
  memblock_fd = -1;
}

Elab_scanner::~Elab_scanner() {
  unregister_memblock();
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

void Elab_scanner::scan_format_append(std::string &text) const {
  assert(scanner_pos < token_list.size());
  if (scanner_pos > 0) {
    if (token_list[scanner_pos-1].line != token_list[scanner_pos].line) {
      absl::StrAppend(&text, "\n", token_list[scanner_pos].get_text());
      return;
    }
    absl::StrAppend(&text, " ", token_list[scanner_pos].get_text());
    return;
  }
  absl::StrAppend(&text, token_list[scanner_pos].get_text());
}

uint32_t Elab_scanner::scan_line() const {
  size_t max_pos = scanner_pos;
  if (max_pos >= token_list.size()) max_pos = token_list.size() - 1;
  return token_list[max_pos].line;
}

void Elab_scanner::lex_error(std::string_view text) {
  // lexer can not look at token list

  fmt::print("{}\n", text);
  n_errors++;
  throw std::runtime_error(std::string(text));
}
void Elab_scanner::scan_error(std::string_view text) const {
  scan_raw_msg("error", text, true);
  n_errors++;
  throw std::runtime_error(std::string(text));
}

void Elab_scanner::scan_warn(std::string_view text) const {
  scan_raw_msg("warning", text, true);
  n_warnings++;
  if (n_warnings > max_warnings)
    throw std::runtime_error("too many warnings");
}

void Elab_scanner::parser_info(std::string_view text) const { scan_raw_msg("info", text, true); }

void Elab_scanner::parser_error(std::string_view text) const {
  scan_raw_msg("error", text, false);
  n_errors++;
  //if (n_errors > max_errors) exit(-3);
  throw std::runtime_error(std::string(text));
}

void Elab_scanner::parser_warn(std::string_view text) const {
  scan_raw_msg("warning", text, false);
  n_warnings++;
  if (max_warnings && n_warnings > max_warnings)
    throw std::runtime_error("too many warnings");
}

void Elab_scanner::scan_raw_msg(std::string_view cat, std::string_view text, bool third) const {

  if (token_list.size() <= 1) {
    fmt::print("{}:{}:{} {}: {}\n", buffer_name, 0, 0, cat, text);
    return;
  }

  size_t max_pos = scanner_pos;
  if (max_pos >= token_list.size() || scanner_pos.value == 0) max_pos = token_list.size() - 1;

  size_t line_pos_start = 0;
  for (int i = token_list[max_pos].pos1; i > 0; i--) {
    if (is_newline(memblock[i])) {
      line_pos_start = i;
      break;
    }
  }
  size_t line_pos_end = memblock.size();
  for (size_t i = token_list[max_pos].pos1; i < memblock.size(); i++) {
    if (is_newline(memblock[i])) {
      line_pos_end = i;
      break;
    }
  }

  auto line = scan_line();
  int s_col  = token_list[max_pos].pos1 - line_pos_start;
  I(s_col>=0);
  size_t col = s_col;

  std::string line_txt;

  size_t xtra_col = 0;
  for (size_t i = 0; i < (line_pos_end - line_pos_start); i++) {
    if (memblock[line_pos_start + i] == '\t') {
      line_txt += "  ";  // 2 spaces
      if (i <= col) xtra_col++;
    } else {
      line_txt += memblock[line_pos_start + i];
    }
  }
  col += xtra_col;

  fmt::print("{}:{}:{} {}: ", buffer_name, line, col, cat);
  std::cout << text;  // NOTE: no fmt::print because it can contain {}

  if (!is_newline(memblock[line_pos_start])) std::cout << std::endl;

  assert(line_pos_end > line_pos_start);
  std::cout << line_txt << "\n";
  // NOTE: line_pos_start points to the last return
  // NOTE: no fmt::print because the text can contain {}

  if (!third) return;

  int len = token_list[max_pos].get_text().size();
  if ((token_list[max_pos].pos1 + len) > line_pos_end) len = line_pos_end - token_list[max_pos].pos1;

  std::string third_1(col, ' ');
  std::string third_2(len, '^');
  fmt::print(third_1 + third_2 + "\n");
}

void Elab_scanner::dump_token() const {
  size_t pos = scanner_pos;
  if (pos >= token_list.size()) pos = token_list.size();

  auto &t = token_list[pos];
  fmt::print("tok:{} pos1:{}, pos2:{}, line:{} text:{}\n", t.tok, t.pos1, t.pos2, t.line, t.text);
}
