// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc_tcl.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <regex>
#include <utility>

namespace livehd::timing::fast::tcl {
namespace {

constexpr char no_default[] = "\x01";

bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool white(char c) { return blank(c) || c == '\n'; }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string_view trim(std::string_view s) {
  while (!s.empty() && white(s.front())) {
    s.remove_prefix(1);
  }
  while (!s.empty() && white(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

// The character a backslash sequence at text[i] (the backslash) stands for; `i` moves past it.
std::string unescape(std::string_view text, size_t& i) {
  if (i + 1 >= text.size()) {
    ++i;
    return "\\";
  }
  const char c = text[i + 1];
  i += 2;
  switch (c) {
    case 'n': return "\n";
    case 't': return "\t";
    case 'r': return "\r";
    case 'a': return "\a";
    case 'b': return "\b";
    case 'f': return "\f";
    case 'v': return "\v";
    case '\n':
      while (i < text.size() && blank(text[i])) {
        ++i;
      }
      return " ";
    default: return std::string(1, c);
  }
}

long long index_of(std::string_view text, long long size) {
  text = trim(text);
  if (text.starts_with("end")) {
    long long offset = 0;
    if (text.size() > 3) {
      double v = 0;
      if (!to_number(text.substr(3), v)) {
        throw Error("bad index \"" + std::string(text) + "\"");
      }
      offset = static_cast<long long>(v);
    }
    return size - 1 + offset;
  }
  double v = 0;
  if (!to_number(text, v)) {
    throw Error("bad index \"" + std::string(text) + "\"");
  }
  return static_cast<long long>(v);
}

std::regex make_regex(const std::string& pattern, bool nocase) {
  try {
    return std::regex(pattern, nocase ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript);
  } catch (const std::regex_error&) {
    throw Error("regular expression \"" + pattern + "\" is not supported");
  }
}

}  // namespace

bool match(std::string_view p, std::string_view s, bool nocase) {
  const auto eq = [&](char a, char b) { return nocase ? lower(a) == lower(b) : a == b; };
  size_t     pi = 0, si = 0, star = std::string_view::npos, mark = 0;
  while (si < s.size()) {
    bool   ok   = false;
    size_t next = pi;
    if (pi < p.size()) {
      char c = p[pi];
      if (c == '*') {
        star = pi++;
        mark = si;
        continue;
      }
      if (c == '?') {
        ok   = true;
        next = pi + 1;
      } else if (c == '[') {
        size_t j   = pi + 1;
        bool   hit = false;
        while (j < p.size() && p[j] != ']') {
          char lo = p[j];
          if (lo == '\\' && j + 1 < p.size()) {
            lo = p[++j];
          }
          char hi = lo;
          if (j + 2 < p.size() && p[j + 1] == '-' && p[j + 2] != ']') {
            hi  = p[j + 2];
            j  += 2;
          }
          const char x = nocase ? lower(s[si]) : s[si];
          if (x >= (nocase ? lower(lo) : lo) && x <= (nocase ? lower(hi) : hi)) {
            hit = true;
          }
          ++j;
        }
        if (j < p.size()) {
          ok   = hit;
          next = j + 1;
        } else {
          ok   = eq('[', s[si]);
          next = pi + 1;
        }
      } else {
        next = pi + 1;
        if (c == '\\' && pi + 1 < p.size()) {
          c    = p[pi + 1];
          next = pi + 2;
        }
        ok = eq(c, s[si]);
      }
    }
    if (ok) {
      pi = next;
      ++si;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      si = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') {
    ++pi;
  }
  return pi == p.size();
}

std::vector<std::string> list_split(std::string_view s) {
  std::vector<std::string> out;
  size_t                   i = 0;
  while (true) {
    while (i < s.size() && white(s[i])) {
      ++i;
    }
    if (i >= s.size()) {
      break;
    }
    std::string element;
    if (s[i] == '{') {
      int          depth = 1;
      const size_t begin = ++i;
      for (; i < s.size() && depth > 0; ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          ++i;
        } else if (s[i] == '{') {
          ++depth;
        } else if (s[i] == '}') {
          --depth;
        }
      }
      if (depth != 0) {
        throw Error("unmatched open brace in list");
      }
      element = std::string(s.substr(begin, i - 1 - begin));
    } else if (s[i] == '"') {
      ++i;
      while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
          element += unescape(s, i);
        } else {
          element += s[i++];
        }
      }
      if (i >= s.size()) {
        throw Error("unmatched open quote in list");
      }
      ++i;
    } else {
      while (i < s.size() && !white(s[i])) {
        if (s[i] == '\\') {
          element += unescape(s, i);
        } else {
          element += s[i++];
        }
      }
    }
    out.push_back(std::move(element));
  }
  return out;
}

std::string list_quote(std::string_view e) {
  if (e.empty()) {
    return "{}";
  }
  bool needs = e.front() == '#';
  int  depth = 0;
  bool balanced = true;
  for (size_t i = 0; i < e.size(); ++i) {
    const char c = e[i];
    if (white(c) || c == '[' || c == ']' || c == '$' || c == ';' || c == '"' || c == '\\' || c == '{' || c == '}') {
      needs = true;
    }
    if (c == '\\') {
      balanced = false;  // keep it simple: a backslash is escaped, not braced
    } else if (c == '{') {
      ++depth;
    } else if (c == '}' && --depth < 0) {
      balanced = false;
    }
  }
  if (!needs) {
    return std::string(e);
  }
  if (balanced && depth == 0) {
    return "{" + std::string(e) + "}";
  }
  std::string out;
  for (const char c : e) {
    if (white(c) || c == '[' || c == ']' || c == '$' || c == ';' || c == '"' || c == '\\' || c == '{' || c == '}') {
      out += '\\';
    }
    out += c == '\n' ? 'n' : c;
  }
  return out;
}

std::string list_join(const std::vector<std::string>& elements) {
  std::string out;
  for (size_t i = 0; i < elements.size(); ++i) {
    if (i != 0) {
      out += ' ';
    }
    out += list_quote(elements[i]);
  }
  return out;
}

std::string number_text(double value, bool integer) {
  char buffer[64];
  if (integer) {
    const auto r = std::to_chars(buffer, buffer + sizeof buffer, static_cast<long long>(value));
    return {buffer, r.ptr};
  }
  if (!std::isfinite(value)) {
    return std::isnan(value) ? "NaN" : value > 0 ? "Inf" : "-Inf";
  }
  const auto  r = std::to_chars(buffer, buffer + sizeof buffer, value);
  std::string text(buffer, r.ptr);
  if (text.find_first_of(".en") == std::string::npos) {
    text += ".0";
  }
  return text;
}

bool to_number(std::string_view text, double& value, bool* integer) {
  text = trim(text);
  if (text.empty()) {
    return false;
  }
  bool negative = false;
  if (text.front() == '+' || text.front() == '-') {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    unsigned long long v = 0;
    const auto         r = std::from_chars(text.data() + 2, text.data() + text.size(), v, 16);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
      return false;
    }
    value = negative ? -static_cast<double>(v) : static_cast<double>(v);
    if (integer != nullptr) {
      *integer = true;
    }
    return true;
  }
  if (!(std::isdigit(static_cast<unsigned char>(text.front())) || text.front() == '.')) {
    return false;  // no inf, nan or names
  }
  double     v = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
  if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
    return false;
  }
  value = negative ? -v : v;
  if (integer != nullptr) {
    *integer = text.find_first_not_of("0123456789") == std::string_view::npos;
  }
  return true;
}

