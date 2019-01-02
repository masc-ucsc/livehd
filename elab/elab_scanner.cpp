//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <ctype.h>

#include <limits>
#include <iostream>
#include <string>
#include <cctype>

#include "elab_scanner.hpp"

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

void Elab_scanner::setup_translate() {

  translate.resize(256);

  for(int i=0;i<256;i++) {
    if (isalnum(i) || i == '_') {
      translate[i] = TOK_ALNUM | TOK_TRYMERGE;
    }else{
      translate[i] = TOK_NOP;
    }
  }

  translate['{'] = TOK_OB;
  translate['}'] = TOK_CB;
  translate[':'] = TOK_COLON | TOK_TRYMERGE; // TOK_LABEL
  translate['|'] = TOK_OR;
  translate['.'] = TOK_DOT;
  translate[';'] = TOK_SEMICOLON;
  translate[','] = TOK_COMMA;
  translate['('] = TOK_OP;
  translate[')'] = TOK_CP;
  translate['#'] = TOK_POUND;
  translate['>'] = TOK_GT  | TOK_TRYMERGE;  // TOK_PIPE
  translate['*'] = TOK_MUL;
  translate['/'] = TOK_DIV;
  translate['"'] = TOK_STRING; // | TOK_TRYMERGE;  // Anything else until other TOK_STRING
  translate['+'] = TOK_PLUS;
  translate['-'] = TOK_MINUS;
  translate['!'] = TOK_BANG;
  translate['<'] = TOK_LT;
  translate['='] = TOK_EQ | TOK_TRYMERGE; // <= >= == 
  translate['&'] = TOK_AND;
  translate['^'] = TOK_XOR;
  translate['?'] = TOK_QMARK;
  translate['\''] = TOK_TICK;

  translate['@'] = TOK_AT;
  translate['$'] = TOK_DOLLAR;
  translate['%'] = TOK_PERCENT;

  translate['`'] = TOK_BACKTICK;

  translate['['] = TOK_OBR;
  translate[']'] = TOK_CBR;
}

void Elab_scanner::add_token(Token &t) {

  if (!t.tok) {
    token_list_spaced = true;
    return;
  }

  if (likely(!(t.tok & TOK_TRYMERGE))) {
    token_list_spaced = false;
    token_list.push_back(t);
    return;
  }

  t.tok &= ~TOK_TRYMERGE;
  if (!token_list.empty() && !token_list_spaced) {
    Token &last_tok = token_list.back();

    if (last_tok.tok == TOK_OR && t.tok == TOK_GT) {
      token_list.back().tok = TOK_PIPE;
      token_list.back().len += t.len;
      return;
    }else if (t.tok == TOK_EQ) { // <=
      if (last_tok.tok == TOK_LT) { // <=
        token_list.back().tok = TOK_LE;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_GT) { // >=
        token_list.back().tok = TOK_GE;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_EQ) { // ==
        token_list.back().tok = TOK_SAME;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_BANG) { // !=
        token_list.back().tok = TOK_DIFF;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_COLON ) { // :=
        token_list.back().tok = TOK_COLONEQ;
        token_list.back().len += t.len;
        return;
      }
    }else if (t.tok == TOK_ALNUM) {
      if (last_tok.tok == TOK_AT) { // @foo
        token_list.back().tok = TOK_REGISTER;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_PERCENT) { // %foo
        token_list.back().tok = TOK_OUTPUT;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_DOLLAR) { // $foo
        token_list.back().tok = TOK_INPUT;
        token_list.back().len += t.len;
        return;
      }
    }else if (last_tok.tok == TOK_ALNUM && t.tok == TOK_COLON) {
      last_tok.tok = TOK_LABEL;
      // token_list.back().len += t.len;
      return;
    }
  }

  token_list_spaced = false;
  token_list.push_back(t);
}

void Elab_scanner::patch_pass(const absl::flat_hash_map<std::string, uint8_t> &keywords) {
  for(auto &t:token_list) {
    if (t.tok != TOK_ALNUM)
      continue;

    if (isdigit(buffer[t.pos])) {
      t.tok = TOK_NUM;
      continue;
    }
    std::string alnum(&buffer[t.pos], t.len);
    auto it = keywords.find(alnum);
    if (it == keywords.end())
      continue;

    assert(it->second >= TOK_KEYWORD_FIRST);
    assert(it->second <= TOK_KEYWORD_LAST);

    t.tok = it->second;
  }
}

