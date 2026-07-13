// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "formal_blocks.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "prpparse/ast.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/prp_diag.hpp"
#include "prpparse/source_buffer.hpp"

namespace livehd::formal_blocks {

namespace {

using prpparse::Ast;
using prpparse::Field;
using prpparse::Kind;

std::string_view text_of(std::string_view src, const Ast* n) {
  return src.substr(n->start_byte, n->end_byte - n->start_byte);
}

int line_of(std::string_view src, uint32_t byte) {
  return 1 + static_cast<int>(std::count(src.begin(), src.begin() + std::min<size_t>(byte, src.size()), '\n'));
}

const Ast* find_field(const Ast* n, Field f) {
  for (const Ast* k : n->kids) {
    if (k->field == f) {
      return k;
    }
  }
  return nullptr;
}

// A dotted path "ident(.ident)+" — the shape a signal reference takes.
bool is_dotted_path(std::string_view t) {
  bool   seen_dot = false;
  bool   at_start = true;
  for (char c : t) {
    if (c == '.') {
      if (at_start) {
        return false;
      }
      seen_dot = true;
      at_start = true;
    } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      at_start = false;
    } else {
      return false;
    }
  }
  return seen_dot && !at_start;
}

std::string sanitize(std::string_view path) {
  std::string out{"__p_"};
  for (char c : path) {
    out += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
  }
  return out;
}

// One property statement's rewrite pass: find every maximal dot-chain rooted
// at a known alias, record it as a referenced input, and splice the generated
// identifier over its byte span. Nested function calls are out of the V2
// subset (a call INSIDE the condition — the outer assert/assume itself is the
// statement being processed).
struct Rewriter {
  std::string_view                               src;
  const absl::flat_hash_map<std::string, std::string>* aliases;  // alias -> target module ("" = top)
  absl::flat_hash_map<std::string, std::string>* path2ident;  // path -> ident (dedup)
  absl::flat_hash_set<std::string>*              used_roots;  // aliases this statement referenced
  std::vector<std::pair<uint32_t, uint32_t>>     spans;       // replaced [start,end)
  std::vector<std::string>                       idents;      // parallel to spans
  std::string                                    error;

  void walk(const Ast* n) {
    if (!error.empty()) {
      return;
    }
    std::string_view kn = prpparse::kind_name(n->kind);
    // Nested calls (casts u8(x), reductions, …) are allowed: the statement is
    // compiled through the REAL Pyrope pipeline, which rejects anything a comb
    // cannot do — no need to pre-restrict here.
    if (kn == "dot_expression" || kn == "member_selection") {
      std::string_view t = text_of(src, n);
      if (is_dotted_path(t)) {
        auto        dot  = t.find('.');
        std::string root(t.substr(0, dot));
        if (aliases->count(root) != 0) {
          used_roots->insert(root);
          std::string path(t.substr(dot + 1));
          auto        it = path2ident->find(path);
          std::string id = it != path2ident->end() ? it->second : sanitize(path);
          path2ident->emplace(path, id);
          spans.emplace_back(n->start_byte, n->end_byte);
          idents.push_back(std::move(id));
          return;  // maximal chain consumed — do not recurse into it
        }
      }
    }
    if (kn == "identifier") {
      std::string t(text_of(src, n));
      if (aliases->count(t) != 0) {
        error = "a bare design alias ('" + t + "') is not a signal — reference a dotted path like " + t + ".<signal>";
        return;
      }
    }
    for (const Ast* k : n->kids) {
      walk(k);
    }
  }
};

// `const X = import("f.mod")` / `mut X = <alias-or-module>`: register the
// alias with the target module it names ("" = the verified top). The statement
// text is regex-free string surgery on an already-parsed declaration, so the
// shapes are trusted loose. Works for FILE-level bindings (the canonical
// `const top = import(...)` above the blocks) and block-body ones alike.
bool parse_alias_binding(std::string_view stmt_text, absl::flat_hash_map<std::string, std::string>& alias_target,
                         std::string& error) {
  auto trim = [](std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
      s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
      s.remove_suffix(1);
    }
    return s;
  };
  std::string_view t = trim(stmt_text);
  bool             is_decl = false;
  for (std::string_view kw : {"const ", "mut "}) {
    if (t.substr(0, kw.size()) == kw) {
      t       = trim(t.substr(kw.size()));
      is_decl = true;
      break;
    }
  }
  if (!is_decl) {
    return false;
  }
  auto eq = t.find('=');
  if (eq == std::string_view::npos) {
    error = "formal-block declarations must be alias bindings (const X = import(...) / mut X = <alias>)";
    return true;
  }
  std::string      name(trim(t.substr(0, eq)));
  std::string      target;
  std::string_view rhs = trim(t.substr(eq + 1));
  if (rhs.substr(0, 7) == "import(") {
    // import("file.mod") — the target is the imported entity (post-'.' tail).
    auto q1 = rhs.find('"');
    auto q2 = q1 == std::string_view::npos ? std::string_view::npos : rhs.find('"', q1 + 1);
    if (q2 == std::string_view::npos) {
      error = "malformed import(...) in formal block";
      return true;
    }
    std::string spec(rhs.substr(q1 + 1, q2 - q1 - 1));
    auto        dot = spec.rfind('.');
    // "file.mod" imports entity mod; a bare "file" imports the whole file —
    // the driver resolves "" against the verified top either way.
    target = dot == std::string::npos ? std::string{} : spec.substr(dot + 1);
  } else if (auto it = alias_target.find(std::string(rhs)); it != alias_target.end()) {
    target = it->second;  // secondary alias of an existing alias: same target
  } else if (is_dotted_path(rhs) || rhs.find('(') != std::string_view::npos) {
    error = "formal-block alias RHS must be import(...), a prior alias, or a module name";
    return true;
  } else if (!rhs.empty()) {
    target = std::string(rhs);  // bare module name (same-file target)
  }
  if (!name.empty()) {
    alias_target.insert_or_assign(std::move(name), std::move(target));
  }
  return true;
}

