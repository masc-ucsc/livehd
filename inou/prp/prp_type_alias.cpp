//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_type_alias.hpp"

#include <cctype>
#include <format>
#include <string>

#include "prpparse/ast.hpp"
#include "prpparse/lexer.hpp"
#include "prpparse/node_kind.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/source_buffer.hpp"
#include "prpparse/token.hpp"

namespace prp_type_alias {

namespace {

using prpparse::Ast;
using prpparse::Field;
using prpparse::Kind;
using prpparse::Token;
using prpparse::Token_kind;

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

std::string_view text_of(std::string_view src, const Ast* n) { return src.substr(n->start_byte, n->end_byte - n->start_byte); }

const Ast* kid_with_field(const Ast* n, Field f) {
  for (const Ast* k : n->kids) {
    if (k->field == f) {
      return k;
    }
  }
  return nullptr;
}

// The first identifier at or under `n` (the declared name of an lvalue).
const Ast* first_identifier(const Ast* n) {
  if (n == nullptr) {
    return nullptr;
  }
  if (n->kind == Kind::identifier) {
    return n;
  }
  for (const Ast* k : n->kids) {
    if (const Ast* id = first_identifier(k)) {
      return id;
    }
  }
  return nullptr;
}

// One generic parameter (`W`, `W=32`, `W:U8=32`): the name is the first token, the
// default is the text after the first depth-0 `=`.
Param parse_param(std::string_view node_text) {
  Param p;
  try {
    prpparse::Source_buffer buf("", std::string(node_text));
    prpparse::Lexer         lex(buf);
    const auto              toks = lex.tokenize();
    if (toks.empty()) {
      return p;
    }
    p.name    = std::string(toks.front().text);
    int depth = 0;
    for (const Token& t : toks) {
      if (t.kind == Token_kind::lparen || t.kind == Token_kind::lbracket || t.kind == Token_kind::lbrace || t.kind == Token_kind::lt) {
        ++depth;
      } else if (t.kind == Token_kind::rparen || t.kind == Token_kind::rbracket || t.kind == Token_kind::rbrace
                 || t.kind == Token_kind::gt) {
        --depth;
      } else if (t.kind == Token_kind::assign && depth == 0) {
        p.default_text = std::string(trim(node_text.substr(t.end_byte)));
        p.has_default  = true;
        break;
      }
    }
  } catch (...) {
    // A parameter the lexer rejects never reached here (the file parsed).
  }
  return p;
}

bool is_word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The token-level rewrite shared by generic substitution and unit qualification.
// `map` sends a name to its replacement text. A name is left alone when it is
//  - a field access (`C.W`, after a `.`),
//  - a field label (`W:U8`, before a `:`),
//  - a named-argument / generic-bind label (`bits=W` keeps `bits`; `W=3` after `(`,
//    `<` or `,` is a label).
std::string rewrite_names(std::string_view text, const std::map<std::string, std::string>& map) {
  if (map.empty()) {
    return std::string(text);
  }
  prpparse::Source_buffer buf("", std::string(text));
  prpparse::Lexer         lex(buf);
  const auto              toks = lex.tokenize();
  std::string             out;
  size_t                  cursor = 0;
  for (size_t i = 0; i < toks.size(); ++i) {
    const Token& t = toks[i];
    if (t.kind != Token_kind::ident) {
      continue;
    }
    const auto hit = map.find(std::string(t.text));
    if (hit == map.end()) {
      continue;
    }
    const Token* prev = i > 0 ? &toks[i - 1] : nullptr;
    const Token* next = i + 1 < toks.size() ? &toks[i + 1] : nullptr;
    if (prev != nullptr && prev->kind == Token_kind::dot) {
      continue;
    }
    if (next != nullptr && next->kind == Token_kind::colon) {
      continue;
    }
    if (next != nullptr && next->kind == Token_kind::assign && prev != nullptr
        && (prev->kind == Token_kind::lparen || prev->kind == Token_kind::lt || prev->kind == Token_kind::comma)) {
      continue;
    }
    out.append(text.substr(cursor, t.start_byte - cursor));
    out.append(hit->second);
    cursor = t.end_byte;
  }
  out.append(text.substr(cursor));
  return out;
}

}  // namespace

bool might_have_templates(std::string_view s) {
  for (size_t i = 0; i + 4 < s.size(); ++i) {
    if (s.compare(i, 4, "type") != 0 || (i > 0 && is_word_char(s[i - 1])) || is_word_char(s[i + 4])) {
      continue;
    }
    size_t j = i + 4;
    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) {
      ++j;
    }
    const size_t name_start = j;
    while (j < s.size() && is_word_char(s[j])) {
      ++j;
    }
    if (j == name_start) {
      continue;
    }
    while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) {
      ++j;
    }
    if (j < s.size() && s[j] == '<') {
      return true;
    }
  }
  return false;
}

