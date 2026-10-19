//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sexp.hpp"

#include <cctype>

namespace livehd::aegraph::rulegen {

namespace {

class Reader {
public:
  Reader(std::string_view text, std::string_view file) : text_(text), file_(file) {}

  std::vector<Sexp> all() {
    std::vector<Sexp> out;
    skip();
    while (pos_ < text_.size()) {
      out.push_back(one());
      skip();
    }
    return out;
  }

private:
  [[noreturn]] void fail(int line, const std::string& msg) const {
    throw Syntax_error{std::string{file_} + ":" + std::to_string(line) + ": error: " + msg};
  }

  void skip() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == '\n') {
        ++line_;
        ++pos_;
      } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
        ++pos_;
      } else if (c == ';') {
        while (pos_ < text_.size() && text_[pos_] != '\n') {
          ++pos_;
        }
      } else {
        break;
      }
    }
  }

  static bool atom_char(char c) {
    return std::isspace(static_cast<unsigned char>(c)) == 0 && c != '(' && c != ')' && c != '[' && c != ']' && c != '{' && c != '}'
           && c != '"' && c != ';';
  }

  Sexp one() {
    Sexp s;
    s.line     = line_;
    const char c = text_[pos_];
    if (c == '(' || c == '[') {
      const char close = c == '(' ? ')' : ']';
      s.kind           = Sexp::Kind::list;
      s.open           = c;
      ++pos_;
      for (;;) {
        skip();
        if (pos_ >= text_.size()) {
          fail(s.line, std::string{"unterminated '"} + c + "'");
        }
        if (text_[pos_] == close) {
          ++pos_;
          break;
        }
        if (text_[pos_] == ')' || text_[pos_] == ']' || text_[pos_] == '}') {
          fail(line_, std::string{"unexpected '"} + text_[pos_] + "'");
        }
        s.items.push_back(one());
      }
      return s;
    }
    if (c == '"') {
      s.kind = Sexp::Kind::str;
      ++pos_;
      for (;;) {
        if (pos_ >= text_.size()) {
          fail(s.line, "unterminated string");
        }
        const char d = text_[pos_++];
        if (d == '"') {
          break;
        }
        if (d == '\\' && pos_ < text_.size()) {
          const char e = text_[pos_++];
          s.text += e == 'n' ? '\n' : e;
          continue;
        }
        if (d == '\n') {
          ++line_;
        }
        s.text += d;
      }
      return s;
    }
    if (c == '{' || c == '}' || c == ')' || c == ']') {
      fail(line_, std::string{"unexpected '"} + c + "'");
    }
    s.kind = Sexp::Kind::atom;
    while (pos_ < text_.size() && atom_char(text_[pos_])) {
      s.text += text_[pos_++];
    }
    if (pos_ < text_.size() && text_[pos_] == '{') {
      ++pos_;
      std::string cur;
      for (;;) {
        if (pos_ >= text_.size()) {
          fail(s.line, "unterminated '{'");
        }
        const char d = text_[pos_++];
        if (d == '}') {
          break;
        }
        if (d == ',' || std::isspace(static_cast<unsigned char>(d)) != 0) {
          if (!cur.empty()) {
            s.attrs.push_back(cur);
            cur.clear();
          }
          continue;
        }
        cur += d;
      }
      if (!cur.empty()) {
        s.attrs.push_back(cur);
      }
    }
    return s;
  }

  std::string_view text_;
  std::string_view file_;
  size_t           pos_  = 0;
  int              line_ = 1;
};

}  // namespace

std::vector<Sexp> parse_sexps(std::string_view text, std::string_view file) { return Reader(text, file).all(); }

std::string to_text(const Sexp& s) {
  switch (s.kind) {
    case Sexp::Kind::atom: {
      std::string t = s.text;
      if (!s.attrs.empty()) {
        t += "{";
        for (size_t i = 0; i < s.attrs.size(); ++i) {
          t += (i != 0 ? "," : "") + s.attrs[i];
        }
        t += "}";
      }
      return t;
    }
    case Sexp::Kind::str: return "\"" + s.text + "\"";
    default: {
      std::string t(1, s.open);
      for (size_t i = 0; i < s.items.size(); ++i) {
        t += (i != 0 ? " " : "") + to_text(s.items[i]);
      }
      t += s.open == '(' ? ')' : ']';
      return t;
    }
  }
}

}  // namespace livehd::aegraph::rulegen