void Elab_scanner::parse(std::string_view name, std::string_view memblock, bool chunking) {

  token_list.clear();

  buffer_name = name;
  buffer      = memblock; // To allow error reporting before chunking
  scanner_pos = 0;

  int  nlines                = 0;
  char last_c                = 0;
  int in_string_pos          = -1;
  bool in_comment            = false;
  bool in_singleline_comment = false;
  int  in_multiline_comment  = 0; // Nesting support

  std::string_view ptr_section = memblock;

  Token t;
  t.pos = 0;
  t.tok = TOK_NOP;

  bool starting_comment=false; // Only for comments to avoid /*/* nested back to back */*/
  bool finishing_comment=false; // Only for comments to avoid /*/* nested back to back */*/
  for(size_t pos = 0; pos < memblock.size(); pos++) {
    char c = memblock[pos];
    //int pos = (&memblock[i] - ptr_section); // same as "i" unless chunking
    if(c == '\n' || c == '\r' || c == '\f') {
      nlines++;
      if (!in_comment && t.tok) {
        t.len = pos - t.pos;
        add_token(t);
        t.tok = TOK_NOP;
        t.pos = pos;
      }else{
        t.len = pos - t.pos;
        starting_comment  = false;
        finishing_comment = false;
        in_singleline_comment = false;
        in_comment            = in_singleline_comment | in_multiline_comment;
        if (!in_comment) {
          add_token(t);
          if (t.tok == TOK_SYNOPSYS) {
            scan_warn("synopsys directive (most likely ignored)");
          }
          t.tok = TOK_NOP;
          t.pos = pos;
        }
      }
      if(in_string_pos>=0) {
        t.len = pos - in_string_pos;
        add_token(t);
        t.tok = TOK_NOP;
        t.pos = pos;
        in_string_pos = -1;
      }
    }else if(unlikely(last_c == '/' && c == '/')) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      // in the works!!
      if (!in_comment) {
        constexpr size_t len1 = std::char_traits<char>::length("synopsys ");
        size_t npos=pos+1;
        while(buffer[npos] == ' ' && npos<memblock.size())
          npos++;
        if ((npos+len1)<memblock.size()) {
          if (strncmp(&buffer[npos], "synopsys ",len1)==0) {
            t.tok = TOK_SYNOPSYS;
          }
        }
      }
      in_singleline_comment = true;
      in_comment = true;
      assert(!starting_comment);
      assert(!finishing_comment);
    }else if(unlikely(!finishing_comment
          && ( (last_c == '/' && c == '*')
            || (last_c == '(' && c == '*' && (memblock.size() > pos) && buffer[pos+1] != ')' && token_list.size() && token_list.back().tok != TOK_AT)
              ))) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      in_multiline_comment++;
      in_comment = true;
      starting_comment  = true;
      assert(!finishing_comment);
      // The (* foo *) are attributes - not comments - in verilog. Must be handled in the grammar
    }else if (unlikely(!starting_comment && ((last_c == '*' && c == '/') || (in_comment && last_c == '*' && c == ')') ))) {
      t.len = pos - t.pos;
      t.tok = TOK_COMMENT;
      in_multiline_comment--;
      if (in_multiline_comment < 0) {
        scan_error(fmt::format("{}:{} found end of comment without matching beginning of comment", name, nlines));
      }else if (in_multiline_comment==0) {
        in_singleline_comment = false;
        in_comment = false;
      }
      assert(!starting_comment);
      finishing_comment = true;
    }else if(in_comment) {
      starting_comment  = false;
      finishing_comment = false;
      t.len = pos - t.pos;
    }else if(in_string_pos>=0) {
      if(c == '"' && last_c != '\\') {
        t.len = pos - in_string_pos;
        add_token(t);
        t.tok = TOK_NOP;
        t.pos = pos;
        in_string_pos = -1;
      }
    }else if(c == '"' && last_c != '\\') {
      t.len = pos - t.pos;
      add_token(t);
      t.tok = TOK_STRING;
      t.pos = pos + 1;
      in_string_pos = pos + 1;
    }else{
      Token_id nt = translate[static_cast<uint8_t>(c)];
      if (t.tok != nt) {
        finishing_comment = false;
        t.len = pos - t.pos;
        add_token(t);
        t.tok = nt;
        t.pos = pos;
      }
    }

    last_c = c;
  }
  if (t.tok) {
    t.len = memblock.size() - t.pos;
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
  n_errors = 0;
  n_warnings = 0;

  buffer = 0; // just to be clean
}

void Elab_scanner::chunked(std::string_view _buffer) {

  buffer = _buffer;

  elaborate();

  token_list.clear();
  scanner_pos = 0;
}

bool Elab_scanner::scan_next() {
  if (scanner_pos >= token_list.size())
    return false;

  scanner_pos++;

  return true;
}

void Elab_scanner::scan_append(std::string &text) const {

  assert(scanner_pos < token_list.size());

  text.append(&buffer[token_list[scanner_pos].pos], token_list[scanner_pos].len);
}