std::optional<Template> template_from_statement(std::string_view src, const Ast* st) {
  if (st == nullptr || st->kind != Kind::type_statement) {
    return std::nullopt;
  }
  const Ast* gen   = kid_with_field(st, Field::f_generic);
  const Ast* alias = kid_with_field(st, Field::f_alias);
  const Ast* name  = kid_with_field(st, Field::f_name);
  if (gen == nullptr || alias == nullptr || name == nullptr) {
    return std::nullopt;
  }
  Template t;
  t.name   = std::string(text_of(src, name));
  t.body   = std::string(trim(text_of(src, alias)));
  t.is_pub = kid_with_field(st, Field::f_pub) != nullptr;
  for (const Ast* item : gen->kids) {
    if (item->kind == Kind::comment) {
      continue;
    }
    Param p = parse_param(text_of(src, item));
    if (!p.name.empty()) {
      t.params.push_back(std::move(p));
    }
  }
  return t;
}

Unit_templates scan_unit(std::string_view path, std::string_view source) {
  Unit_templates out;
  try {
    prpparse::Source_buffer buf{std::string(path), std::string(source)};
    prpparse::Parser        parser(buf);
    Ast*                    root = parser.parse_ast();
    if (root == nullptr) {
      return out;
    }
    const std::string_view src = buf.view();
    for (const Ast* st : root->kids) {
      if (st->kind == Kind::type_statement || st->kind == Kind::enum_assignment || st->kind == Kind::lambda) {
        if (const Ast* nm = kid_with_field(st, Field::f_name)) {
          out.top_names.emplace(text_of(src, nm));
        }
      } else if (st->kind == Kind::assignment) {
        if (const Ast* lv = kid_with_field(st, Field::f_lvalue)) {
          if (const Ast* id = first_identifier(lv)) {
            out.top_names.emplace(text_of(src, id));
          }
        }
      }
      if (st->kind != Kind::type_statement) {
        continue;
      }
      if (auto t = template_from_statement(src, st)) {
        out.by_name.emplace(t->name, std::move(*t));
      }
    }
  } catch (...) {
    return Unit_templates{};
  }
  return out;
}

Expansion expand(const Template& t, const std::vector<Arg>& args, std::string_view qualifier, const std::set<std::string>& unit_names) {
  Expansion r;
  const auto fail = [&](std::string code, std::string msg, std::string hint) {
    r.text.clear();
    r.error_code    = std::move(code);
    r.error_message = std::move(msg);
    r.error_hint    = std::move(hint);
    return r;
  };

  std::string sig;  // `<W=32, K>` for messages
  for (const auto& p : t.params) {
    sig += (sig.empty() ? "" : ", ") + p.name + (p.has_default ? "=" + p.default_text : "");
  }
  sig = "<" + sig + ">";

  std::map<std::string, std::string> bound;
  size_t                             positional = 0;
  for (const auto& a : args) {
    if (!a.name.empty()) {
      continue;
    }
    if (positional >= t.params.size()) {
      return fail("type-alias-arity",
                  std::format("type alias `{}{}` takes {} generic argument{}, but {} {} given",
                              t.name,
                              sig,
                              t.params.size(),
                              t.params.size() == 1 ? "" : "s",
                              args.size(),
                              args.size() == 1 ? "was" : "were"),
                  "drop the extra argument");
    }
    bound[t.params[positional++].name] = a.text;
  }
  for (const auto& a : args) {
    if (a.name.empty()) {
      continue;
    }
    bool known = false;
    for (const auto& p : t.params) {
      known = known || p.name == a.name;
    }
    if (!known) {
      return fail("unknown-generic-name",
                  std::format("type alias `{}{}` has no generic named `{}`", t.name, sig, a.name),
                  "bind one of its generics by name, e.g. `" + t.name + "<" + (t.params.empty() ? "" : t.params.front().name) + "=…>`");
    }
    if (!bound.emplace(a.name, a.text).second) {
      return fail("type-alias-duplicate-binding",
                  std::format("generic `{}` of type alias `{}` is bound twice", a.name, t.name),
                  "bind each generic once, positionally or by name");
    }
  }
  // The body and the defaults speak in the declaring file's scope: a file-scope
  // name of that file they mention is written through the importer's binding
  // (`qualifier.name`). A generic is never a file-scope name.
  std::map<std::string, std::string> q;
  if (!qualifier.empty()) {
    for (const auto& n : unit_names) {
      q.emplace(n, std::string(qualifier) + "." + n);
    }
    for (const auto& p : t.params) {
      q.erase(p.name);
    }
  }
  const auto qualify = [&](std::string_view text) { return rewrite_names(text, q); };

  // Defaults, in declaration order: a default may use the generics before it.
  for (const auto& p : t.params) {
    if (bound.contains(p.name)) {
      continue;
    }
    if (!p.has_default) {
      return fail("missing-generic-argument",
                  std::format("type alias `{}{}` needs a value for generic `{}`", t.name, sig, p.name),
                  "write `" + t.name + "<" + p.name + "=…>` or give the generic a default in the alias");
    }
    std::string text;
    try {
      text = rewrite_names(qualify(p.default_text), bound);
    } catch (...) {
      text = p.default_text;
    }
    bound[p.name] = text;
  }

  try {
    // Qualify the body's own free names first, then substitute the generics, so
    // an argument (use-site text) is never qualified.
    r.text = rewrite_names(qualify(t.body), bound);
  } catch (const std::exception& e) {
    return fail("type-alias-expansion", std::format("type alias `{}` could not be expanded: {}", t.name, e.what()), "");
  }
  return r;
}

}  // namespace prp_type_alias