// ---- expr -----------------------------------------------------------------------------------------------

// A recursive-descent evaluation of a Tcl expression. Operands are numbers, strings in quotes or braces,
// `$variables`, `[commands]` and function calls; `skip` parses without evaluating (the untaken side of `&&`,
// `||` and `?:`).
class Expression {
public:
  Expression(Interp& interp, std::string_view text) : interp_(interp), s_(text) {}
  std::string run() {
    const auto v = ternary(false);
    blanks();
    if (i_ != s_.size()) {
      throw Error("expr: unexpected \"" + std::string(s_.substr(i_)) + "\"");
    }
    return text_of(v);
  }

private:
  struct Value {
    bool        number = false, integer = false;
    double      v      = 0;
    std::string text;
  };
  static Value num(double v, bool integer) { return {true, integer, integer ? std::trunc(v) : v, {}}; }
  static Value from(std::string text) {
    Value value;
    if (to_number(text, value.v, &value.integer)) {
      value.number = true;
    } else {
      value.text = std::move(text);
    }
    return value;
  }
  static std::string text_of(const Value& v) { return v.number ? number_text(v.v, v.integer) : v.text; }
  static bool        truth(const Value& v) {
    if (v.number) {
      return v.v != 0;
    }
    std::string t;
    for (const char c : v.text) {
      t += lower(c);
    }
    if (t == "true" || t == "yes" || t == "on") {
      return true;
    }
    if (t == "false" || t == "no" || t == "off") {
      return false;
    }
    throw Error("expr: expected a boolean, got \"" + v.text + "\"");
  }
  static double number(const Value& v, const char* op) {
    if (!v.number) {
      throw Error(std::string("expr: \"") + v.text + "\" is not a number (operand of " + op + ")");
    }
    return v.v;
  }
  void blanks() {
    while (i_ < s_.size() && white(s_[i_])) {
      ++i_;
    }
  }
  bool take(std::string_view op) {
    blanks();
    if (s_.substr(i_).starts_with(op)) {
      i_ += op.size();
      return true;
    }
    return false;
  }
  bool take_word(std::string_view op) {
    blanks();
    if (s_.substr(i_).starts_with(op) && (i_ + op.size() == s_.size() || !(std::isalnum(static_cast<unsigned char>(s_[i_ + op.size()])) || s_[i_ + op.size()] == '_'))) {
      i_ += op.size();
      return true;
    }
    return false;
  }
  Value ternary(bool skip) {
    auto condition = logical_or(skip);
    if (!take("?")) {
      return condition;
    }
    const bool chosen = skip || truth(condition);
    auto       a      = ternary(skip || !chosen);
    if (!take(":")) {
      throw Error("expr: missing ':'");
    }
    auto b = ternary(skip || chosen);
    return chosen ? a : b;
  }
  Value logical_or(bool skip) {
    auto a = logical_and(skip);
    while (take("||")) {
      const bool known = !skip && truth(a);
      auto       b     = logical_and(skip || known);
      if (!skip) {
        a = num(known || truth(b), true);
      }
    }
    return a;
  }
  Value logical_and(bool skip) {
    auto a = equality(skip);
    while (take("&&")) {
      const bool known = !skip && !truth(a);
      auto       b     = equality(skip || known);
      if (!skip) {
        a = num(!known && truth(b), true);
      }
    }
    return a;
  }
  Value equality(bool skip) {
    auto a = relational(skip);
    while (true) {
      int op = 0;
      if (take("==")) {
        op = 1;
      } else if (take("!=")) {
        op = 2;
      } else if (take_word("eq")) {
        op = 3;
      } else if (take_word("ne")) {
        op = 4;
      } else {
        return a;
      }
      auto b = relational(skip);
      if (skip) {
        continue;
      }
      bool same;
      if (op <= 2 && a.number && b.number) {
        same = a.v == b.v;
      } else {
        same = text_of(a) == text_of(b);
      }
      a = num(op == 1 || op == 3 ? same : !same, true);
    }
  }
  Value relational(bool skip) {
    auto a = additive(skip);
    while (true) {
      int op = 0;
      blanks();
      if (take("<=")) {
        op = 1;
      } else if (take(">=")) {
        op = 2;
      } else if (i_ < s_.size() && s_[i_] == '<' && s_.substr(i_, 2) != "<<") {
        ++i_;
        op = 3;
      } else if (i_ < s_.size() && s_[i_] == '>' && s_.substr(i_, 2) != ">>") {
        ++i_;
        op = 4;
      } else {
        return a;
      }
      auto b = additive(skip);
      if (skip) {
        continue;
      }
      int order;
      if (a.number && b.number) {
        order = a.v < b.v ? -1 : a.v > b.v ? 1 : 0;
      } else {
        order = text_of(a).compare(text_of(b));
      }
      a = num(op == 1 ? order <= 0 : op == 2 ? order >= 0 : op == 3 ? order < 0 : order > 0, true);
    }
  }
  Value additive(bool skip) {
    auto a = multiplicative(skip);
    while (true) {
      blanks();
      if (i_ >= s_.size() || (s_[i_] != '+' && s_[i_] != '-')) {
        return a;
      }
      const char op = s_[i_++];
      auto       b  = multiplicative(skip);
      if (skip) {
        continue;
      }
      const double x = number(a, "+ or -"), y = number(b, "+ or -");
      a              = num(op == '+' ? x + y : x - y, a.integer && b.integer);
    }
  }
  Value multiplicative(bool skip) {
    auto a = unary(skip);
    while (true) {
      blanks();
      if (i_ >= s_.size() || (s_[i_] != '*' && s_[i_] != '/' && s_[i_] != '%') || s_.substr(i_, 2) == "**") {
        return a;
      }
      const char op = s_[i_++];
      auto       b  = unary(skip);
      if (skip) {
        continue;
      }
      const double x = number(a, "* / %"), y = number(b, "* / %");
      const bool   integer = a.integer && b.integer;
      if (op == '*') {
        a = num(x * y, integer);
      } else if (y == 0 && (integer || op == '%')) {
        throw Error("expr: divide by zero");
      } else if (op == '/') {
        a = num(integer ? std::floor(x / y) : x / y, integer);
      } else {
        a = num(x - y * std::floor(x / y), integer);
      }
    }
  }
  Value unary(bool skip) {
    blanks();
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '!')) {
      const char op = s_[i_++];
      auto       a  = unary(skip);
      if (skip) {
        return a;
      }
      if (op == '!') {
        return num(!truth(a), true);
      }
      return op == '-' ? num(-number(a, "unary -"), a.integer) : a;
    }
    return power(skip);
  }
  Value power(bool skip) {
    auto a = primary(skip);
    if (take("**")) {
      auto b = unary(skip);
      if (!skip) {
        const double x = number(a, "**"), y = number(b, "**");
        a              = num(std::pow(x, y), a.integer && b.integer && y >= 0);
      }
    }
    return a;
  }
  Value primary(bool skip) {
    blanks();
    if (i_ >= s_.size()) {
      throw Error("expr: missing operand");
    }
    const char c = s_[i_];
    if (c == '(') {
      ++i_;
      auto v = ternary(skip);
      if (!take(")")) {
        throw Error("expr: missing ')'");
      }
      return v;
    }
    if (c == '$') {
      const size_t begin = i_++;
      if (i_ < s_.size() && s_[i_] == '{') {
        const auto close = s_.find('}', i_);
        if (close == std::string_view::npos) {
          throw Error("expr: unterminated ${");
        }
        i_ = close + 1;
      } else {
        while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_' || s_[i_] == ':')) {
          ++i_;
        }
      }
      return skip ? Value{} : from(interp_.substitute(s_.substr(begin, i_ - begin)));
    }
    if (c == '[') {
      const size_t end = Interp::skip_bracket(s_, i_);
      const auto   inner = s_.substr(i_ + 1, end - i_ - 2);
      i_                 = end;
      return skip ? Value{} : from(interp_.eval(inner));
    }
    if (c == '"') {
      const size_t begin = ++i_;
      while (i_ < s_.size() && s_[i_] != '"') {
        i_ += s_[i_] == '\\' ? 2 : 1;
      }
      if (i_ >= s_.size()) {
        throw Error("expr: unterminated string");
      }
      const auto inner = s_.substr(begin, i_ - begin);
      ++i_;
      Value v;
      if (!skip) {
        v.text = interp_.substitute(inner);
      }
      return v;
    }
    if (c == '{') {
      const size_t end = Interp::skip_brace(s_, i_);
      Value        v;
      v.text = std::string(s_.substr(i_ + 1, end - i_ - 2));
      i_     = end;
      return v;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      const size_t begin = i_;
      while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.')) {
        if ((s_[i_] == 'e' || s_[i_] == 'E') && i_ + 1 < s_.size() && (s_[i_ + 1] == '-' || s_[i_ + 1] == '+')
            && !(s_.substr(begin, 2) == "0x" || s_.substr(begin, 2) == "0X")) {
          ++i_;
        }
        ++i_;
      }
      Value v;
      if (!to_number(s_.substr(begin, i_ - begin), v.v, &v.integer)) {
        throw Error("expr: \"" + std::string(s_.substr(begin, i_ - begin)) + "\" is not a number");
      }
      v.number = true;
      return v;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const size_t begin = i_;
      while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) {
        ++i_;
      }
      const std::string name(s_.substr(begin, i_ - begin));
      blanks();
      if (i_ < s_.size() && s_[i_] == '(') {
        ++i_;
        std::vector<Value> args;
        if (!take(")")) {
          do {
            args.push_back(ternary(skip));
          } while (take(","));
          if (!take(")")) {
            throw Error("expr: missing ')' after the arguments of " + name);
          }
        }
        return skip ? Value{} : function(name, args);
      }
      // A bare word: a boolean, or a string for eq / ne.
      Value v;
      v.text = name;
      return v;
    }
    throw Error("expr: unexpected \"" + std::string(s_.substr(i_)) + "\"");
  }
  static Value function(const std::string& name, const std::vector<Value>& args) {
    const auto need = [&](size_t n) {
      if (args.size() != n) {
        throw Error("expr: " + name + " takes " + std::to_string(n) + " argument" + (n == 1 ? "" : "s"));
      }
    };
    const auto x = [&](size_t i) { return number(args[i], name.c_str()); };
    if (name == "min" || name == "max") {
      if (args.empty()) {
        throw Error("expr: " + name + " needs an argument");
      }
      double best    = x(0);
      bool   integer = args[0].integer;
      for (size_t i = 1; i < args.size(); ++i) {
        best    = name == "min" ? std::min(best, x(i)) : std::max(best, x(i));
        integer = integer && args[i].integer;
      }
      return num(best, integer);
    }
    if (name == "pow" || name == "fmod" || name == "hypot" || name == "atan2") {
      need(2);
      return num(name == "pow" ? std::pow(x(0), x(1)) : name == "fmod" ? std::fmod(x(0), x(1)) : name == "hypot" ? std::hypot(x(0), x(1)) : std::atan2(x(0), x(1)),
                 false);
    }
    need(1);
    const double v = x(0);
    if (name == "abs") {
      return num(std::fabs(v), args[0].integer);
    }
    if (name == "int" || name == "wide" || name == "entier") {
      return num(std::trunc(v), true);
    }
    if (name == "round") {
      return num(std::round(v), true);
    }
    if (name == "double") {
      return num(v, false);
    }
    if (name == "bool") {
      return num(truth(args[0]), true);
    }
    static const std::unordered_map<std::string, double (*)(double)> table{
        {"floor", std::floor}, {"ceil", std::ceil}, {"sqrt", std::sqrt}, {"exp", std::exp}, {"log", std::log}, {"log10", std::log10},
        {"sin", std::sin},     {"cos", std::cos},   {"tan", std::tan}};
    if (const auto it = table.find(name); it != table.end()) {
      return num(it->second(v), false);
    }
    throw Error("expr: unknown function " + name);
  }

  Interp&          interp_;
  std::string_view s_;
  size_t           i_ = 0;
};

