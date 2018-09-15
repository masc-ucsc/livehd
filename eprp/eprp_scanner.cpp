
#include <ctype.h>

#include <limits>

#include "eprp_scanner.hpp"

void Eprp_scanner::setup_translate() {

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
  translate['>'] = TOK_GT  | TOK_TRYMERGE;  // TOK_PIPE
  translate['/'] = TOK_DIV;

  translate['@'] = TOK_AT;
  translate['$'] = TOK_DOLLAR;
  translate['%'] = TOK_PERCENT;

}

void Eprp_scanner::add_token(Token t) {

  if (!t.tok) {
    token_list_spaced = true;
    return;
  }

  if (t.tok & TOK_TRYMERGE) {
    t.tok &= ~TOK_TRYMERGE;
    if (!token_list.empty() && !token_list_spaced) {
      Token last_tok = token_list.back();
      if (last_tok.tok == TOK_OR && t.tok == TOK_GT) {
        token_list.back().tok = TOK_PIPE;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_AT && t.tok == TOK_ALNUM) {
        token_list.back().tok = TOK_REGISTER;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_PERCENT && t.tok == TOK_ALNUM) {
        token_list.back().tok = TOK_OUTPUT;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_DOLLAR && t.tok == TOK_ALNUM) {
        token_list.back().tok = TOK_INPUT;
        token_list.back().len += t.len;
        return;
      }else if (last_tok.tok == TOK_ALNUM && t.tok == TOK_COLON) {
        token_list.back().tok = TOK_LABEL;
        // token_list.back().len += t.len;
        return;
      }
    }
  }

  token_list_spaced = false;
  token_list.push_back(t);
}

void Eprp_scanner::parse(std::string name, const char *memblock, size_t sz) {

  buffer_name = name;
  buffer = 0;
  buffer_sz = 0;
  buffer_start_pos = 0;
  buffer_start_line = 0;
  scanner_pos = 0;

  int  nlines                = 0;
  int  nchunks               = 1;
  int  nopen                 = 0;
  char last_c                = 0;
  bool in_comment            = false;
  bool in_singleline_comment = false;
  bool in_multiline_comment  = false;

  const char *ptr_section = memblock;

  Token t;
  t.pos = 0;
  t.tok = TOK_NOP;

  for(size_t i = 0; i < sz; i++) {
    char c = memblock[i];
    int pos = (&memblock[i] - ptr_section);
    if(c == '#') {
      if (t.tok!= TOK_COMMENT && t.tok) {
        t.len = pos - t.pos;
        add_token(t);
        t.pos = pos;
        t.tok = TOK_COMMENT;
      }
      if(!in_comment && sz > (i + 2) && memblock[i + 1] == '#' && memblock[i + 2] == '#')
        in_multiline_comment = !in_multiline_comment;
      else
        in_singleline_comment = true;
      in_comment = in_singleline_comment | in_multiline_comment;
    } else if(c == '\n' || c == '\r' || c == '\f') {
      nlines++;
      in_singleline_comment = false;
      in_comment            = in_singleline_comment | in_multiline_comment;
      if (!in_comment && t.tok) {
        t.len = pos - t.pos;
        add_token(t);
        t.tok = TOK_NOP;
        t.pos = pos;
      }

      // At least MIN_CHUNK_SIZE characters, and not close to the end of file
      if(nopen == 0 && (ptr_section + MIN_CHUNK_SIZE) < &memblock[i] && !in_comment && (last_c == '\n' || last_c == '\r' || last_c == '\f') &&
         ((i + MIN_CHUNK_SIZE) < sz) && !token_list.empty()) {
        chunked(ptr_section, (&memblock[i] -ptr_section), ptr_section - memblock, nlines);
        ptr_section = &memblock[i + 1];
        nchunks++;
      }
    } if(!in_comment) {
      Token_id nt = translate[static_cast<uint8_t>(c)];
      if (t.tok != nt) {
        t.len = pos - t.pos;
        add_token(t);
        t.tok = nt;
        t.pos = pos;
      }
    }

    last_c = c;
  }

  if (&memblock[sz-1] != ptr_section)
    chunked(ptr_section, (&memblock[sz-1] -ptr_section), ptr_section - memblock, nlines);
}

Eprp_scanner::Eprp_scanner() {
  setup_translate();
}

void Eprp_scanner::chunked(const char *_buffer, size_t _buffer_sz, size_t _buffer_start_pos, size_t _buffer_start_line) {

  assert(_buffer_sz < std::numeric_limits<uint32_t>::max());

  // TODO?: we could multithread each chunk. buffer_*, scanner_pos, and token_list should be privatized per thread

  buffer = _buffer;
  buffer_sz = static_cast<uint32_t>(_buffer_sz);
  buffer_start_pos = _buffer_start_pos;
  buffer_start_line = _buffer_start_line;

  assert(token_list.size());

  elaborate();

  token_list.clear();
  scanner_pos = 0;
}

bool Eprp_scanner::scan_next() {
  if ((scanner_pos+1) >= token_list.size())
    return false;

  scanner_pos++;

  return true;
}

void Eprp_scanner::scan_append(std::string &text) const {

  assert(scanner_pos < token_list.size());

  text.append(&buffer[token_list[scanner_pos].pos], token_list[scanner_pos].len);
}

std::string Eprp_scanner::scan_text() const {
  std::string text;

  scan_append(text);

  return text;
}

int Eprp_scanner::scan_calc_lineno() const {

  int nlines = 0;
  for(size_t i=0;i<token_list[scanner_pos].pos;i++) {
    char c = buffer[i];
    nlines += (c == '\n' || c == '\r' || c == '\f')?1:0;
  }

  return buffer_start_line + nlines;
}

void Eprp_scanner::scan_error(const std::string &text) const {

  // Look at buffer for previous line change

  int line_pos_start = 0;
  for(int i=token_list[scanner_pos].pos;i>0;i--) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_start = i;
      break;
    }
  }
  int line_pos_end = buffer_sz;
  for(size_t i=token_list[scanner_pos].pos;i<buffer_sz;i++) {
    char c = buffer[i];
    if (c == '\n' || c == '\r' || c == '\f') {
      line_pos_end = i;
      break;
    }
  }

  std::string line_txt(&buffer[line_pos_start],line_pos_end-line_pos_start);

  int line = scan_calc_lineno();
  int col  = token_list[scanner_pos].pos - line_pos_start;

  std::string first_1 = fmt::format("{}:{}:{} error: {}", buffer_name, line, col, text);
  std::string second_1 = line_txt;

  std::string third_1(col, ' ');
  std::string third_2(token_list[scanner_pos].len, '^');

  fmt::print(first_1 + "\n");
  fmt::print(second_1 + "\n");
  fmt::print(third_1 + third_2 + "\n");
}