void Elab_scanner::scan_format_append(std::string &text) const {
  assert(scanner_pos < token_list.size());

  int start_pos = token_list[scanner_pos].pos;
  int len = token_list[scanner_pos].len;
  if (scanner_pos>0) {
    int last_end_pos = token_list[scanner_pos-1].pos+token_list[scanner_pos-1].len;
    len += (start_pos - last_end_pos);
    start_pos = last_end_pos;
  }
  if (len>0)
    text.append(&buffer[start_pos], len);
}

void Elab_scanner::scan_prev_append(std::string &text) const {

  assert(scanner_pos < token_list.size());
  int p = scanner_pos-1;
  if (p<0)
    p = 0;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

void Elab_scanner::scan_next_append(std::string &text) const {

  assert(scanner_pos < token_list.size());
  size_t p = scanner_pos+1;
  if (p>=token_list.size())
    p = token_list.size()-1;;

  text.append(&buffer[token_list[p].pos], token_list[p].len);
}

std::string Elab_scanner::scan_text() const {
  std::string text;

  scan_append(text);

  return text;
}

int Elab_scanner::scan_calc_lineno() const {

  size_t max_pos = scanner_pos;
  if (max_pos>=token_list.size())
    max_pos = token_list.size()-1;

  int nlines = 0;
  for(size_t i=0;i<token_list[max_pos].pos;i++) {
    char c = buffer[i];
    nlines += (c == '\n' || c == '\r' || c == '\f')?1:0;
  }

  return nlines;
}

void Elab_scanner::lex_error(std::string_view text) {
  // lexer can not look at token list

  fmt::print("{}\n",text);
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
}
void Elab_scanner::scan_error(std::string_view text) const {
  scan_raw_msg("error", text, true);
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
}

void Elab_scanner::scan_warn(std::string_view text) const {
  scan_raw_msg("warning", text, true);
  n_warnings++;
  if (n_warnings>max_warnings)
    exit(-3);
}

void Elab_scanner::parser_info(std::string_view text) const {
  scan_raw_msg("info", text, true);
}

void Elab_scanner::parser_error(std::string_view text) const {
  scan_raw_msg("error", text, false);
  n_errors++;
  if (n_errors>max_errors)
    exit(-3);
  //throw std::runtime_error(text);
}

void Elab_scanner::parser_warn(std::string_view text) const {
  scan_raw_msg("warning", text, false);
  n_warnings++;
  if (n_warnings>max_warnings)
    exit(-3);
}

void Elab_scanner::scan_raw_msg(std::string_view cat, std::string_view text, bool third) const {

  // Look at buffer for previous line change

  if (token_list.empty()) {
    fmt::print(fmt::format("{}:{}:{} {}: {}\n", buffer_name, 0, 0, cat, text));
    return;
  }

  size_t max_pos = scanner_pos;
  if (max_pos>=token_list.size() || scanner_pos==0)
    max_pos = token_list.size()-1;

  size_t line_pos_start = 0;
  for(int i=token_list[max_pos].pos;i>0;i--) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_start = i;
      break;
    }
  }
  size_t line_pos_end = buffer.size();
  for(size_t i=token_list[max_pos].pos;i<buffer.size();i++) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_end = i;
      break;
    }
  }

  int line = scan_calc_lineno();
  int col  = token_list[max_pos].pos - line_pos_start;

  std::string line_txt;

  int xtra_col=0;
  for(size_t i=0;i<(line_pos_end-line_pos_start);i++) {
    if (buffer[line_pos_start+i]=='\t') {
      line_txt += "  "; // 2 spaces
      if (static_cast<int>(i)<=col)
        xtra_col++;
    }else{
      line_txt += buffer[line_pos_start+i];
    }
  }
  col += xtra_col;

  fmt::print("{}:{}:{} {}: ", buffer_name, line, col, cat);
  std::cout << text; // NOTE: no fmt::print because it can contain {}

  if(buffer[line_pos_start] != '\n')
    std::cout << std::endl;

  assert(line_pos_end>line_pos_start);
  std::cout << line_txt << "\n";
  // NOTE: line_pos_start points to the last return
  // NOTE: no fmt::print because the text can contain {}

  if (!third)
    return;

  int len = token_list[max_pos].len;
  if ((token_list[max_pos].pos + len) > line_pos_end)
    len = line_pos_end - token_list[max_pos].pos;

  std::string third_1(col, ' ');
  std::string third_2(len, '^');
  fmt::print(third_1 + third_2 + "\n");
}

void Elab_scanner::dump_token() const {
  size_t pos = scanner_pos;
  if (pos>=token_list.size())
    pos = token_list.size();

  auto &t = token_list[pos];
  std::string raw(&buffer[t.pos], t.len);
  fmt::print("tok:{} pos:{} len:{} raw:{}\n",t.tok, t.pos, t.len, raw);
}