// ---- the interpreter ------------------------------------------------------------------------------------

Interp::Interp(Host host, Plain plain) : host_(std::move(host)), plain_(std::move(plain)) { frames_.emplace_back(); }

size_t Interp::skip_brace(std::string_view text, size_t open) {
  int    depth = 0;
  size_t i     = open;
  for (; i < text.size(); ++i) {
    if (text[i] == '\\') {
      ++i;
    } else if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}' && --depth == 0) {
      return i + 1;
    }
  }
  throw Error("missing close-brace");
}

size_t Interp::skip_bracket(std::string_view text, size_t open) {
  size_t i = open + 1;
  while (i < text.size()) {
    const char c = text[i];
    if (c == '\\') {
      i += 2;
    } else if (c == '[') {
      i = skip_bracket(text, i);
    } else if (c == ']') {
      return i + 1;
    } else if (c == '{') {
      i = skip_brace(text, i);
    } else if (c == '"') {
      for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\') {
          ++i;
        } else if (text[i] == '[') {
          i = skip_bracket(text, i) - 1;
        }
      }
      ++i;
    } else {
      ++i;
    }
  }
  throw Error("missing close-bracket");
}

bool Interp::scan(std::string_view s, size_t& i, std::vector<Raw>& words) {
  words.clear();
  // Between commands: blank space, separators and comments.
  while (i < s.size()) {
    if (white(s[i]) || s[i] == ';') {
      ++i;
    } else if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\n') {
      i += 2;
    } else if (s[i] == '#') {
      while (i < s.size() && s[i] != '\n') {
        i += s[i] == '\\' && i + 1 < s.size() ? 2 : 1;
      }
    } else {
      break;
    }
  }
  if (i >= s.size()) {
    return false;
  }
  while (i < s.size()) {
    while (i < s.size() && (blank(s[i]) || (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\n'))) {
      i += s[i] == '\\' ? 2 : 1;
    }
    if (i >= s.size() || s[i] == '\n' || s[i] == ';') {
      if (i < s.size()) {
        ++i;
      }
      break;
    }
    if (s[i] == '{') {
      const size_t end = skip_brace(s, i);
      words.push_back({Raw::Kind::braced, s.substr(i + 1, end - i - 2)});
      i = end;
      if (i < s.size() && !white(s[i]) && s[i] != ';') {
        throw Error("extra characters after close-brace");
      }
    } else if (s[i] == '"') {
      const size_t begin = ++i;
      while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
          i += 2;
        } else if (s[i] == '[') {
          i = skip_bracket(s, i);
        } else {
          ++i;
        }
      }
      if (i >= s.size()) {
        throw Error("missing close-quote");
      }
      words.push_back({Raw::Kind::quoted, s.substr(begin, i - begin)});
      ++i;
      if (i < s.size() && !white(s[i]) && s[i] != ';') {
        throw Error("extra characters after close-quote");
      }
    } else {
      const size_t begin = i;
      while (i < s.size() && !white(s[i]) && s[i] != ';') {
        if (s[i] == '\\') {
          if (i + 1 < s.size() && s[i + 1] == '\n') {
            break;
          }
          i += 2;
        } else if (s[i] == '[') {
          i = skip_bracket(s, i);
        } else if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
          const auto close = s.find('}', i);
          i                = close == std::string_view::npos ? s.size() : close + 1;
        } else {
          ++i;
        }
      }
      i = std::min(i, s.size());
      words.push_back({Raw::Kind::bare, s.substr(begin, i - begin)});
    }
  }
  return true;
}