Block build_block(std::string_view src, const std::string& path, const Ast* fnode,
                  const absl::flat_hash_map<std::string, std::string>& file_aliases, bool allow_nocheck) {
  Block b;
  b.line = line_of(src, fnode->start_byte);
  if (const Ast* name = find_field(fnode, Field::f_name)) {
    b.name = std::string(text_of(src, name));
  }
  const Ast* code = find_field(fnode, Field::f_code);
  if (code == nullptr) {
    b.error = path + ":" + std::to_string(b.line) + ": formal block has no body";
    return b;
  }
  absl::flat_hash_map<std::string, std::string> alias_target = file_aliases;  // file-scope bindings visible in-block
  absl::flat_hash_map<std::string, std::string> path2ident;
  absl::flat_hash_set<std::string>              used_roots;
  for (const Ast* stmt : code->kids) {
    std::string_view kn = prpparse::kind_name(stmt->kind);
    if (kn == "comment") {
      continue;
    }
    const int   sline = line_of(src, stmt->start_byte);
    std::string stext(text_of(src, stmt));
    auto err = [&](const std::string& why) { b.error = path + ":" + std::to_string(sline) + ": " + why; };

    if (kn == "declaration_statement" || kn == "assignment") {
      std::string e;
      if (parse_alias_binding(stext, alias_target, e)) {
        if (!e.empty()) {
          err(e);
          return b;
        }
        continue;
      }
      err("unsupported statement in formal block (V2: alias bindings and assert/assume/assert_always only)");
      return b;
    }

    // Property statement: an assert/assume/assert_always call.
    std::string callee;
    for (char c : stext) {
      if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
        callee += c;
      } else {
        break;
      }
    }
    const bool nocheck = callee == "assume_nocheck_formal" || callee == "assume_nocheck_synth";
    if (callee != "assert" && callee != "assume" && callee != "assert_always" && !(allow_nocheck && nocheck)) {
      // Only advertise the forms THIS caller accepts (a nocheck rejection that
      // lists assume_nocheck_* as supported is self-contradictory guidance).
      err(allow_nocheck ? "unsupported statement in formal block (alias bindings and "
                          "assert/assume/assert_always/assume_nocheck_* only; got '"
                              + callee + "')"
                        : "unsupported statement in formal block (alias bindings and assert/assume/assert_always only; got '"
                              + callee + "')");
      return b;
    }
    Rewriter rw{src, &alias_target, &path2ident, &used_roots, {}, {}, {}};
    rw.walk(stmt);
    if (!rw.error.empty()) {
      err(rw.error);
      return b;
    }
    // Splice the replacements (already in walk order; sort by start to be safe).
    std::vector<size_t> order(rw.spans.size());
    for (size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t c) { return rw.spans[a].first < rw.spans[c].first; });
    std::string out;
    uint32_t    pos = stmt->start_byte;
    for (size_t i : order) {
      out += src.substr(pos, rw.spans[i].first - pos);
      out += rw.idents[i];
      pos  = rw.spans[i].second;
    }
    out += src.substr(pos, stmt->end_byte - pos);
    std::sort(rw.idents.begin(), rw.idents.end());
    rw.idents.erase(std::unique(rw.idents.begin(), rw.idents.end()), rw.idents.end());
    b.stmts.push_back(Stmt{std::move(out), std::move(rw.idents), sline});
  }
  // The block's target module = the (unique) target of the aliases it USED.
  for (const auto& root : used_roots) {
    const std::string& t = alias_target.at(root);
    if (t.empty()) {
      continue;
    }
    if (!b.target.empty() && b.target != t) {
      b.error = path + ":" + std::to_string(b.line) + ": block references two different target modules ('" + b.target + "' and '"
                + t + "')";
      return b;
    }
    b.target = t;
  }
  for (const auto& [p, id] : path2ident) {
    b.inputs.push_back(Input{id, p});
  }
  std::sort(b.inputs.begin(), b.inputs.end(), [](const Input& a, const Input& c) { return a.ident < c.ident; });
  return b;
}

}  // namespace

std::vector<Block> extract(const std::string& path, bool allow_nocheck) {
  std::vector<Block> out;
  std::ifstream      f(path);
  if (!f) {
    Block b;
    b.error = path + ": cannot read file";
    out.push_back(std::move(b));
    return out;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string src = ss.str();

  try {
    prpparse::Source_buffer buf(path, src);
    prpparse::Parser        parser(buf);
    // File-scope alias bindings (`const top = import("file.mod")` ABOVE the
    // blocks — the canonical sidecar shape) are visible to every later block.
    absl::flat_hash_map<std::string, std::string> file_aliases;
    while (const Ast* top = parser.parse_next()) {
      std::string_view kn = prpparse::kind_name(top->kind);
      if (kn == "declaration_statement" || kn == "assignment") {
        std::string e;  // non-binding declarations are design code — ignore quietly
        (void)parse_alias_binding(text_of(src, top), file_aliases, e);
        continue;
      }
      if (kn != "formal_statement") {
        continue;
      }
      // parse_next() recycles the arena on the NEXT call, so build now.
      out.push_back(build_block(src, path, top, file_aliases, allow_nocheck));
    }
  } catch (const prpparse::Parse_error& pe) {
    Block b;
    b.error = path + ": parse error: " + std::string(pe.what());
    out.push_back(std::move(b));
  }
  return out;
}

}  // namespace livehd::formal_blocks