const Interp::Frame& Interp::frame_of(std::string_view& name) const {
  if (name.starts_with("::")) {
    name.remove_prefix(2);
    return frames_.front();
  }
  return frames_.back().globals.contains(std::string(name)) ? frames_.front() : frames_.back();
}
Interp::Frame& Interp::frame_of(std::string_view& name) { return const_cast<Frame&>(std::as_const(*this).frame_of(name)); }

bool Interp::has_variable(std::string_view name) const {
  const auto& frame = frame_of(name);
  return frame.variables.contains(std::string(name));
}

const std::string& Interp::variable(std::string_view name) const {
  const auto  given = name;
  const auto& frame = frame_of(name);
  const auto  it    = frame.variables.find(std::string(name));
  if (it == frame.variables.end()) {
    throw Error("can't read \"" + std::string(given) + "\": no such variable");
  }
  return it->second;
}

void Interp::set_variable(std::string_view name, std::string value) {
  auto& frame                         = frame_of(name);
  frame.variables[std::string(name)] = std::move(value);
}

std::string Interp::substitute(std::string_view s) {
  std::string out;
  size_t      i = 0;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\\') {
      out += unescape(s, i);
    } else if (c == '[') {
      const size_t end  = skip_bracket(s, i);
      out              += eval(s.substr(i + 1, end - i - 2));
      i                 = end;
    } else if (c == '$') {
      size_t           j = i + 1;
      std::string_view name;
      if (j < s.size() && s[j] == '{') {
        const auto close = s.find('}', j);
        if (close == std::string_view::npos) {
          throw Error("missing close-brace for variable name");
        }
        name = s.substr(j + 1, close - j - 1);
        j    = close + 1;
      } else {
        while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || (s[j] == ':' && j + 1 < s.size() && s[j + 1] == ':'))) {
          j += s[j] == ':' ? 2 : 1;
        }
        name = s.substr(i + 1, j - i - 1);
        if (!name.empty() && j < s.size() && s[j] == '(') {
          throw Error("array variables are not supported ($" + std::string(name) + "(..))");
        }
      }
      if (name.empty()) {
        out += '$';
        ++i;
      } else {
        out += variable(name);
        i    = j;
      }
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

std::string Interp::word(const Raw& raw) {
  if (raw.kind != Raw::Kind::braced) {
    return substitute(raw.text);
  }
  // Inside braces only a backslash-newline is replaced.
  if (raw.text.find("\\\n") == std::string_view::npos) {
    return std::string(raw.text);
  }
  std::string out;
  for (size_t i = 0; i < raw.text.size();) {
    if (raw.text[i] == '\\' && i + 1 < raw.text.size() && raw.text[i + 1] == '\n') {
      out += unescape(raw.text, i);
    } else {
      out += raw.text[i++];
    }
  }
  return out;
}

std::string Interp::eval(std::string_view script) {
  if (++depth_ > 200) {
    --depth_;
    throw Error("too many nested evaluations");
  }
  struct Leave {
    unsigned& depth;
    ~Leave() { --depth; }
  } leave{depth_};
  std::string      result;
  std::vector<Raw> raw;
  size_t           i = 0;
  while (scan(script, i, raw)) {
    if (raw.empty()) {
      continue;
    }
    Words words;
    words.reserve(raw.size());
    for (const auto& r : raw) {
      words.push_back(word(r));
    }
    result = invoke(words);
  }
  return result;
}

void Interp::run(std::string_view script, const std::function<void(const std::string&, const std::string&)>& on_error) {
  std::vector<Raw> raw;
  size_t           i = 0, counted = 0;
  line_              = 1;
  while (true) {
    std::string name;
    try {
      const size_t before = i;
      bool         more   = false;
      try {
        more = scan(script, i, raw);
      } catch (const Error&) {
        // The command that cannot be split starts after the blank space and comments in front of it.
        size_t at = before;
        while (at < script.size() && (white(script[at]) || script[at] == ';')) {
          ++at;
        }
        line_   += static_cast<size_t>(std::count(script.begin() + static_cast<long>(counted), script.begin() + static_cast<long>(at), '\n'));
        counted  = at;
        throw;
      }
      if (!more) {
        break;
      }
      if (raw.empty()) {
        continue;
      }
      const auto start = static_cast<size_t>(raw.front().text.data() - script.data());
      line_   += static_cast<size_t>(std::count(script.begin() + static_cast<long>(counted), script.begin() + static_cast<long>(start), '\n'));
      counted  = start;
      name     = std::string(raw.front().text);
      Words words;
      for (const auto& r : raw) {
        words.push_back(word(r));
      }
      invoke(words);
    } catch (const Error& e) {
      on_error(name, e.what());
      if (name.empty()) {
        break;  // the text itself cannot be split into commands any further
      }
    } catch (const Return&) {
      break;
    } catch (const Break&) {
      on_error(name, "break outside of a loop");
    } catch (const Continue&) {
      on_error(name, "continue outside of a loop");
    }
  }
}

std::string Interp::expression(std::string_view text) { return Expression(*this, text).run(); }

bool Interp::truth(std::string_view text) {
  const auto  value = expression(text);
  double      v     = 0;
  if (to_number(value, v)) {
    return v != 0;
  }
  std::string t;
  for (const char c : value) {
    t += lower(c);
  }
  if (t == "true" || t == "yes" || t == "on") {
    return true;
  }
  if (t == "false" || t == "no" || t == "off") {
    return false;
  }
  throw Error("expected a boolean, got \"" + value + "\"");
}

std::string Interp::call(const Proc& proc, Words& w) {
  Frame frame;
  size_t at = 1;
  for (size_t p = 0; p < proc.parameters.size(); ++p) {
    const auto& [name, fallback] = proc.parameters[p];
    if (name == "args" && p + 1 == proc.parameters.size()) {
      frame.variables["args"] = list_join({w.begin() + static_cast<long>(std::min(at, w.size())), w.end()});
      at                      = w.size();
    } else if (at < w.size()) {
      frame.variables[name] = w[at++];
    } else if (fallback != no_default) {
      frame.variables[name] = fallback;
    } else {
      throw Error("wrong # args: " + w[0] + " needs \"" + name + "\"");
    }
  }
  if (at != w.size()) {
    throw Error("wrong # args: too many for " + w[0]);
  }
  frames_.push_back(std::move(frame));
  struct Pop {
    std::vector<Frame>& frames;
    ~Pop() { frames.pop_back(); }
  } pop{frames_};
  try {
    return eval(proc.body);
  } catch (Return& r) {
    return std::move(r.value);
  }
}

std::string Interp::invoke(Words& w) {
  bool known  = true;
  auto result = builtin(w, known);
  if (known) {
    return result;
  }
  if (const auto it = procs_.find(w[0]); it != procs_.end()) {
    return call(it->second, w);
  }
  if (host_ && host_(w, result)) {
    return result;
  }
  throw Error("invalid command name \"" + w[0] + "\"");
}

std::string Interp::do_if(Words& w) {
  size_t i = 1;
  while (true) {
    if (i >= w.size()) {
      throw Error("wrong # args: no expression after \"" + w[i - 1] + "\"");
    }
    const bool taken = truth(w[i++]);
    if (i < w.size() && w[i] == "then") {
      ++i;
    }
    if (i >= w.size()) {
      throw Error("wrong # args: no script following the condition of if");
    }
    if (taken) {
      return eval(w[i]);
    }
    ++i;
    if (i >= w.size()) {
      return {};
    }
    if (w[i] == "elseif") {
      ++i;
      continue;
    }
    if (w[i] == "else") {
      ++i;
    }
    if (i + 1 != w.size()) {
      throw Error("wrong # args: extra words after the else clause of if");
    }
    return eval(w[i]);
  }
}

std::string Interp::do_foreach(Words& w) {
  if (w.size() < 4 || w.size() % 2 != 0) {
    throw Error("wrong # args: foreach varList list ?varList list ...? body");
  }
  std::vector<std::vector<std::string>> names, values;
  size_t                                rounds = 0;
  for (size_t i = 1; i + 1 < w.size(); i += 2) {
    names.push_back(list_split(w[i]));
    values.push_back(list_split(w[i + 1]));
    if (names.back().empty()) {
      throw Error("foreach varlist is empty");
    }
    rounds = std::max(rounds, (values.back().size() + names.back().size() - 1) / names.back().size());
  }
  for (size_t r = 0; r < rounds; ++r) {
    for (size_t k = 0; k < names.size(); ++k) {
      for (size_t j = 0; j < names[k].size(); ++j) {
        const size_t at = r * names[k].size() + j;
        set_variable(names[k][j], at < values[k].size() ? values[k][at] : std::string());
      }
    }
    try {
      eval(w.back());
    } catch (const Break&) {
      break;
    } catch (const Continue&) {
    }
  }
  return {};
}

std::string Interp::do_lsearch(Words& w) {
  bool   exact = false, regexp = false, inline_ = false, all = false, not_ = false, nocase = false;
  size_t i = 1;
  for (; i + 2 < w.size(); ++i) {
    const auto& o = w[i];
    if (o == "-exact") {
      exact = true;
    } else if (o == "-glob") {
      exact = regexp = false;
    } else if (o == "-regexp") {
      regexp = true;
    } else if (o == "-inline") {
      inline_ = true;
    } else if (o == "-all") {
      all = true;
    } else if (o == "-not") {
      not_ = true;
    } else if (o == "-nocase") {
      nocase = true;
    } else {
      throw Error("lsearch " + o + " is not supported");
    }
  }
  if (i + 2 != w.size()) {
    throw Error("wrong # args: lsearch ?options? list pattern");
  }
  const auto  list    = list_split(w[i]);
  // An object handle is compared as an object, by its name; any other pattern as Tcl does.
  const bool  object  = plain_ && plain(w[i + 1]) != w[i + 1];
  const auto  pattern = plain(w[i + 1]);
  std::regex  re;
  if (regexp && !object) {
    re = make_regex(pattern, nocase);
  }
  std::vector<std::string> found;
  for (size_t k = 0; k < list.size(); ++k) {
    const auto text = plain(list[k]);
    bool       hit;
    if (exact || object) {
      hit = text == pattern;
    } else if (regexp) {
      hit = std::regex_search(text, re);
    } else {
      hit = match(pattern, text, nocase);
    }
    if (hit != not_) {
      found.push_back(inline_ ? list[k] : std::to_string(k));
      if (!all) {
        break;
      }
    }
  }
  if (all) {
    return list_join(found);
  }
  return found.empty() ? (inline_ ? std::string() : "-1") : found.front();
}

std::string Interp::do_lsort(Words& w) {
  bool unique = false, decreasing = false, numeric = false;
  for (size_t i = 1; i + 1 < w.size(); ++i) {
    if (w[i] == "-unique") {
      unique = true;
    } else if (w[i] == "-decreasing") {
      decreasing = true;
    } else if (w[i] == "-increasing" || w[i] == "-ascii" || w[i] == "-dictionary") {
    } else if (w[i] == "-integer" || w[i] == "-real") {
      numeric = true;
    } else {
      throw Error("lsort " + w[i] + " is not supported");
    }
  }
  if (w.size() < 2) {
    throw Error("wrong # args: lsort ?options? list");
  }
  auto list = list_split(w.back());
  if (numeric) {
    std::stable_sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) {
      double x = 0, y = 0;
      if (!to_number(a, x) || !to_number(b, y)) {
        throw Error("lsort: expected a number");
      }
      return x < y;
    });
  } else {
    std::stable_sort(list.begin(), list.end());
  }
  if (unique) {
    list.erase(std::unique(list.begin(), list.end()), list.end());
  }
  if (decreasing) {
    std::reverse(list.begin(), list.end());
  }
  return list_join(list);
}

std::string Interp::do_string(Words& w) {
  if (w.size() < 3) {
    throw Error("wrong # args: string subcommand arg ?arg ...?");
  }
  const auto& sub = w[1];
  const auto  fold = [](std::string s) {
    for (auto& c : s) {
      c = lower(c);
    }
    return s;
  };
  if (sub == "length") {
    return std::to_string(w[2].size());
  }
  if (sub == "tolower") {
    return fold(w[2]);
  }
  if (sub == "toupper") {
    auto s = w[2];
    for (auto& c : s) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
  }
  if (sub == "trim" || sub == "trimleft" || sub == "trimright") {
    const std::string chars = w.size() > 3 ? w[3] : " \t\n\r";
    std::string_view  s     = w[2];
    while (sub != "trimright" && !s.empty() && chars.find(s.front()) != std::string::npos) {
      s.remove_prefix(1);
    }
    while (sub != "trimleft" && !s.empty() && chars.find(s.back()) != std::string::npos) {
      s.remove_suffix(1);
    }
    return std::string(s);
  }
  if (sub == "equal" || sub == "compare" || sub == "match") {
    bool   nocase = false;
    size_t i      = 2;
    while (i + 2 < w.size()) {
      if (w[i] == "-nocase") {
        nocase = true;
      } else {
        throw Error("string " + sub + " " + w[i] + " is not supported");
      }
      ++i;
    }
    if (i + 2 != w.size()) {
      throw Error("wrong # args: string " + sub + " ?-nocase? a b");
    }
    if (sub == "match") {
      return match(w[i], w[i + 1], nocase) ? "1" : "0";
    }
    const auto a = nocase ? fold(w[i]) : w[i], b = nocase ? fold(w[i + 1]) : w[i + 1];
    if (sub == "equal") {
      return a == b ? "1" : "0";
    }
    return a < b ? "-1" : a > b ? "1" : "0";
  }
  if (sub == "range" && w.size() == 5) {
    const auto size = static_cast<long long>(w[2].size());
    const auto a = std::max<long long>(0, index_of(w[3], size)), b = std::min(size - 1, index_of(w[4], size));
    return a > b ? std::string() : w[2].substr(static_cast<size_t>(a), static_cast<size_t>(b - a + 1));
  }
  if (sub == "index" && w.size() == 4) {
    const auto size = static_cast<long long>(w[2].size());
    const auto a    = index_of(w[3], size);
    return a < 0 || a >= size ? std::string() : std::string(1, w[2][static_cast<size_t>(a)]);
  }
  if ((sub == "first" || sub == "last") && w.size() == 4) {
    const auto at = sub == "first" ? w[3].find(w[2]) : w[3].rfind(w[2]);
    return at == std::string::npos ? "-1" : std::to_string(at);
  }
  if (sub == "repeat" && w.size() == 4) {
    std::string out;
    for (long long n = index_of(w[3], 0); n > 0; --n) {
      out += w[2];
    }
    return out;
  }
  if (sub == "map" && w.size() == 4) {
    const auto  pairs = list_split(w[2]);
    std::string out;
    for (size_t i = 0; i < w[3].size();) {
      bool hit = false;
      for (size_t k = 0; k + 1 < pairs.size(); k += 2) {
        if (!pairs[k].empty() && std::string_view(w[3]).substr(i).starts_with(pairs[k])) {
          out += pairs[k + 1];
          i   += pairs[k].size();
          hit  = true;
          break;
        }
      }
      if (!hit) {
        out += w[3][i++];
      }
    }
    return out;
  }
  throw Error("string " + sub + " is not supported");
}

std::string Interp::do_format(Words& w) {
  if (w.size() < 2) {
    throw Error("wrong # args: format formatString ?arg ...?");
  }
  const auto& f = w[1];
  std::string out;
  size_t      arg = 2;
  for (size_t i = 0; i < f.size();) {
    if (f[i] != '%') {
      out += f[i++];
      continue;
    }
    if (i + 1 < f.size() && f[i + 1] == '%') {
      out += '%';
      i   += 2;
      continue;
    }
    size_t j = i + 1;
    while (j < f.size() && std::string_view("-+ #0123456789.").find(f[j]) != std::string_view::npos) {
      ++j;
    }
    while (j < f.size() && (f[j] == 'l' || f[j] == 'h')) {
      ++j;
    }
    if (j >= f.size()) {
      throw Error("format string ended in the middle of a field");
    }
    const char conversion = f[j];
    if (arg >= w.size()) {
      throw Error("not enough arguments for all format specifiers");
    }
    std::string spec = f.substr(i, j - i);
    std::erase_if(spec, [](char c) { return c == 'l' || c == 'h' || c == '*'; });
    char   buffer[512];
    double v = 0;
    if (conversion == 's') {
      spec += 's';
      const int n = std::snprintf(buffer, sizeof buffer, spec.c_str(), w[arg].c_str());
      out += n >= 0 && static_cast<size_t>(n) < sizeof buffer ? std::string(buffer) : w[arg];
    } else if (conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'x' || conversion == 'X' || conversion == 'o' || conversion == 'c') {
      if (!to_number(w[arg], v)) {
        throw Error("format: expected an integer but got \"" + w[arg] + "\"");
      }
      if (conversion == 'c') {
        out += static_cast<char>(v);
      } else {
        spec += "ll";
        spec += conversion == 'i' || conversion == 'u' ? 'd' : conversion;
        std::snprintf(buffer, sizeof buffer, spec.c_str(), static_cast<long long>(v));
        out += buffer;
      }
    } else if (conversion == 'f' || conversion == 'e' || conversion == 'E' || conversion == 'g' || conversion == 'G') {
      if (!to_number(w[arg], v)) {
        throw Error("format: expected a number but got \"" + w[arg] + "\"");
      }
      spec += conversion;
      std::snprintf(buffer, sizeof buffer, spec.c_str(), v);
      out += buffer;
    } else {
      throw Error(std::string("format: bad field specifier \"") + conversion + "\"");
    }
    ++arg;
    i = j + 1;
  }
  return out;
}

std::string Interp::do_regexp(Words& w) {
  bool   nocase = false;
  size_t i      = 1;
  for (; i < w.size() && w[i].starts_with("-"); ++i) {
    if (w[i] == "--") {
      ++i;
      break;
    }
    if (w[i] == "-nocase") {
      nocase = true;
    } else {
      throw Error("regexp " + w[i] + " is not supported");
    }
  }
  if (i + 2 > w.size()) {
    throw Error("wrong # args: regexp ?options? exp string ?matchVar? ?subMatchVar ...?");
  }
  const auto  re = make_regex(w[i], nocase);
  std::smatch m;
  const bool  hit = std::regex_search(w[i + 1], m, re);
  if (hit) {
    for (size_t k = i + 2; k < w.size(); ++k) {
      const size_t group = k - (i + 2);
      set_variable(w[k], group < m.size() ? m[group].str() : std::string());
    }
  }
  return hit ? "1" : "0";
}

std::string Interp::do_regsub(Words& w) {
  bool   nocase = false, all = false;
  size_t i = 1;
  for (; i < w.size() && w[i].starts_with("-"); ++i) {
    if (w[i] == "--") {
      ++i;
      break;
    }
    if (w[i] == "-nocase") {
      nocase = true;
    } else if (w[i] == "-all") {
      all = true;
    } else {
      throw Error("regsub " + w[i] + " is not supported");
    }
  }
  if (i + 3 > w.size() || i + 4 < w.size()) {
    throw Error("wrong # args: regsub ?options? exp string subSpec ?varName?");
  }
  const auto  re = make_regex(w[i], nocase);
  std::string replacement;
  for (size_t k = 0; k < w[i + 2].size(); ++k) {
    const char c = w[i + 2][k];
    if (c == '&') {
      replacement += "$&";
    } else if (c == '\\' && k + 1 < w[i + 2].size()) {
      const char n = w[i + 2][++k];
      replacement += std::isdigit(static_cast<unsigned char>(n)) ? std::string("$") + n : std::string(1, n);
    } else if (c == '$') {
      replacement += "$$";
    } else {
      replacement += c;
    }
  }
  const auto result = std::regex_replace(w[i + 1], re, replacement, all ? std::regex_constants::format_default : std::regex_constants::format_first_only);
  if (i + 4 == w.size()) {
    set_variable(w[i + 3], result);
    return result == w[i + 1] ? "0" : "1";
  }
  return result;
}

std::string Interp::builtin(Words& w, bool& known) {
  const auto& name = w[0];
  const auto  args = [&](size_t low, size_t high, const char* usage) {
    if (w.size() < low + 1 || w.size() > high + 1) {
      throw Error(std::string("wrong # args: ") + usage);
    }
  };
  if (name == "set") {
    args(1, 2, "set varName ?newValue?");
    if (w.size() == 3) {
      set_variable(w[1], w[2]);
    }
    return variable(w[1]);
  }
  if (name == "expr") {
    args(1, 1000, "expr arg ?arg ...?");
    std::string text = w[1];
    for (size_t i = 2; i < w.size(); ++i) {
      text += " " + w[i];
    }
    return expression(text);
  }
  if (name == "if") {
    return do_if(w);
  }
  if (name == "foreach") {
    return do_foreach(w);
  }
  if (name == "list") {
    return list_join({w.begin() + 1, w.end()});
  }
  if (name == "llength") {
    args(1, 1, "llength list");
    return std::to_string(list_split(w[1]).size());
  }
  if (name == "lindex") {
    args(1, 2, "lindex list ?index?");
    if (w.size() == 2) {
      return w[1];
    }
    const auto list = list_split(w[1]);
    const auto at   = index_of(w[2], static_cast<long long>(list.size()));
    return at < 0 || at >= static_cast<long long>(list.size()) ? std::string() : list[static_cast<size_t>(at)];
  }
  if (name == "lrange") {
    args(3, 3, "lrange list first last");
    const auto list = list_split(w[1]);
    const auto size = static_cast<long long>(list.size());
    const auto a = std::max<long long>(0, index_of(w[2], size)), b = std::min(size - 1, index_of(w[3], size));
    return a > b ? std::string() : list_join({list.begin() + a, list.begin() + b + 1});
  }
  if (name == "lappend") {
    args(1, 100000, "lappend varName ?value ...?");
    std::string value = has_variable(w[1]) ? variable(w[1]) : std::string();
    for (size_t i = 2; i < w.size(); ++i) {
      if (!value.empty()) {
        value += ' ';
      }
      value += list_quote(w[i]);
    }
    set_variable(w[1], value);
    return value;
  }
  if (name == "append") {
    args(1, 100000, "append varName ?value ...?");
    std::string value = has_variable(w[1]) ? variable(w[1]) : std::string();
    for (size_t i = 2; i < w.size(); ++i) {
      value += w[i];
    }
    set_variable(w[1], value);
    return value;
  }
  if (name == "concat") {
    std::string out;
    for (size_t i = 1; i < w.size(); ++i) {
      const auto piece = trim(w[i]);
      if (piece.empty()) {
        continue;
      }
      if (!out.empty()) {
        out += ' ';
      }
      out += piece;
    }
    return out;
  }
  if (name == "lsearch") {
    return do_lsearch(w);
  }
  if (name == "lsort") {
    return do_lsort(w);
  }
  if (name == "lreverse") {
    args(1, 1, "lreverse list");
    auto list = list_split(w[1]);
    std::reverse(list.begin(), list.end());
    return list_join(list);
  }
  if (name == "join") {
    args(1, 2, "join list ?joinString?");
    const auto  list = list_split(w[1]);
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
      out += (i != 0 ? (w.size() > 2 ? w[2] : std::string(" ")) : std::string()) + list[i];
    }
    return out;
  }
  if (name == "split") {
    args(1, 2, "split string ?splitChars?");
    const std::string        chars = w.size() > 2 ? w[2] : " \t\n\r";
    std::vector<std::string> out(1);
    for (const char c : w[1]) {
      if (chars.empty()) {
        out.back() = std::string(1, c);
        out.emplace_back();
      } else if (chars.find(c) != std::string::npos) {
        out.emplace_back();
      } else {
        out.back() += c;
      }
    }
    if (chars.empty()) {
      out.pop_back();
    }
    return list_join(out);
  }
  if (name == "incr") {
    args(1, 2, "incr varName ?increment?");
    double value = 0, by = 1;
    if (has_variable(w[1]) && !to_number(variable(w[1]), value)) {
      throw Error("expected an integer but got \"" + variable(w[1]) + "\"");
    }
    if (w.size() == 3 && !to_number(w[2], by)) {
      throw Error("expected an integer but got \"" + w[2] + "\"");
    }
    set_variable(w[1], number_text(value + by, true));
    return variable(w[1]);
  }
  if (name == "unset") {
    size_t i       = 1;
    bool   lenient = false;
    if (i < w.size() && w[i] == "-nocomplain") {
      lenient = true;
      ++i;
    }
    if (i < w.size() && w[i] == "--") {
      ++i;
    }
    for (; i < w.size(); ++i) {
      std::string_view variable_name = w[i];
      auto&            frame         = frame_of(variable_name);
      if (frame.variables.erase(std::string(variable_name)) == 0 && !lenient) {
        throw Error("can't unset \"" + w[i] + "\": no such variable");
      }
    }
    return {};
  }
  if (name == "string") {
    return do_string(w);
  }
  if (name == "format") {
    return do_format(w);
  }
  if (name == "regexp") {
    return do_regexp(w);
  }
  if (name == "regsub") {
    return do_regsub(w);
  }
  if (name == "while") {
    args(2, 2, "while test command");
    for (size_t guard = 0; truth(w[1]); ++guard) {
      if (guard > 10'000'000) {
        throw Error("while: more than 10,000,000 iterations");
      }
      try {
        eval(w[2]);
      } catch (const Break&) {
        break;
      } catch (const Continue&) {
      }
    }
    return {};
  }
  if (name == "for") {
    args(4, 4, "for start test next command");
    eval(w[1]);
    for (size_t guard = 0; truth(w[2]); ++guard) {
      if (guard > 10'000'000) {
        throw Error("for: more than 10,000,000 iterations");
      }
      try {
        eval(w[4]);
      } catch (const Break&) {
        break;
      } catch (const Continue&) {
      }
      eval(w[3]);
    }
    return {};
  }
  if (name == "break") {
    throw Break{};
  }
  if (name == "continue") {
    throw Continue{};
  }
  if (name == "return") {
    throw Return{w.size() > 1 ? w.back() : std::string()};
  }
  if (name == "error") {
    args(1, 3, "error message ?info? ?code?");
    throw Error(w[1]);
  }
  if (name == "catch") {
    args(1, 2, "catch script ?resultVarName?");
    std::string result;
    int         code = 0;
    try {
      result = eval(w[1]);
    } catch (const Error& e) {
      result = e.what();
      code   = 1;
    } catch (Return& r) {
      result = std::move(r.value);
      code   = 2;
    } catch (const Break&) {
      code = 3;
    } catch (const Continue&) {
      code = 4;
    }
    if (w.size() == 3) {
      set_variable(w[2], result);
    }
    return std::to_string(code);
  }
  if (name == "eval") {
    std::string script;
    for (size_t i = 1; i < w.size(); ++i) {
      script += (i > 1 ? " " : "") + w[i];
    }
    return eval(script);
  }
  if (name == "info") {
    if (w.size() == 3 && w[1] == "exists") {
      return has_variable(w[2]) ? "1" : "0";
    }
    if (w.size() == 2 && w[1] == "script") {
      return {};
    }
    if (w.size() >= 2 && (w[1] == "commands" || w[1] == "procs")) {
      std::vector<std::string> names;
      for (const auto& [proc_name, proc] : procs_) {
        (void)proc;
        if (w.size() == 2 || match(w[2], proc_name)) {
          names.push_back(proc_name);
        }
      }
      std::sort(names.begin(), names.end());
      return list_join(names);
    }
    throw Error("info " + (w.size() > 1 ? w[1] : std::string()) + " is not supported");
  }
  if (name == "proc") {
    args(3, 3, "proc name args body");
    Proc proc;
    for (const auto& parameter : list_split(w[2])) {
      const auto parts = list_split(parameter);
      if (parts.empty() || parts.size() > 2) {
        throw Error("proc " + w[1] + ": bad parameter \"" + parameter + "\"");
      }
      proc.parameters.emplace_back(parts[0], parts.size() == 2 ? parts[1] : std::string(no_default));
    }
    proc.body    = w[3];
    procs_[w[1]] = std::move(proc);
    return {};
  }
  if (name == "global") {
    if (frames_.size() > 1) {
      for (size_t i = 1; i < w.size(); ++i) {
        frames_.back().globals.insert(w[i]);
      }
    }
    return {};
  }
  if (name == "puts") {
    return {};
  }
  known = false;
  return {};
}

}  // namespace livehd::timing::fast::tcl
