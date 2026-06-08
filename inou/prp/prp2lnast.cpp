//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp2lnast.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "diag.hpp"
#include "pass.hpp"
#include "str_tools.hpp"

extern "C" TSLanguage* tree_sitter_pyrope();

static constexpr std::string_view call_ref_arg_marker  = "__ref_arg";
// Task 1k — marks the receiver actual of a UFCS method call `obj.method(...)`
// so the runner can reject UFCS onto a self-less callee (the direct call form
// `method(obj, ...)` lowers without it). Positional, like __ref_arg.
static constexpr std::string_view call_ufcs_arg_marker = "__ufcs_arg";

namespace {
std::string slurp_file(std::string_view filename) {
  std::string   fname(filename);
  auto          ss = std::ostringstream{};
  std::ifstream file(fname);
  ss << file.rdbuf();
  return ss.str();
}
}  // namespace

// File-backed entry: read `filename` from disk, then delegate to the buffer ctor.
Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name)
    : Prp2lnast(filename, module_name, slurp_file(filename)) {}

// Buffer-backed entry (LSP / unsaved buffers): analyze `source` verbatim.
Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name, std::string_view source) {
  lnast = std::make_shared<Lnast>(module_name);

  lnast->set_root(Lnast_ntype::create_top());

  // Builder co-owns lnast so its tmp counter and (future) cursor track the
  // same tree Prp2lnast is mutating directly.
  builder.lnast = lnast;

  src_filename = std::string(filename);
  prp_file     = std::string(source);

  parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  ts_tree      = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());
  ts_root_node = ts_tree_root_node(ts_tree);

  // dump();  // tree-sitter parse tree; enable for debugging the grammar

  // A throwing constructor does NOT run the destructor, so the TS resources would
  // leak on every reported error — which matters for the long-running LSP daemon
  // that re-parses on each keystroke. Clean up explicitly before rethrowing.
  try {
    // Stop before analyzing a file that did not parse. A MISSING node (a token the
    // parser had to insert to recover, e.g. an unbalanced `)`/`}`) is an
    // unambiguous syntax error — unlike ERROR nodes, which the pyrope grammar also
    // emits for valid `uint`/`sint` constructs, so a MISSING check is quirk-safe.
    check_parse_errors();

    process_description();  // runs the scope checks internally, before rewrite_decls_to_declare
  } catch (...) {
    ts_tree_delete(ts_tree);
    ts_parser_delete(parser);
    ts_tree = nullptr;
    parser  = nullptr;
    throw;
  }
}

Prp2lnast::~Prp2lnast() {
  if (ts_tree != nullptr) {
    ts_tree_delete(ts_tree);
  }
  if (parser != nullptr) {
    ts_parser_delete(parser);
  }
}

std::string_view Prp2lnast::get_text(const TSNode& node) const {
  auto start  = ts_node_start_byte(node);
  auto end    = ts_node_end_byte(node);
  auto length = end - start;

  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, length);
}

std::string_view Prp2lnast::text_between(uint32_t start, uint32_t end) const {
  if (start > end) {
    return {};
  }
  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, end - start);
}

void Prp2lnast::report_error(const TSNode& node, std::string_view code, std::string_view category, std::string message,
                             std::string_view hint) const {
  livehd::diag::Span span;
  span.file       = src_filename;
  span.start_byte = ts_node_start_byte(node);
  span.end_byte   = ts_node_end_byte(node);
  auto sp         = ts_node_start_point(node);
  auto ep         = ts_node_end_point(node);
  span.start_line = sp.row + 1;
  span.start_col  = sp.column + 1;
  span.end_line   = ep.row + 1;
  span.end_col    = ep.column + 1;

  auto msg_copy = message;
  // Stage the rich record; the throw path (parser_error_int -> sink.flush) emits
  // it exactly once and prints the `livehd: error:` line.
  livehd::diag::sink().stage(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                      .code     = std::string(code),
                                                      .category = std::string(category),
                                                      .pass     = "inou.prp",
                                                      .message  = std::move(message),
                                                      .span     = std::move(span),
                                                      .hint     = std::string(hint)});
  throw Eprp::parser_error(Pass::eprp, msg_copy);
}

void Prp2lnast::attach_loc(const Lnast_nid& idx, const TSNode& node) {
  if (idx.is_invalid() || ts_node_is_null(node)) {
    return;
  }
  const auto sp = ts_node_start_point(node);
  lnast->set_loc(idx, Lnast::Loc{.pos1 = ts_node_start_byte(node), .pos2 = ts_node_end_byte(node), .line = sp.row + 1, .tok = 0});
  lnast->set_fname(idx, src_filename);
}

namespace {
// First MISSING node in DFS order (a token the parser inserted to recover), or a
// null node. Prunes clean subtrees via ts_node_has_error (which is set whenever a
// descendant is an ERROR or a MISSING node).
TSNode find_first_missing(TSNode n) {
  if (ts_node_is_missing(n)) {
    return n;
  }
  uint32_t cnt = ts_node_child_count(n);
  for (uint32_t i = 0; i < cnt; i++) {
    TSNode c = ts_node_child(n, i);
    if (ts_node_has_error(c) || ts_node_is_missing(c)) {
      TSNode r = find_first_missing(c);
      if (!ts_node_is_null(r)) {
        return r;
      }
    }
  }
  return TSNode{};
}

// First ERROR node at *statement position* — a direct child of a statement list
// (`description` or `scope_statement`) — in DFS order, or a null node. Such an
// ERROR is a genuine syntax error: stray tokens the parser could not attach to
// any statement (e.g. `i c { … }`, junk before a block). The pyrope grammar also
// emits ERROR nodes for benign quirks (uint/sint type-calls, `::[attr]:T`, an
// unparenthesized multi-target tuple, an anonymous `comb`/`pipe` lambda), but
// those always nest inside an `assignment`/`type_cast` and never surface at
// statement position, so they are skipped. Prunes clean subtrees via
// ts_node_has_error.
TSNode find_first_error_statement(TSNode n) {
  std::string_view t(ts_node_type(n));
  const bool       is_stmt_list = (t == "description" || t == "scope_statement");
  uint32_t         cnt          = ts_node_child_count(n);
  for (uint32_t i = 0; i < cnt; i++) {
    TSNode c = ts_node_child(n, i);
    if (is_stmt_list && std::string_view(ts_node_type(c)) == "ERROR") {
      return c;
    }
    if (ts_node_has_error(c)) {
      TSNode r = find_first_error_statement(c);
      if (!ts_node_is_null(r)) {
        return r;
      }
    }
  }
  return TSNode{};
}

// ── Typed-declaration initializer kind check (shared by variables & params) ──
// A typed scalar declaration (`mut c:bool = …`, or a function parameter
// `b:bool = …`) constrains the initializer's kind: bool/int/string are distinct
// with NO implicit conversion. These mirror the upass `uPass_typecheck` kind
// model so prp2lnast can reject a literal initializer whose kind contradicts the
// declared type at parse time, identically for both declaration forms.
enum class Scalar_kind { unknown, integer, boolean, string };

const char* scalar_kind_name(Scalar_kind k) {
  switch (k) {
    case Scalar_kind::integer: return "integer";
    case Scalar_kind::boolean: return "boolean";
    case Scalar_kind::string : return "string";
    default                  : return "unknown";
  }
}

// Classify a literal initializer's scalar kind. Mirrors
// uPass_typecheck::seed_kind_from_const — keep the two in sync. A non-const, a
// `nil` copy, or the typeless single-bit unknown wildcard returns `unknown` (no
// static kind ⇒ no check; runtime/flow cases stay the upass pass's job).
Scalar_kind literal_scalar_kind(const Lnast_node& v) {
  if (!v.is_const()) {
    return Scalar_kind::unknown;
  }
  std::string_view t = v.get_name();
  if (t == "nil") {
    return Scalar_kind::unknown;  // `nil` is a legal initializer for any type
  }
  if (t == "true" || t == "false") {
    return Scalar_kind::boolean;
  }
  if (t == "0sb?" || t == "0ub?") {
    return Scalar_kind::unknown;  // single unknown bit is typeless (bool or int)
  }
  if (!t.empty() && t.front() == '"') {
    return Scalar_kind::string;  // double-quoted string literal
  }
  try {
    auto c = Dlop::from_pyrope(t);
    if (c && c->is_integer()) {
      return Scalar_kind::integer;  // numeric / char literal
    }
  } catch (...) {
  }
  return Scalar_kind::unknown;
}

// Declared scalar kind of a type node (`bool_type` / `string_type` /
// `uint_type` / `sint_type`). Composite/named/none types do not constrain the
// scalar kind here, so they read as `unknown` (check skipped).
Scalar_kind declared_scalar_kind(TSNode ty) {
  if (ts_node_is_null(ty)) {
    return Scalar_kind::unknown;
  }
  std::string_view t(ts_node_type(ty));
  if (t == "bool_type") {
    return Scalar_kind::boolean;
  }
  if (t == "string_type") {
    return Scalar_kind::string;
  }
  if (t == "uint_type" || t == "sint_type") {
    return Scalar_kind::integer;
  }
  return Scalar_kind::unknown;
}
}  // namespace

void Prp2lnast::check_decl_init_kind(std::string_view name, const Lnast_node& value, TSNode inner_type,
                                     const TSNode& anchor) const {
  // Shared by the variable-declaration arm of process_lvalue_for_assign and the
  // function-parameter emit_arg_assign: a typed scalar declaration's literal
  // initializer must match the declared kind (bool/int/string are distinct, no
  // implicit conversion). Both `mut c:bool = 10` and `comb f(b:bool = 3)` reach
  // here, so the diagnostic is identical for variables and parameters.
  Scalar_kind dk = declared_scalar_kind(inner_type);
  if (dk == Scalar_kind::unknown) {
    return;  // named/composite/unsized-named type — kind not constrained here
  }
  Scalar_kind vk = literal_scalar_kind(value);
  if (vk == Scalar_kind::unknown || vk == dk) {
    return;  // no static initializer kind, or kinds already agree
  }
  report_error(anchor,
               "decl-init-type-mismatch",
               "type",
               std::format("cannot initialize `{}` (declared {}) with {} value `{}`",
                           name,
                           scalar_kind_name(dk),
                           scalar_kind_name(vk),
                           value.get_name()),
               "no implicit conversion — the initializer must match the declared type, or cast explicitly "
               "(e.g. `int(x)`, `x == false`)");
}

void Prp2lnast::check_parse_errors() const {
  if (!ts_node_has_error(ts_root_node)) {
    return;  // clean parse
  }
  // A MISSING node (a token the parser had to insert to recover) is the most
  // unambiguous syntax error, so prefer it.
  TSNode miss = find_first_missing(ts_root_node);
  if (!ts_node_is_null(miss)) {
    // ts_node_type of a MISSING node is the expected symbol (e.g. `)`), so name it.
    auto expected = std::string_view(ts_node_type(miss));
    report_error(miss,
                 "missing-token",
                 "syntax",
                 std::format("syntax error: expected '{}'", expected),
                 "the file does not parse — check for an unbalanced '(' / ')', '[' / ']', '{' / '}', or a dropped token");
  }
  // No inserted token, but the parser may have parked stray tokens in an ERROR
  // node at statement position (e.g. `i c { … }` — junk before a block). Report
  // it with a hint tailored to what follows. Benign grammar quirks nest their
  // ERROR inside an `assignment`/`type_cast` (never at statement position), so
  // `find_first_error_statement` skips them and they stay tolerated.
  TSNode err = find_first_error_statement(ts_root_node);
  if (!ts_node_is_null(err)) {
    auto             stray        = trim(get_text(err));
    TSNode           nxt          = ts_node_next_named_sibling(err);
    const bool       before_block = !ts_node_is_null(nxt) && std::string_view(ts_node_type(nxt)) == "scope_statement";
    std::string_view hint
        = before_block ? "stray tokens before a `{ … }` block — did you mean a control statement "
                         "(`if`/`for`/`while`/`match`/`loop`) or a lambda (`comb`/`pipe`/`mod`)?"
                       : "the file does not parse — remove the unexpected token(s) or check for a dropped keyword/operator";
    report_error(err, "syntax-error", "syntax", std::format("syntax error: unexpected `{}`", stray), hint);
  }
  // else: only benign ERROR nodes (uint/sint type-calls, `::[attr]:T`, …) — tolerated.
}

void Prp2lnast::report_error(std::string_view code, std::string_view category, std::string message, std::string_view hint) const {
  auto msg_copy = message;
  livehd::diag::sink().stage(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                      .code     = std::string(code),
                                                      .category = std::string(category),
                                                      .pass     = "inou.prp",
                                                      .message  = std::move(message),
                                                      .hint     = std::string(hint)});
  throw Eprp::parser_error(Pass::eprp, msg_copy);
}

void Prp2lnast::report_error(const Lnast_nid& nid, std::string_view code, std::string_view category, std::string message,
                             std::string_view hint) const {
  const auto loc = nid.is_invalid() ? Lnast::Loc{} : lnast->get_loc(nid);
  if (loc.line == 0) {
    report_error(code, category, std::move(message), hint);  // no attached span — fall back to location-less
  }
  livehd::diag::Span span;
  auto               fn = lnast->get_fname(nid);
  span.file             = fn.empty() ? src_filename : std::string(fn);
  span.start_byte       = loc.pos1;
  span.end_byte         = loc.pos2;
  span.start_line       = loc.line;
  span.start_col        = 1;  // pre-sourcemap: column not recovered from the LNAST loc
  span.end_line         = loc.line;
  span.end_col          = 1;

  auto msg_copy = message;
  livehd::diag::sink().stage(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                      .code     = std::string(code),
                                                      .category = std::string(category),
                                                      .pass     = "inou.prp",
                                                      .message  = std::move(message),
                                                      .span     = std::move(span),
                                                      .hint     = std::string(hint)});
  throw Eprp::parser_error(Pass::eprp, msg_copy);
}

namespace {
bool prp_name_is_tmp(std::string_view n) { return n.size() >= 3 && n[0] == '_' && n[1] == '_' && n[2] == '_'; }

// `_` (sole arg) and `_0`/`_1`/… (positional args) are the implicit
// placeholder-lambda parameters (`comb add(a,b){ _0 + _1 }`). They are never
// declared yet are valid reads, so they are not "undefined". (A regular
// leading-underscore name like `_val` has a non-digit tail and is NOT a
// placeholder — it must be declared like any other variable.)
bool prp_name_is_placeholder_arg(std::string_view n) {
  if (n == "_") {
    return true;
  }
  if (n.size() < 2 || n[0] != '_') {
    return false;
  }
  for (size_t i = 1; i < n.size(); ++i) {
    if (n[i] < '0' || n[i] > '9') {
      return false;
    }
  }
  return true;
}

// Collect store/declare TARGET ref names within a func_def signature (the
// func_def children other than the body `stmts`) — its params and outputs.
void prp_collect_sig_targets(const Lnast* ln, const Lnast_nid& node, std::unordered_set<std::string>& out) {
  auto t = ln->get_type(node);
  if (Lnast_ntype::is_store(t) || Lnast_ntype::is_declare(t)) {
    auto v = ln->get_first_child(node);
    if (!v.is_invalid() && Lnast_ntype::is_ref(ln->get_type(v))) {
      out.insert(std::string(ln->get_name(v)));
    }
    return;
  }
  for (auto c = ln->get_first_child(node); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(ln->get_type(c))) {
      continue;  // never descend into the body
    }
    prp_collect_sig_targets(ln, c, out);
  }
}
}  // namespace

void Prp2lnast::check_undeclared_writes() const {
  for (auto c = lnast->get_first_child(lnast->get_root()); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(lnast->get_type(c))) {
      check_writes_in_scope(c, {});
    }
  }
}

// A statement-level `store` to a non-tmp name with no `mut`/`const`/declare (or
// param/output) visible in scope is `a = 3` with no prior declaration — an error.
// `visible` carries names declared in enclosing frames; a func body is a fresh
// namespace seeded with its params/outputs.
void Prp2lnast::check_writes_in_scope(const Lnast_nid& scope_stmts, const std::unordered_set<std::string>& visible,
                                      const std::unordered_set<std::string>& seed_here) const {
  std::unordered_set<std::string> here = seed_here;
  for (auto c = lnast->get_first_child(scope_stmts); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    const auto ct = lnast->get_type(c);

    // A bare `stmts` block (e.g. the for-loop unroll wrapper) is a nested scope.
    if (Lnast_ntype::is_stmts(ct)) {
      std::unordered_set<std::string> combined = visible;
      combined.insert(here.begin(), here.end());
      check_writes_in_scope(c, combined);
      continue;
    }

    // A `for` node declares its binding vars — value (child0) plus the optional
    // idx (child4) / key (child5) of `for (value, idx, key) in t`. The
    // runner-unroll form carries no attr_set decl, so register them here: they
    // are visible inside the body and may be written, and a binding that reuses
    // an enclosing name is variable shadowing (located at the `for`). Layout:
    // for(value, iterable, body, mode [, idx [, key]]).
    if (Lnast_ntype::is_for(ct)) {
      std::unordered_set<std::string> binds;
      Lnast_nid                       body_stmts;
      int                             pos = 0;
      for (auto cc = lnast->get_first_child(c); !cc.is_invalid(); cc = lnast->get_sibling_next(cc), ++pos) {
        if (pos == 2) {
          body_stmts = cc;  // the loop body
        } else if ((pos == 0 || pos == 4 || pos == 5) && Lnast_ntype::is_ref(lnast->get_type(cc))) {
          std::string name(lnast->get_name(cc));
          if (name.empty() || prp_name_is_tmp(name)) {
            continue;
          }
          // Shadows if the name is already declared in an ENCLOSING scope
          // (`visible`) or earlier in THIS scope (`here`) — `mut c` then
          // `for c …` at the same level both forbid reusing `c`.
          if (visible.contains(name) || here.contains(name)) {
            report_error(c,
                         "variable-shadowing",
                         "name",
                         std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
                         "rename the inner/loop variable, or assign without a new `mut`/`const`/`reg` to reuse the outer one");
          }
          binds.insert(std::move(name));
        }
      }
      if (!body_stmts.is_invalid()) {
        std::unordered_set<std::string> combined = visible;
        combined.insert(here.begin(), here.end());
        combined.insert(binds.begin(), binds.end());
        check_writes_in_scope(body_stmts, combined);
      }
      continue;
    }

    auto first_ref_name = [&](const Lnast_nid& node) -> std::string {
      auto c0 = lnast->get_first_child(node);
      if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
        return std::string(lnast->get_name(c0));
      }
      return {};
    };

    // The declaration mode (child2 of `declare`/`attr_set`-type): "mut" / "const"
    // / "reg" for a variable, vs "type" / enum / func kinds we do not shadow-check.
    auto third_child_name = [&](const Lnast_nid& node) -> std::string_view {
      auto a = lnast->get_first_child(node);
      if (a.is_invalid()) {
        return {};
      }
      auto b = lnast->get_sibling_next(a);
      if (b.is_invalid()) {
        return {};
      }
      auto d = lnast->get_sibling_next(b);
      return d.is_invalid() ? std::string_view{} : lnast->get_name(d);
    };
    auto is_var_mode = [](std::string_view m) { return m == "mut" || m == "const" || m == "reg"; };

    // No-shadowing rule (04-variables.md "Variable scope"): a `mut`/`const`/`reg`
    // declaration — including a `for` loop iterator — may NOT reuse a name that is
    // already visible in an enclosing scope. (Plain re-assignment `a = e` is fine;
    // it is a `store`, not a declaration. `type`/enum/func declarations are not
    // shadow-checked here.) Located via the span attached to the declaring node.
    auto check_shadowing = [&](const Lnast_nid& node, const std::string& name) {
      if (!name.empty() && is_var_mode(third_child_name(node)) && visible.contains(name)) {
        report_error(node,
                     "variable-shadowing",
                     "name",
                     std::format("variable shadowing: '{}' is already declared in an enclosing scope", name),
                     "rename the inner/loop variable, or assign without a new `mut`/`const`/`reg` to reuse the outer one");
      }
    };

    if (Lnast_ntype::is_declare(ct)) {
      auto name = first_ref_name(c);
      if (!name.empty()) {
        check_shadowing(c, name);
        here.insert(name);
      }
    } else if (Lnast_ntype::is_attr_set(ct)) {
      // Legacy declaration form still emitted by some lowerings (e.g. the
      // for-loop iter var): attr_set(var, "type", <mode>) declares `var`.
      auto c0 = lnast->get_first_child(c);
      auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
      if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0)) && !c1.is_invalid() && lnast->get_name(c1) == "type") {
        auto name = std::string(lnast->get_name(c0));
        check_shadowing(c, name);
        here.insert(name);
      }
    } else if (Lnast_ntype::is_store(ct)) {
      auto name = first_ref_name(c);
      if (!name.empty() && !prp_name_is_tmp(name) && !here.contains(name) && !visible.contains(name)) {
        report_error("assign-no-decl",
                     "name",
                     std::format("assignment to undeclared variable '{}' (declare it with `mut`/`const` first)", name));
      }
    }

    const bool                      is_func_body = Lnast_ntype::is_func_def(ct);
    std::unordered_set<std::string> sig;
    if (is_func_body) {
      prp_collect_sig_targets(lnast.get(), c, sig);
    }
    for (auto cc = lnast->get_first_child(c); !cc.is_invalid(); cc = lnast->get_sibling_next(cc)) {
      if (Lnast_ntype::is_stmts(lnast->get_type(cc))) {
        if (is_func_body) {
          // Params/outputs belong to the body's OWN scope: seed `here` (so a
          // body-level `mut res = …` initializing an output is not shadowing),
          // with no enclosing `visible` (a lambda does not see outer runtime
          // vars). A NESTED re-declaration still shadows — `here` flows down.
          check_writes_in_scope(cc, {}, sig);
        } else {
          std::unordered_set<std::string> combined = visible;
          combined.insert(here.begin(), here.end());
          check_writes_in_scope(cc, combined);
        }
      }
    }
  }
}

// Collect the order-independent names: function names (`comb/pipe/mod
// NAME(...)` — func_def child0, used as a value in higher-order calls like
// `apply_each(add_1)`) and type/enum declarations (declare/attr_set with a
// non-var mode). These are comptime entities — forward references are fine.
void Prp2lnast::collect_hoisted_names(const Lnast_nid& node, std::unordered_set<std::string>& hoisted) const {
  const auto t = lnast->get_type(node);
  if (Lnast_ntype::is_func_def(t)) {
    auto c0 = lnast->get_first_child(node);
    if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
      hoisted.insert(std::string(lnast->get_name(c0)));
    }
  } else if (Lnast_ntype::is_declare(t) || Lnast_ntype::is_attr_set(t)) {
    // declare(name, TYPE, mode) — `type Foo = …` / `enum Foo = …` carry mode
    // "type". attr_set(name, "type", mode) is the pre-rewrite decl form; var
    // modes (mut/const/reg/stage) are program-order scoped, anything else is
    // a comptime entity.
    auto c0 = lnast->get_first_child(node);
    if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
      auto c1 = lnast->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
      if (!c2.is_invalid() && (Lnast_ntype::is_declare(t) || lnast->get_name(c1) == "type")) {
        auto mode = lnast->get_name(c2);
        if (mode != "mut" && mode != "const" && mode != "reg" && mode != "stage") {
          hoisted.insert(std::string(lnast->get_name(c0)));
        }
      }
    }
  }
  for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
    collect_hoisted_names(c, hoisted);
  }
}

// Lexical visibility of one recorded read: scan the recorded frame up to the
// read position (a declaration is visible from its statement onward), then
// climb the enclosing frames. Crossing a func_def boundary also checks its
// signature (params/outputs); names visible at the lambda's definition point
// stay readable inside the body — whether the captured VALUE is
// comptime-constant is the runner's closure-capture check (entry 1x, see
// closure_capture.prp), not a parse-time rule. Crossing a `for` node makes
// its iterator visible (the runner-unroll form carries no attr_set decl).
bool Prp2lnast::read_is_visible(const Read_site& rs) const {
  auto first_ref_is_name = [&](const Lnast_nid& node) -> bool {
    auto c0 = lnast->get_first_child(node);
    return !c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0)) && lnast->get_name(c0) == rs.name;
  };
  // A `for` node binds value (child0) plus the optional idx (child4) / key
  // (child5) of `for (value, idx, key) in t`. Any of them is visible in the body.
  // Layout: for(value, iterable, body, mode [, idx [, key]]).
  auto for_binds_name = [&](const Lnast_nid& node) -> bool {
    int pos = 0;
    for (auto c = lnast->get_first_child(node); !c.is_invalid(); c = lnast->get_sibling_next(c), ++pos) {
      if ((pos == 0 || pos == 4 || pos == 5) && Lnast_ntype::is_ref(lnast->get_type(c)) && lnast->get_name(c) == rs.name) {
        return true;
      }
    }
    return false;
  };
  auto second_child_name = [&](const Lnast_nid& node) -> std::string_view {
    auto c0 = lnast->get_first_child(node);
    if (c0.is_invalid()) {
      return {};
    }
    auto c1 = lnast->get_sibling_next(c0);
    return c1.is_invalid() ? std::string_view{} : lnast->get_name(c1);
  };

  // Does this statement-level node declare rs.name? A gated declaration
  // (`mut x = e when c`) lowers to a FLAT `if` whose direct children are the
  // decl statements — by design it declares into the surrounding scope
  // (process_gated_statement), so look through `if` nodes' direct children
  // too. Regular if/while arms live inside `stmts` wrappers (their own
  // scope), which this deliberately does not descend into.
  std::function<bool(const Lnast_nid&)> stmt_declares = [&](const Lnast_nid& c) -> bool {
    const auto ct = lnast->get_type(c);
    if (Lnast_ntype::is_declare(ct) && first_ref_is_name(c)) {
      return true;
    }
    if (Lnast_ntype::is_attr_set(ct) && first_ref_is_name(c) && second_child_name(c) == "type") {
      return true;
    }
    if (Lnast_ntype::is_if(ct)) {
      for (auto gc = lnast->get_first_child(c); !gc.is_invalid(); gc = lnast->get_sibling_next(gc)) {
        if (!Lnast_ntype::is_stmts(lnast->get_type(gc)) && stmt_declares(gc)) {
          return true;
        }
      }
    }
    return false;
  };

  Lnast_nid frame    = rs.scope;
  Lnast_nid boundary = rs.before;  // inclusive; invalid = frame was empty at read time

  while (!frame.is_invalid()) {
    if (!boundary.is_invalid()) {
      for (auto c = lnast->get_first_child(frame); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
        if (stmt_declares(c)) {
          return true;
        }
        if (c == boundary) {
          break;
        }
      }
    }
    // Climb to the enclosing frame: walk up to the statement-level node that
    // contains this frame; its parent is the next frame and it becomes the
    // new (inclusive) boundary. Binding constructs on the way contribute
    // their introduced names.
    Lnast_nid child = frame;
    Lnast_nid p     = lnast->get_parent(frame);
    while (!p.is_invalid() && !lnast->is_root(p) && !Lnast_ntype::is_stmts(lnast->get_type(p))) {
      const auto pt = lnast->get_type(p);
      if (Lnast_ntype::is_func_def(pt)) {
        std::unordered_set<std::string> sig;
        prp_collect_sig_targets(lnast.get(), p, sig);
        if (sig.contains(rs.name)) {
          return true;
        }
      } else if (Lnast_ntype::is_for(pt) && for_binds_name(p)) {
        // for(value, iterable, body, mode [, idx [, key]]) — runner-unroll form:
        // the value/idx/key binds are implicitly declared by the loop itself.
        return true;
      }
      child = p;
      p     = lnast->get_parent(p);
    }
    if (p.is_invalid() || lnast->is_root(p)) {
      break;  // ran out of enclosing frames
    }
    frame    = p;
    boundary = child;
  }
  return false;
}

bool Prp2lnast::name_in_inflight_scope(std::string_view name) const {
  for (const auto& frame : inflight_name_scopes_) {
    for (const auto& s : frame) {
      if (s == name) {
        return true;
      }
    }
  }
  return false;
}

void Prp2lnast::check_undefined_reads() const {
  std::unordered_set<std::string> hoisted;
  collect_hoisted_names(lnast->get_root(), hoisted);

  for (const auto& rs : read_sites_) {
    if (rs.name.empty() || rs.name == "self" || prp_name_is_tmp(rs.name) || prp_name_is_placeholder_arg(rs.name)
        || hoisted.contains(rs.name)) {
      continue;
    }
    if (read_is_visible(rs)) {
      continue;
    }
    // report_error throws — the first undefined read aborts the parse. The
    // TSNode gives a located diagnostic (the read site).
    report_error(rs.node,
                 "undefined-read",
                 "name",
                 std::format("read of undefined variable '{}' (not visible here: declared later, out of scope, or never)", rs.name),
                 "declare it with `mut`/`const`/`reg` before use — a variable is visible from its "
                 "declaration to the end of its scope");
  }
}

std::string_view Prp2lnast::trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

void Prp2lnast::dump_tree_sitter() const {
  auto tc = ts_tree_cursor_new(ts_root_node);

  dump_tree_sitter(&tc, 1);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::dump_tree_sitter(TSTreeCursor* tc, int level) const {
  auto indent = std::string(level * 2, ' ');

  bool go_next = true;
  while (go_next) {
    auto             node         = ts_tree_cursor_current_node(tc);
    auto             num_children = ts_node_child_count(node);
    std::string_view node_type(ts_node_type(node));

    std::print("{}{} {}\n", indent, node_type, num_children);

    if (num_children) {
      ts_tree_cursor_goto_first_child(tc);
      dump_tree_sitter(tc, level + 1);
      ts_tree_cursor_goto_parent(tc);
    }

    go_next = ts_tree_cursor_goto_next_sibling(tc);
  }
}

void Prp2lnast::dump() const {
  std::cout << "tree-sitter-dump\n";

  dump_tree_sitter();
}

inline TSNode Prp2lnast::child_by_field(const TSNode& node, const char* field) const {
  return ts_node_child_by_field_name(node, field, std::char_traits<char>::length(field));
}

// ---------------- Top level ----------------

void Prp2lnast::process_description() {
  builder.idx_stmts = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
  walk_statement_block(ts_root_node);
  // Run the scope checks on the PRE-rewrite tree: declarations are still in
  // their `attr_set(t,"type",K)` form here, with their source spans intact and
  // each scope's declarations un-merged. rewrite_decls_to_declare folds
  // same-name declaration clusters and does not preserve declare-node locs, so
  // running the no-shadowing / undeclared / undefined-read checks afterward
  // would both lose the diagnostic location and miss a `for c` iterator that
  // shadows an outer `mut c` (the merge collapses them).
  check_undeclared_writes();
  check_undefined_reads();
  rewrite_decls_to_declare();
}

namespace {
// Copy one LNAST node (preserving ref/const text) under dst_parent.
Lnast_nid prp_copy_one_node(const Lnast& src, const Lnast_nid& src_nid, Lnast& dst, const Lnast_nid& dst_parent) {
  const auto t = src.get_type(src_nid);
  Lnast_nid  nn;
  if (Lnast_ntype::is_ref(t)) {
    nn = dst.add_child(dst_parent, Lnast_node::create_ref(src.get_name(src_nid)));
  } else if (Lnast_ntype::is_const(t)) {
    nn = dst.add_child(dst_parent, Lnast_node::create_const(src.get_name(src_nid)));
  } else {
    nn = dst.add_child(dst_parent, t);
  }
  // Preserve the source location across the decl-merge rebuild. Only cassert,
  // func_call, if, while, store, declare and range nodes carry one today (see
  // attach_loc); gating on the type keeps this off the per-node hot path (no
  // get_loc lookup for the many string/ref/const nodes a string-heavy tree
  // copies). func_call carries it so the upass argument-naming diagnostics can
  // point at the call site; if/while carry it so upass/typecheck's
  // cond-not-bool can point at the condition; store/declare carry it so
  // bitwidth's "does not fit" can point at the write; range carries it so
  // constprop's descending-range error can point at the `a..=b`.
  if (t == Lnast_ntype::Lnast_ntype_cassert || t == Lnast_ntype::Lnast_ntype_func_call || t == Lnast_ntype::Lnast_ntype_if
      || t == Lnast_ntype::Lnast_ntype_while || t == Lnast_ntype::Lnast_ntype_store || t == Lnast_ntype::Lnast_ntype_declare
      || t == Lnast_ntype::Lnast_ntype_range || t == Lnast_ntype::Lnast_ntype_attr_set) {
    const auto loc = src.get_loc(src_nid);
    if (loc.pos1 != 0 || loc.pos2 != 0 || loc.line != 0 || loc.tok != 0) {
      dst.set_loc(nn, loc);
      if (auto fn = src.get_fname(src_nid); !fn.empty()) {
        dst.set_fname(nn, fn);
      }
    }
  }
  return nn;
}

// Faithful recursive subtree copy (no merging) — used for the TYPE subtree.
void prp_copy_subtree(const Lnast& src, const Lnast_nid& src_nid, Lnast& dst, const Lnast_nid& dst_parent) {
  auto nn = prp_copy_one_node(src, src_nid, dst, dst_parent);
  for (auto c : src.children(src_nid)) {
    prp_copy_subtree(src, c, dst, nn);
  }
}

// Name of the Nth child of `nid` (0-based), or "" if absent.
std::string_view prp_child_name(const Lnast& src, const Lnast_nid& nid, int n) {
  int i = 0;
  for (auto c : src.children(nid)) {
    if (i == n) {
      return src.get_name(c);
    }
    ++i;
  }
  return {};
}
// The Nth child nid, or invalid.
Lnast_nid prp_child_nid(const Lnast& src, const Lnast_nid& nid, int n) {
  int i = 0;
  for (auto c : src.children(nid)) {
    if (i == n) {
      return c;
    }
    ++i;
  }
  return Lnast_nid{};
}
}  // namespace

void Prp2lnast::rewrite_decls_to_declare() {
  auto staging = std::make_shared<Lnast>(lnast->forest()->create_tree_temp("decl-merge"), lnast->get_top_module_name());

  // Recursive copy; at each `stmts` block, fold the declaration cluster
  // (attr_set(t,"type",K) [+ attr_set(t,"comptime")] [+ type_spec(t,TYPE)])
  // into one `declare(ref(t), TYPE|none_type, const(mode))`. Everything else —
  // including the value `store`, the `typename` attr_set, and nested blocks —
  // is copied verbatim (recursing so nested stmts merge too).
  std::function<Lnast_nid(const Lnast_nid&, const Lnast_nid&)> copy_merge
      = [&](const Lnast_nid& src_nid, const Lnast_nid& dst_parent) -> Lnast_nid {
    auto nn   = prp_copy_one_node(*lnast, src_nid, *staging, dst_parent);
    auto type = lnast->get_type(src_nid);
    if (!Lnast_ntype::is_stmts(type)) {
      for (auto c : lnast->children(src_nid)) {
        copy_merge(c, nn);
      }
      return nn;
    }
    // stmts block — scan children with cluster lookahead.
    std::vector<Lnast_nid> kids;
    for (auto c : lnast->children(src_nid)) {
      kids.push_back(c);
    }
    auto is_type_attr_set = [&](const Lnast_nid& k) {
      return Lnast_ntype::is_attr_set(lnast->get_type(k)) && prp_child_name(*lnast, k, 1) == "type";
    };
    for (size_t i = 0; i < kids.size();) {
      const auto& k = kids[i];
      if (!is_type_attr_set(k)) {
        copy_merge(k, nn);
        ++i;
        continue;
      }
      // Cluster head: attr_set(t, "type", KIND).
      std::string target(prp_child_name(*lnast, k, 0));
      std::string kind(prp_child_name(*lnast, k, 2));
      bool        comptime  = false;
      Lnast_nid   type_node = {};
      size_t      j         = i + 1;
      if (j < kids.size() && Lnast_ntype::is_attr_set(lnast->get_type(kids[j])) && prp_child_name(*lnast, kids[j], 1) == "comptime"
          && prp_child_name(*lnast, kids[j], 0) == target) {
        comptime = true;
        ++j;
      }
      if (j < kids.size() && Lnast_ntype::is_type_spec(lnast->get_type(kids[j])) && prp_child_name(*lnast, kids[j], 0) == target) {
        type_node = kids[j];
        ++j;
      }
      // Emit declare(ref(t), TYPE|none_type, const(mode)).
      auto d = staging->add_child(nn, Lnast_ntype::create_declare());
      // Carry the cluster head's source span (attached at the attr_set
      // creation) so declaration-site diagnostics stay located post-merge.
      {
        const auto loc = lnast->get_loc(k);
        if (loc.pos1 != 0 || loc.pos2 != 0 || loc.line != 0 || loc.tok != 0) {
          staging->set_loc(d, loc);
          if (auto fn = lnast->get_fname(k); !fn.empty()) {
            staging->set_fname(d, fn);
          }
        }
      }
      staging->add_child(d, Lnast_node::create_ref(target));
      Lnast_nid tnid = type_node.is_invalid() ? Lnast_nid{} : prp_child_nid(*lnast, type_node, 1);
      if (!tnid.is_invalid()) {
        prp_copy_subtree(*lnast, tnid, *staging, d);
      } else {
        staging->add_child(d, Lnast_ntype::create_prim_type_none());
      }
      // Task 1r — a `stage[N]` decl lowers to a `reg` declare carrying a
      // trailing stages(min,max) node (the upass/pipe insertion shape): tolg
      // lowers it as one depth-N pipeline Flop (pipe_min/pipe_max pins).
      const bool  is_stage = kind == "stage";
      std::string mode(is_stage ? "reg" : kind);
      if (comptime) {
        if (!mode.empty()) {
          mode.push_back(' ');
        }
        mode += "comptime";
      }
      staging->add_child(d, Lnast_node::create_const(mode));
      if (is_stage) {
        // The (min,max) rides the cluster head as its trailing child.
        auto st = prp_child_nid(*lnast, k, 3);
        if (!st.is_invalid() && Lnast_ntype::is_stages(lnast->get_type(st))) {
          prp_copy_subtree(*lnast, st, *staging, d);
        }
      } else if (kind == "reg") {
        // 2d-reg / 3j — the reg initializer rides the cluster head (child 3);
        // fold it into the declare's optional 4th [value] child so tolg can
        // recover the reset value and the runner never binds it as a comptime
        // value (reg reads are runtime q reads).
        auto iv = prp_child_nid(*lnast, k, 3);
        if (!iv.is_invalid()) {
          prp_copy_subtree(*lnast, iv, *staging, d);
        }
      }
      i = j;
    }
    return nn;
  };

  auto src_root = lnast->get_root();
  auto dst_root = staging->set_root(lnast->get_type(src_root));
  for (auto c : lnast->children(src_root)) {
    copy_merge(c, dst_root);
  }
  lnast->replace_body(staging->tree_ptr());
}

void Prp2lnast::walk_statement_block(TSNode parent) {
  // Walk ALL children (not just named) so the anonymous `wrap`/`sat` token
  // that the new grammar attaches as the `overflow` field on an assignment
  // statement can be detected — it precedes the assignment as a sibling
  // because `_statement` is hidden. Similarly, a `when_unless_cond` node
  // that the grammar attaches as the `gate` field of the hidden `_semicolon`
  // bubbles up as a sibling immediately after the gated statement.
  using namespace std::string_view_literals;
  const uint32_t   nc = ts_node_child_count(parent);
  std::string_view pending_overflow;
  // Track the end byte of the previous processed child so we can scan the
  // unparsed source span between siblings for the `wrap` / `sat` overflow
  // prefix. The new grammar hides those tokens entirely (they are part of
  // a hidden `_statement` seq with a field tag), so they no longer surface
  // even as anonymous children.
  uint32_t         prev_end             = ts_node_start_byte(parent);
  auto             scan_overflow_in_gap = [&](uint32_t gap_end) -> std::string_view {
    if (prev_end >= gap_end) {
      return {};
    }
    auto gap = text_between(prev_end, gap_end);
    auto kw  = trim(gap);
    if (kw == "wrap"sv) {
      return "wrap"sv;
    }
    if (kw == "sat"sv) {
      return "sat"sv;
    }
    return {};
  };
  for (uint32_t i = 0; i < nc; i++) {
    TSNode           c = ts_node_child(parent, i);
    std::string_view t(ts_node_type(c));

    // `gate` siblings are consumed by the previous statement via lookahead.
    const char* fname = ts_node_field_name_for_child(parent, i);
    if (fname && std::string_view(fname) == "gate"sv) {
      continue;
    }

    if (!ts_node_is_named(c)) {
      if (t == "wrap"sv || t == "sat"sv) {
        pending_overflow = t;
      }
      continue;
    }
    // A scope_statement that was already consumed as a lambda body must
    // not be re-walked here, otherwise the body content emits twice
    // (once inside the lambda's body_idx, once as an orphan top-level
    // stmts). See `consumed_lambda_body_starts`.
    if (consumed_lambda_body_starts.contains(ts_node_start_byte(c))) {
      pending_overflow = {};
      prev_end         = ts_node_end_byte(c);
      continue;
    }

    // Look ahead for a sibling whose field name is `gate` — it carries the
    // when/unless condition that gates this statement.
    TSNode gate{};
    bool   have_gate = false;
    for (uint32_t j = i + 1; j < nc; j++) {
      TSNode      g  = ts_node_child(parent, j);
      const char* gf = ts_node_field_name_for_child(parent, j);
      if (gf && std::string_view(gf) == "gate"sv) {
        gate      = g;
        have_gate = true;
        break;
      }
      if (ts_node_is_named(g)) {
        break;
      }
    }

    const bool is_assign = (t == "assignment"sv);
    if (is_assign && pending_overflow.empty()) {
      // Fallback: the grammar hides the `wrap`/`sat` token; recover it
      // from the source text between the previous statement and this one.
      pending_overflow = scan_overflow_in_gap(ts_node_start_byte(c));
    }
    if (is_assign && !pending_overflow.empty()) {
      pending_overflow_kind = pending_overflow;
    }
    if (have_gate) {
      process_gated_statement(c, gate);
    } else {
      process_statement(c);
    }
    pending_overflow_kind = {};
    pending_overflow      = {};
    prev_end              = ts_node_end_byte(c);
  }
}

void Prp2lnast::process_statement(TSNode n) {
  if (ts_node_is_null(n)) {
    return;
  }
  using Handler                                                             = void (Prp2lnast::*)(TSNode);
  static const absl::flat_hash_map<std::string_view, Handler> stmt_dispatch = {
      {  "declaration_statement",   &Prp2lnast::process_declaration_statement},
      {             "assignment",              &Prp2lnast::process_assignment},
      {       "assert_statement",        &Prp2lnast::process_assert_statement},
      {        "while_statement",         &Prp2lnast::process_while_statement},
      {          "for_statement",           &Prp2lnast::process_for_statement},
      {         "loop_statement",          &Prp2lnast::process_loop_statement},
      {      "control_statement",       &Prp2lnast::process_control_statement},
      {"function_call_statement", &Prp2lnast::process_function_call_statement},
      {                 "lambda",        &Prp2lnast::process_lambda_statement},
      {        "enum_assignment",         &Prp2lnast::process_enum_assignment},
      {         "type_statement",          &Prp2lnast::process_type_statement},
      {       "import_statement",        &Prp2lnast::process_import_statement},
      {         "test_statement",          &Prp2lnast::process_test_statement},
      {        "spawn_statement",         &Prp2lnast::process_spawn_statement},
      {         "impl_statement",          &Prp2lnast::process_impl_statement},
  };
  // Expression-as-statement node kinds (lowered for side effects).
  static const absl::flat_hash_set<std::string_view> expr_stmt = {
      "if_expression",
      "match_expression",
      "expression_item",
      "unary_expression",
      "bit_selection",
      "member_selection",
      "attribute_read",
      "dot_expression",
      "function_call_expression",
      "identifier",
      "tuple",
      "tuple_sq",
      "paren_group",
      "type_specification",
      "constant",
  };

  std::string_view t(ts_node_type(n));
  if (t == "comment") {
    return;
  }
  if (t == "scope_statement") {
    auto inner_stmts = builder.add_child(Lnast_ntype::create_stmts());
    builder.push_stmts(inner_stmts);
    process_scope_statement(n, inner_stmts);
    builder.pop_stmts();
    return;
  }
  if (auto it = stmt_dispatch.find(t); it != stmt_dispatch.end()) {
    (this->*it->second)(n);
    return;
  }
  // if/match used as a statement: the value is discarded, so skip the
  // result tmp + per-arm placeholder `assign ___N = 0` that the expression
  // form emits.
  if (t == "if_expression") {
    (void)if_expr_to_node(n, /*need_result=*/false);
    return;
  }
  if (t == "match_expression") {
    (void)match_expr_to_node(n, /*need_result=*/false);
    return;
  }
  // `cassert(...)` / `assert(...)` are plain function calls in the new
  // grammar (the dedicated `assert_statement` rule is gone). Detect them at
  // statement position and emit a `cassert` LNAST node so the verifier
  // counts the assertion. Other function calls fall through to the generic
  // expr-as-statement path.
  if (t == "function_call_expression") {
    TSNode func = child_by_field(n, "function");
    if (!ts_node_is_null(func)) {
      auto fname = trim(get_text(func));
      if (fname == "cassert" || fname == "assert") {
        TSNode arg_tuple = child_by_field(n, "argument");
        if (!ts_node_is_null(arg_tuple)) {
          // The argument tuple is `(cond)` or `(cond, "msg")`. Lower the
          // first arg as the condition and the optional second arg as a
          // diagnostic message: a `cassert` node becomes
          //   cassert(<cond>)            — no message
          //   cassert(<cond>, <msg>)     — message read by the verifier when
          //                                the assertion is statically false
          // (see uPass_verifier::classify_statement). The message is any
          // expression that resolves to a comptime string (a string literal or
          // an interpolated string), lowered the same way as the condition.
          uint32_t   nnc       = ts_node_named_child_count(arg_tuple);
          TSNode     cond_node = nnc >= 1 ? ts_node_named_child(arg_tuple, 0) : TSNode{};
          Lnast_node cond_ref  = ts_node_is_null(cond_node) ? Lnast_node::create_const("true") : expr_to_node(cond_node);
          // Lower the message (if any) BEFORE creating the cassert node so the
          // helper statements an interpolated string emits land ahead of the
          // assertion in source order (otherwise the message ref would dangle).
          bool       have_msg  = nnc >= 2;
          Lnast_node msg_ref;
          if (have_msg) {
            msg_ref = expr_to_node(ts_node_named_child(arg_tuple, 1));
          }
          auto idx = builder.add_child(Lnast_ntype::create_cassert());
          attach_loc(idx, n);  // source span → verifier can point at this assertion
          lnast->add_child(idx, cond_ref);
          if (have_msg) {
            lnast->add_child(idx, msg_ref);
          }
          return;
        }
      }
    }
  }
  if (expr_stmt.contains(t)) {
    (void)expr_to_node(n);
    return;
  }
  std::print("prp2lnast: unhandled statement type `{}`\n", t);
}

void Prp2lnast::process_scope_statement(TSNode n, Lnast_nid /*target_stmts*/) {
  // Every body lowered through here is nested under a conditional / loop /
  // match arm / lambda (or an inline scope expression) — never the top-level
  // file scope (process_description walks the root directly). Bump
  // conditional_depth_ so a write inside cannot capture/update a file-scope
  // comptime int binding (see const_int_bindings_).
  Conditional_scope guard(&conditional_depth_);
  walk_statement_block(n);
}

// Resolved modifiers of a `var_or_let_or_reg` decl node.
struct Decl_mods {
  std::string_view kind;  // storage kind ("mut"/"const"/"reg"/"stage"/…; empty = none)
  bool             has_comptime{false};
  bool             has_pub{false};  // task 1m — `pub` export modifier
};
// Resolve a `var_or_let_or_reg` decl node's modifiers. Defined below with the
// declaration helpers (needs storage_kind_from_node_type); forward-declared
// here because process_gated_statement calls it earlier in the file.
static Decl_mods decode_decl(TSNode decl);

void Prp2lnast::process_gated_statement(TSNode stmt, TSNode gate) {
  // gate is a `when_unless_cond` node:
  //   when_unless_cond := ('when' | 'unless') condition:_expression
  //
  // Lower `stmt when c`   to `if c <stmt-children…>`
  //       `stmt unless c` to `if !c <stmt-children…>`
  //
  // Unlike a normal `if`, the gated body must NOT introduce a new scope:
  // `mut x = e when c` declares x in the surrounding scope when c is true,
  // and is a complete no-op when c is false (preserving any prior binding
  // of x). We encode this by giving the `if` direct stmt children — no
  // `stmts` wrapper — which signals to consumers (writer, constprop,
  // runner) that the body executes flat in the parent scope.
  // TODO(grammar_todo2.md #1): once the grammar splits when/unless into
  // distinct named nodes, dispatch on node kind here.
  std::string_view gate_kw_text = trim(get_text(gate));
  bool             is_unless    = gate_kw_text.starts_with("unless");

  TSNode cond_node = child_by_field(gate, "condition");

  // Evaluate the gate condition into the surrounding scope so any
  // helper stmts emitted by expr_to_node land before the `if`.
  Lnast_node cref;
  if (ts_node_is_null(cond_node)) {
    cref = Lnast_node::create_const("true");
  } else {
    cref = expr_to_node(cond_node);
  }
  if (is_unless) {
    auto idx     = builder.add_child(Lnast_ntype::create_log_not());
    auto neg_ref = builder.mint_tmp_ref();
    lnast->add_child(idx, neg_ref);
    lnast->add_child(idx, cref);
    cref = neg_ref;
  }

  // Decl-form assignment (`mut x = e when c`): the DECLARATION itself is not
  // conditional — hoist it into the surrounding scope with a `nil`
  // initializer so `x` is visible (and reads nil) when the gate is false;
  // only the value store stays under the gate. Previously the false-gate
  // `x == nil` read leaned on constprop's undeclared-reads-as-nil fold (now
  // removed: undeclared reads are a compile error). Restricted to a simple
  // identifier lvalue with a plain `=`; anything else keeps the flat path.
  if (std::string_view(ts_node_type(stmt)) == "assignment" && pending_overflow_kind.empty()) {
    TSNode decl_node = child_by_field(stmt, "decl");
    TSNode lv        = child_by_field(stmt, "lvalue");
    TSNode op        = child_by_field(stmt, "operator");
    TSNode op_inner  = ts_node_is_null(op) ? TSNode{} : ts_node_named_child(op, 0);
    bool   plain_eq  = ts_node_is_null(op_inner) || std::string_view(ts_node_type(op_inner)) == "assign";
    if (!ts_node_is_null(decl_node) && !ts_node_is_null(lv) && plain_eq) {
      std::string_view lvt(ts_node_type(lv));
      TSNode           id = lvt == "typed_identifier" ? child_by_field(lv, "identifier") : lv;
      TSNode           tc = lvt == "typed_identifier" ? child_by_field(lv, "type") : TSNode{};
      if (!ts_node_is_null(id) && std::string_view(ts_node_type(id)) == "identifier") {
        auto [kind, has_cpt, has_pub] = decode_decl(decl_node);
        (void)has_pub;  // gated-decl hoist doesn't act on `pub` (handled in process_lvalue_for_assign)
        // Already-declared name (`mut c2 = 100; mut c2 = 3 when false`): the
        // gated re-decl must be a complete no-op when the gate is false —
        // hoisting a nil store would clobber the prior binding. Keep the old
        // flat lowering for that case; hoist only a FRESH declaration.
        const bool already_visible
            = !kind.empty()
              && read_is_visible(
                  {std::string(trim(get_text(id))), id, builder.idx_stmts, lnast->get_last_child(builder.idx_stmts)});
        if (!kind.empty() && !already_visible) {
          Lnast_node ref  = identifier_to_node(id, /*for_lvalue=*/true);
          auto       aidx = builder.add_child(Lnast_ntype::create_attr_set());
          lnast->add_child(aidx, ref);
          lnast->add_child(aidx, Lnast_node::create_const("type"));
          lnast->add_child(aidx, Lnast_node::create_const(kind));
          attach_loc(aidx, id);
          if (has_cpt) {
            auto cidx = builder.add_child(Lnast_ntype::create_attr_set());
            lnast->add_child(cidx, ref);
            lnast->add_child(cidx, Lnast_node::create_const("comptime"));
            lnast->add_child(cidx, Lnast_node::create_const("true"));
          }
          if (!ts_node_is_null(tc)) {
            emit_type_spec(ref, tc);
          }
          auto sidx = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(sidx, ref);
          lnast->add_child(sidx, Lnast_node::create_const("nil"));
          attach_loc(sidx, id);

          // Gated value store, flat under the if (shares the cond eval).
          auto if_idx = builder.add_child(Lnast_ntype::create_if());
          lnast->add_child(if_idx, cref);
          builder.push_stmts(if_idx);
          TSNode     rv = child_by_field(stmt, "rvalue");
          Lnast_node rvalue_node;
          if (ts_node_is_null(rv)) {
            // Hidden rvalue (constant) — extract text after the operator.
            auto start  = ts_node_is_null(op) ? ts_node_end_byte(lv) : ts_node_end_byte(op);
            rvalue_node = constant_text_to_node(trim(text_between(start, ts_node_end_byte(stmt))));
          } else {
            rvalue_node = expr_to_node(rv);
          }
          (void)process_lvalue_for_assign(id, rvalue_node, TSNode{}, TSNode{});
          builder.pop_stmts();
          return;
        }
      }
    }
  }

  auto if_idx = builder.add_child(Lnast_ntype::create_if());
  lnast->add_child(if_idx, cref);

  // Flat form: emit the gated stmt's children directly under the if (no
  // `stmts` wrapper). For multi-effect statements (e.g. `mut x = e when c`
  // emits attr_set + assign), all effects live as siblings under the same
  // `if`, sharing one cond evaluation.
  // The gated statement is conditional, so a write inside it must not capture/
  // update a file-scope comptime int binding (see const_int_bindings_).
  builder.push_stmts(if_idx);
  {
    Conditional_scope guard(&conditional_depth_);
    process_statement(stmt);
  }
  builder.pop_stmts();
}

// ---------------- Declaration / Assignment ----------------

// Map a `var_or_let_or_reg` storage child's node kind to the storage-class
// keyword used downstream (attr_set "type" const|mut|reg|stage).
static std::string_view storage_kind_from_node_type(std::string_view t) {
  if (t == "const_decl") {
    return "const";
  }
  if (t == "mut_decl") {
    return "mut";
  }
  if (t == "reg_decl") {
    return "reg";
  }
  if (t == "stage_decl") {
    // Task 1r — `stage[N] lhs = rhs` (mod-only): lhs is rhs delivered N
    // cycles later. The decl-merge converts the kind to a `reg` declare
    // carrying a trailing stages(min,max) node (the upass/pipe shape).
    return "stage";
  }
  return {};
}

// Resolve (storage-kind, has-comptime, has-pub) for a `var_or_let_or_reg` decl
// node (struct + forward decl are up near process_gated_statement).
static Decl_mods decode_decl(TSNode decl) {
  if (ts_node_is_null(decl)) {
    return {};
  }
  TSNode           storage = ts_node_child_by_field_name(decl, "storage", 7);
  TSNode           cpt     = ts_node_child_by_field_name(decl, "comptime", 8);
  TSNode           pub     = ts_node_child_by_field_name(decl, "pub", 3);
  std::string_view kind    = ts_node_is_null(storage) ? std::string_view{} : storage_kind_from_node_type(ts_node_type(storage));
  return {kind, !ts_node_is_null(cpt), !ts_node_is_null(pub)};
}

std::vector<int64_t> Prp2lnast::extract_array_dims(TSNode type_cast_node) const {
  std::vector<int64_t> out;
  if (ts_node_is_null(type_cast_node)) {
    return out;
  }
  TSNode ty = ts_node_child_by_field_name(type_cast_node, "type", 4);
  if (ts_node_is_null(ty) || std::string_view(ts_node_type(ty)) != "array_type") {
    return out;
  }
  while (!ts_node_is_null(ty) && std::string_view(ts_node_type(ty)) == "array_type") {
    TSNode len = ts_node_child_by_field_name(ty, "length", 6);
    if (ts_node_is_null(len)) {
      return {};
    }
    TSNode idx = ts_node_child_by_field_name(len, "index", 5);
    // A `constant` literal OR a compile-time-resolvable `const`/`mut` binding
    // (`[sz]u8` where `sz` was statically known at the declaration point) — both
    // resolve through resolve_cycle_value (which consults const_int_bindings_,
    // tracking decl-time-known muts with the same UPDATE/ERASE rules). The
    // resolved length must still be > 0, as before.
    std::optional<int64_t> v = resolve_cycle_value(idx);
    if (!v || *v <= 0) {
      return {};
    }
    out.push_back(*v);
    ty = ts_node_child_by_field_name(ty, "base", 4);
  }
  return out;
}

// Task 1m — validate a `pub` value declaration (the LiveHD docs
// §1): only file-scope `const` (optionally `comptime`) declarations are
// exportable. `pub mut`/`pub reg` contradict the read-only export semantics.
void Prp2lnast::check_pub_value_decl(TSNode decl_node, std::string_view kind) {
  if (!builder.at_top_stmts() || !lambda_kind_stack_.empty() || conditional_depth_ > 0) {
    report_error(decl_node,
                 "pub-not-file-scope",
                 "syntax",
                 "`pub` is only valid on file-scope declarations",
                 "move the declaration to the file's top level");
  }
  if (kind != "const") {
    // `pub reg`/`pub stage` is a likely-common mistake: registers ARE shared
    // across modules, just not via `pub` (a read-only value/lambda export).
    // Point at the dedicated `regref` mechanism rather than the generic
    // "use pub const" hint. The grammar still parses `pub reg`; this
    // prp2lnast check is the single enforcement point (no grammar change).
    if (kind == "reg" || kind == "stage") {
      report_error(decl_node,
                   "pub-reg-use-regref",
                   "type",
                   std::format("cannot export `pub {}`: a register cannot be a `pub` export", kind),
                   "to share a register across modules use `regref`, not `pub reg`");
    }
    report_error(decl_node,
                 "pub-requires-const",
                 "type",
                 std::format("cannot export `pub {}`: exported state must be read-only", kind.empty() ? "fluid" : kind),
                 "use `pub const` for values, or export a `comb`/`mod`/`pipe`/`fluid` definition");
  }
}

void Prp2lnast::process_declaration_statement(TSNode n) {
  // declaration_statement: var_or_let_or_reg + lvalue (typed_identifier or list)
  auto decl_node   = child_by_field(n, "decl");
  auto lvalue_node = child_by_field(n, "lvalue");
  if (ts_node_is_null(lvalue_node)) {
    return;
  }

  // Decode storage class + comptime/pub modifiers from the decl's structured fields.
  auto [kind, has_comptime, has_pub] = decode_decl(decl_node);

  // Task 1r — a stage decl without an initializer has no value to deliver.
  if (kind == "stage") {
    report_error(decl_node,
                 "stage-needs-value",
                 "type",
                 "`stage[N]` requires an initializer: it delivers the right-hand side N cycles later",
                 "write `stage[N] name = value`");
  }

  // Task 1m — `pub` marks a file-scope const value declaration exportable.
  if (has_pub) {
    check_pub_value_decl(decl_node, kind);
  }

  // For each typed_identifier in the lvalue, emit: attr_set <ref> "type" kind
  auto emit_decl_attrs = [&](TSNode ti) {
    TSNode id = child_by_field(ti, "identifier");
    if (ts_node_is_null(id)) {
      return;
    }
    if (has_pub) {
      lnast->add_pub(trim(get_text(id)), "value");
    }
    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind));
      // Span → the decl-merge copies this onto the synthesized `declare` so
      // declaration-site diagnostics (semacheck redeclaration/shadowing) can
      // point at the `mut/const x` line. Mirrors the assignment-form site.
      attach_loc(idx, id);
    }
    if (has_comptime) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    TSNode tc = child_by_field(ti, "type");
    if (!ts_node_is_null(tc)) {
      emit_type_spec(ref, tc);
    }
  };

  std::string_view lvtype(ts_node_type(lvalue_node));
  if (lvtype == "typed_identifier") {
    emit_decl_attrs(lvalue_node);
  } else if (lvtype == "typed_identifier_list") {
    uint32_t nc = ts_node_named_child_count(lvalue_node);
    for (uint32_t i = 0; i < nc; i++) {
      emit_decl_attrs(ts_node_named_child(lvalue_node, i));
    }
  }
}

Lnast_node Prp2lnast::process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node,
                                                bool rhs_is_fcall, std::string_view rhs_fcall_name,
                                                std::string_view overflow_kind) {
  std::string_view lvt(ts_node_type(lvalue));
  if (lvt == "lvalue_list") {
    // Tuple lvalue: `(x0, x1, …) = rhs`. Each item is an `lvalue_item`,
    // which wraps either a `typed_identifier`, a `(_complex_identifier,
    // optional type_cast)`, or a `named_lvalue` (rename form). When the
    // item exposes a plain identifier name (typed_identifier or bare
    // identifier), we bind BY NAME — `tuple_get` keyed by the local
    // name, not by position. This matches the named-arg call rule:
    // `(c, b) = dox(a)` picks field `c` and field `b` from the return
    // bundle regardless of declaration order in the function's signature.
    // For complex lvalues (member_selection, bit_selection, etc.) we
    // fall back to positional binding since there's no name to use.
    uint32_t nnc = ts_node_named_child_count(lvalue);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < nnc; i++) {
      TSNode           item = ts_node_named_child(lvalue, i);
      std::string_view itt(ts_node_type(item));
      if (itt != "lvalue_item") {
        continue;
      }
      // Resolve the inner lvalue + per-item type_cast.
      TSNode inner;
      TSNode item_tc      = type_cast_node;  // default: outer type_cast (lvalue_list has none today)
      TSNode item_id_only = child_by_field(item, "identifier");
      TSNode item_tc_only = child_by_field(item, "type");
      if (!ts_node_is_null(item_id_only)) {
        // Option 2: bare _complex_identifier with optional type_cast.
        inner = item_id_only;
        if (!ts_node_is_null(item_tc_only)) {
          item_tc = item_tc_only;
        }
      } else {
        // Option 1: lvalue_item wraps a typed_identifier (or other lvalue
        // node) as its sole named child.
        inner = ts_node_named_child(item, 0);
      }
      if (ts_node_is_null(inner)) {
        ++pos;
        continue;
      }

      std::string_view inner_t(ts_node_type(inner));

      // Rename form: `(x = dox.b, y = dox.c) = dox(...)`. The `name` field
      // is the local identifier to bind, the `lvalue` field is either a
      // bare identifier (whole return tuple → name) or a `dot_expression`
      // whose leading identifier identifies the source (callee name for a
      // single fcall RHS, or positional slot for a tuple-of-fcalls RHS) —
      // the trailing path picks the deep field.
      if (inner_t == "named_lvalue") {
        TSNode name_node = child_by_field(inner, "name");
        TSNode src_node  = child_by_field(inner, "lvalue");
        if (!ts_node_is_null(name_node) && !ts_node_is_null(src_node)) {
          std::vector<std::string>    path_keys;
          std::string                 prefix;
          std::function<void(TSNode)> walk = [&](TSNode n) {
            std::string_view t(ts_node_type(n));
            if (t == "dot_expression") {
              TSNode item = child_by_field(n, "item");
              if (ts_node_is_null(item)) {
                uint32_t nc = ts_node_named_child_count(n);
                if (nc > 0) {
                  walk(ts_node_named_child(n, 0));
                }
                for (uint32_t k = 1; k < nc; k++) {
                  TSNode c = ts_node_named_child(n, k);
                  path_keys.emplace_back(trim(get_text(c)));
                }
              } else {
                walk(item);
                uint32_t nc = ts_node_named_child_count(n);
                for (uint32_t k = 0; k < nc; k++) {
                  TSNode c = ts_node_named_child(n, k);
                  if (ts_node_eq(c, item)) {
                    continue;
                  }
                  path_keys.emplace_back(trim(get_text(c)));
                }
              }
            } else if (t == "typed_identifier") {
              TSNode id = child_by_field(n, "identifier");
              if (!ts_node_is_null(id)) {
                prefix = std::string(trim(get_text(id)));
              }
            } else if (t == "identifier") {
              prefix = std::string(trim(get_text(n)));
            }
          };
          walk(src_node);
          if (rhs_is_fcall) {
            // Single fcall RHS: prefix must match the callee. Hard error
            // otherwise (cross-function reuse).
            if (prefix != rhs_fcall_name) {
              Pass::error("destructure rename prefix `{}` does not match call `{}` in `{}`",
                          prefix,
                          rhs_fcall_name,
                          get_text(inner));
            }
          }
          // Emit a chain of single-key tuple_gets — constprop chains tmp
          // through each step, while a single multi-key tuple_get against a
          // fcall return wasn't propagating through nested tuples. For
          // tuple-of-fcalls RHS, prepend a positional tuple_get on the
          // current lvalue_list slot so the prefix's path applies to the
          // matching call's return.
          Lnast_node tmp = rvalue;
          if (!rhs_is_fcall) {
            auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
            Lnast_node next   = builder.mint_tmp_ref();
            lnast->add_child(tg_idx, next);
            lnast->add_child(tg_idx, tmp);
            lnast->add_child(tg_idx, Lnast_node::create_const(std::to_string(pos)));
            tmp = next;
          }
          for (const auto& k : path_keys) {
            auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
            Lnast_node next   = builder.mint_tmp_ref();
            lnast->add_child(tg_idx, next);
            lnast->add_child(tg_idx, tmp);
            lnast->add_child(tg_idx, Lnast_node::create_const(k));
            tmp = next;
          }
          (void)process_lvalue_for_assign(name_node, tmp, decl_node, item_tc, false, {}, overflow_kind);
          ++pos;
          continue;
        }
      }

      // Decide between name-driven and positional tuple_get. When the RHS
      // is a function call, the destructure binds by return-field name —
      // so we key tuple_get by the local's identifier text. For tuple-
      // literal RHS (e.g. `(a, b) = (b+1, a)`) positional binding is
      // correct because the literal's slots are positional anyway.
      std::string key_text;
      bool        use_name_key = false;
      if (rhs_is_fcall) {
        if (inner_t == "identifier") {
          key_text     = std::string(trim(get_text(inner)));
          use_name_key = true;
        } else if (inner_t == "typed_identifier") {
          TSNode id = child_by_field(inner, "identifier");
          if (!ts_node_is_null(id)) {
            key_text     = std::string(trim(get_text(id)));
            use_name_key = true;
          }
        }
      }

      // tuple_get tmp, rvalue, <key>
      auto       tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      Lnast_node tmp    = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tmp);
      lnast->add_child(tg_idx, rvalue);
      if (use_name_key) {
        lnast->add_child(tg_idx, Lnast_node::create_const(key_text));
      } else {
        lnast->add_child(tg_idx, Lnast_node::create_const(std::to_string(pos)));
      }
      // Recurse: the per-position tmp is the new "rvalue" for the inner lvalue.
      // `decl_node` propagates so `mut (a, b) = …` declares both items.
      (void)process_lvalue_for_assign(inner, tmp, decl_node, item_tc, false, {}, overflow_kind);
      ++pos;
    }
    return rvalue;
  }
  if (lvt == "timed_identifier") {
    // Task 1r — `out@[4] = rhs` (with or without a stage decl): the lvalue is
    // the inner identifier; `@[4]` is a pure landing-cycle check (never a
    // flop), recorded as an inert timecheck statement.
    TSNode inner = child_by_field(lvalue, "identifier");
    if (!ts_node_is_null(inner)) {
      maybe_emit_timecheck(child_by_field(lvalue, "timing"), inner);
      return process_lvalue_for_assign(inner, rvalue, decl_node, type_cast_node, rhs_is_fcall, rhs_fcall_name, overflow_kind);
    }
  }
  if (lvt == "identifier" || lvt == "typed_identifier") {
    TSNode id = (lvt == "typed_identifier") ? child_by_field(lvalue, "identifier") : lvalue;
    TSNode tc = (lvt == "typed_identifier") ? child_by_field(lvalue, "type") : type_cast_node;
    if (ts_node_is_null(id)) {
      id = lvalue;
    }
    // Task 1r — `x:u8@[2] = rhs` body form: the timing rides the
    // typed_identifier (or its type_cast) and is a pure check, same as the
    // timed_identifier wrapper above.
    if (lvt == "typed_identifier") {
      TSNode timing = child_by_field(lvalue, "timing");
      if (ts_node_is_null(timing) && !ts_node_is_null(tc)) {
        timing = child_by_field(tc, "timing");
      }
      maybe_emit_timecheck(timing, id);
    }
    // Determine if this introduces a new declaration (decl_node present)
    bool             has_decl = !ts_node_is_null(decl_node);
    std::string_view kind_sv;
    bool             has_cpt = false;
    bool             has_pub = false;
    if (has_decl) {
      auto pr  = decode_decl(decl_node);
      kind_sv  = pr.kind;
      has_cpt  = pr.has_comptime;
      has_pub  = pr.has_pub;
    }

    // Task 1m — `pub const x = …` marks the value exportable (validated:
    // file-scope, const storage). Destructuring (`pub const (a,b) = t`)
    // records each item — decl_node propagates through the recursion.
    if (has_pub) {
      check_pub_value_decl(decl_node, kind_sv);
      lnast->add_pub(trim(get_text(id)), "value");
    }

    // Task 1r/1q — track compile-time-resolvable integer bindings so a later
    // `@[NAME]`, `stage[NAME]`, or `pipe[NAME]` timing slot can resolve them
    // (see resolve_cycle_value / const_int_bindings_). No-shadowing is already
    // enforced, so a fresh binding may safely overwrite an earlier same-name
    // one.
    //
    //   - `const NAME = <int literal>`  → record (immutable, always valid;
    //       no-shadowing + no-reassign make scoping unambiguous, so no depth
    //       gate — preserves the original Task-1r behavior).
    //   - `mut   NAME = <int literal>`  → record with declaration-time-capture:
    //       a later statement-level plain `NAME = <int literal>` UPDATEs it,
    //       and ANY other write ERASEs it. The
    //       erase covers a non-literal rhs, a compound op (the scalar `rvalue`
    //       arrives as a tmp ref, never a const, so is_const() is false), and
    //       any write nested in a conditional/loop/lambda body (which makes
    //       the value no longer statically known — conditional_depth_ > 0).
    //
    // Updating a mut is gated on conditional_depth_ == 0 so only an
    // unconditional, statement-level write is captured. A timing slot then
    // resolves the value known AT THE LAMBDA DECLARATION POINT.
    {
      std::string nm(trim(get_text(id)));
      auto        as_int = [&]() -> std::optional<int64_t> {
        if (!rvalue.is_const()) {
          return std::nullopt;
        }
        if (auto cst = Dlop::from_pyrope(rvalue.get_name()); cst && cst->is_integer() && cst->is_i()) {
          return cst->to_i();
        }
        return std::nullopt;
      };
      if (has_decl && kind_sv == "const") {
        if (auto v = as_int()) {
          const_int_bindings_[nm] = *v;
        }
      } else if ((has_decl && kind_sv == "mut") || (!has_decl)) {
        // `mut NAME = lit` (decl) or a plain re-assignment `NAME = lit`.
        // Record/UPDATE only on an unconditional integer-literal write;
        // anything else ERASEs the (possibly stale) capture.
        if (auto v = as_int(); v && conditional_depth_ == 0) {
          const_int_bindings_[nm] = *v;
        } else {
          const_int_bindings_.erase(nm);
        }
      } else if (has_decl) {
        // reg / stage (or any other) decl of NAME: not a comptime int binding.
        const_int_bindings_.erase(nm);
      }
    }

    Lnast_node ref = identifier_to_node(id, /*for_lvalue=*/true);
    Lnast_nid  reg_decl_head{};  // 2d-reg — set for `reg` decls; the init value rides this cluster head
    if (has_decl) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("type"));
      lnast->add_child(idx, Lnast_node::create_const(kind_sv));
      // Span → the decl-merge copies this onto the synthesized `declare` so
      // declaration-site diagnostics can point at the `mut/const x` line.
      attach_loc(idx, id);
      // Task 1r — a stage decl rides its (min,max) as a trailing stages node
      // on the cluster head; the decl-merge moves it onto the synthesized
      // declare (mode "reg") so tolg lowers it as a depth-N pipeline Flop.
      if (kind_sv == "stage") {
        if (lambda_kind_stack_.empty() || lambda_kind_stack_.back() != "mod") {
          report_error(decl_node,
                       "stage-not-in-mod",
                       "type",
                       "`stage[N]` is only valid inside `mod` bodies",
                       lambda_kind_stack_.empty() || lambda_kind_stack_.back() == "comb"
                           ? "comb logic is all at cycle 0 — use a plain assignment"
                           : "pipe bodies use stage inference — use an explicit `reg` stage register instead");
        }
        TSNode storage          = ts_node_child_by_field_name(decl_node, "storage", 7);
        auto [min_txt, max_txt] = parse_stage_slot(storage);
        auto st                 = lnast->add_child(idx, Lnast_ntype::create_stages());
        lnast->add_child(st, Lnast_node::create_const(min_txt));
        lnast->add_child(st, Lnast_node::create_const(max_txt));
      }
      // 2d-reg — a `comb` may not declare a `reg` (06-functions.md: comb
      // logic is all at cycle 0 and holds no state; the `::[debug]` exception
      // is out of scope). pipe/mod bodies and file scope may.
      if (kind_sv == "reg") {
        if (!lambda_kind_stack_.empty() && lambda_kind_stack_.back() == "comb") {
          report_error(decl_node,
                       "reg-in-comb",
                       "type",
                       "a `comb` may not declare a `reg` (combinational logic holds no state)",
                       "move the register into a `pipe`/`mod` body, or pass the value through an output");
        }
        reg_decl_head = idx;
      }
    }
    if (has_cpt) {
      auto idx = builder.add_child(Lnast_ntype::create_attr_set());
      lnast->add_child(idx, ref);
      lnast->add_child(idx, Lnast_node::create_const("comptime"));
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    if (!ts_node_is_null(tc)) {
      // A type cast on the lvalue is only legal when it (a) declares the
      // variable (`var c:u4 = …`, decl_node present) or (b) is the cast
      // target of a `wrap`/`sat` write (`wrap c:u4 = …`). A bare
      // `c:u4 = …` re-assignment would otherwise emit a stmts-level
      // `type_spec` that silently re-types an existing variable — reject it
      // and point the user at wrap/sat for an intentional downcast.
      if (!has_decl && overflow_kind.empty()) {
        std::string name(trim(get_text(id)));
        report_error(tc,
                     "assign-retype",
                     "type",
                     std::format("cannot change the type of `{}` in an assignment", name),
                     std::format("annotate the type at the declaration, or use `wrap`/`sat` to downcast "
                                 "(e.g. `wrap {}{} = …`)",
                                 name,
                                 std::string_view(get_text(tc))));
      }
      // A literal initializer must match the declared scalar kind (same check as
      // a function parameter `b:bool = 3` — see emit_arg_assign / check_decl_init_kind).
      check_decl_init_kind(trim(get_text(id)), rvalue, child_by_field(tc, "type"), tc);
      emit_type_spec(ref, tc);
    }

    // Emit assign. Task 1t — a `wrap`/`sat` write lowers the value through a
    // `wrap|sat(v=<value>, type=<lhs>)` library call first; the call result
    // then binds the lvalue. attributes narrows the value to the lhs's declared
    // type (read via the `type=` arg), bitwidth exempts the lhs from the
    // overflow check, and codegen emits get_mask / mux+get_mask. The type_spec
    // above runs first so the lhs's type is known when the call is processed.
    Lnast_node store_value = rvalue;
    if (!overflow_kind.empty()) {
      Lnast_node wrapped = builder.mint_tmp_ref();
      auto       fc      = builder.add_child(Lnast_ntype::create_func_call());
      lnast->add_child(fc, wrapped);                                // dst tmp
      lnast->add_child(fc, Lnast_node::create_ref(overflow_kind));  // callee: wrap | sat
      auto va = lnast->add_child(fc, Lnast_ntype::create_store());  // v = <value>
      lnast->add_child(va, Lnast_node::create_ref("v"));
      lnast->add_child(va, rvalue);
      auto ta = lnast->add_child(fc, Lnast_ntype::create_store());  // type = <lhs> (its declared type)
      lnast->add_child(ta, Lnast_node::create_ref("type"));
      lnast->add_child(ta, ref);
      store_value = wrapped;
    }
    if (!reg_decl_head.is_invalid()) {
      // 2d-reg / 3j declare-folding — a `reg` declaration's initializer is its
      // power-on/reset value, NOT a din write: ride it on the decl cluster
      // head so the decl-merge folds it into the declare's optional 4th
      // [value] child. Only genuine re-assignments emit a `store` (every
      // store to a reg-declared name is a next-state write).
      lnast->add_child(reg_decl_head, store_value);
    } else {
      auto aidx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(aidx, ref);
      lnast->add_child(aidx, store_value);
      // Span → bitwidth's "does not fit" overflow diagnostic can point at the
      // write site (the declare/store loc-carry chain: here, prp_copy_one_node,
      // and the runner's emit_push).
      attach_loc(aidx, id);
    }

    // Phase 8 typesystem: a bare `true`/`false` literal on the rvalue
    // implies a `:bool` type. Inject a synthetic type_spec so the
    // attribute upass records the boolean envelope without requiring
    // the user to write `:bool` explicitly. Only fires when no explicit
    // type cast was given (it would dominate).
    if (ts_node_is_null(tc) && rvalue.is_const()) {
      auto rv_text = rvalue.get_name();
      if (rv_text == "true" || rv_text == "false") {
        auto ts_idx = builder.add_child(Lnast_ntype::create_type_spec());
        lnast->add_child(ts_idx, ref);
        lnast->add_child(ts_idx, Lnast_ntype::create_prim_type_bool());
      }
    }

    // Comptime vector/matrix pre-fill: `mut data:[N][M]T = scalar` populates
    // every flat slot so later element reads (`data[i][j]`) fold even though
    // each `tuple_set data i j …` would otherwise erase the bare scalar
    // trivial that `data = scalar` first stored.
    if (has_decl && !ts_node_is_null(tc) && rvalue.is_const()) {
      std::vector<int64_t> dims = extract_array_dims(tc);
      if (!dims.empty()) {
        std::string_view rv_text = rvalue.get_name();
        if (rv_text != "nil") {
          std::vector<int64_t> idx_v(dims.size(), 0);
          while (true) {
            auto ts_idx = builder.add_child(Lnast_ntype::create_store());
            lnast->add_child(ts_idx, ref);
            for (auto d : idx_v) {
              lnast->add_child(ts_idx, Lnast_node::create_const(std::to_string(d)));
            }
            lnast->add_child(ts_idx, Lnast_node::create_const(rv_text));
            int k = static_cast<int>(idx_v.size()) - 1;
            while (k >= 0) {
              if (++idx_v[k] < dims[k]) {
                break;
              }
              idx_v[k] = 0;
              --k;
            }
            if (k < 0) {
              break;
            }
          }
        }
      }
    }
    return ref;
  }
  if (lvt == "bit_selection") {
    // `b#[range] = rhs` lowers to a read-modify-write:
    //   - `set_mask new_word base mask rhs` synthesizes the updated word
    //     (pure-functional; matches LGraph Dlop set_mask_op shape).
    //   - The write-back is delegated to a recursive process_lvalue_for_assign
    //     on the argument lvalue, so nested forms like `a.b#[range] = rhs`
    //     reuse the member_selection arm and lower to a proper `tuple_set`.
    // No syntax for declaring/typing through a bit-range — reject decl/type
    // alongside the member_selection arm below.
    if (!ts_node_is_null(decl_node)) {
      report_error(lvalue,
                   "bit-range-decl",
                   "name",
                   std::format("cannot declare a bit-range lvalue: `{}`", get_text(lvalue)),
                   "declare the base variable first, then write the bit range");
    }
    if (!ts_node_is_null(type_cast_node)) {
      report_error(lvalue,
                   "bit-range-type",
                   "type",
                   std::format("cannot type-annotate a bit-range lvalue: `{}`", get_text(lvalue)),
                   "the base carries the type; annotate it at the base's declaration");
    }

    TSNode arg_n = child_by_field(lvalue, "argument");
    if (ts_node_is_null(arg_n)) {
      arg_n = ts_node_named_child(lvalue, 0);
    }
    TSNode sel_node{};
    for (uint32_t i = 0; i < child_count(lvalue); i++) {
      TSNode           c = child(lvalue, i);
      std::string_view ct(ts_node_type(c));
      if (ct == "select") {
        sel_node = c;
        break;
      }
    }

    // Read the argument's current value via the expression path. For a plain
    // identifier this is just a ref; for `a.b` this emits a tuple_get into a
    // tmp, which is exactly the old-value input we want feeding set_mask.
    Lnast_node cur_val  = expr_to_node(arg_n);
    Lnast_node mask_ref = compute_bit_mask_ref(sel_node);

    // set_mask new_word cur_val mask_ref rvalue
    auto       sm_idx   = builder.add_child(Lnast_ntype::create_set_mask());
    Lnast_node new_word = builder.mint_tmp_ref();
    lnast->add_child(sm_idx, new_word);
    lnast->add_child(sm_idx, cur_val);
    lnast->add_child(sm_idx, mask_ref);
    lnast->add_child(sm_idx, rvalue);

    // Write the updated word back to the argument lvalue. Recursing keeps the
    // member_selection / dot_expression handling in one place; passing null
    // decl/type because they were already rejected (and don't apply to the
    // inner lvalue either — it's a re-bind, not a declaration).
    return process_lvalue_for_assign(arg_n, new_word, TSNode{}, TSNode{}, false, {});
  }
  if (lvt == "member_selection" || lvt == "dot_expression") {
    // Path-rooted lvalues can't carry a declaration or type cast: Pyrope has
    // no syntax for "declare new field through a path". Reject so we don't
    // silently drop the decl/type and emit a misleading `tuple_set`.
    if (!ts_node_is_null(decl_node)) {
      report_error(lvalue,
                   "tuple-path-decl",
                   "name",
                   std::format("cannot declare a tuple-path lvalue: `{}`", get_text(lvalue)),
                   "declare the base tuple instead");
    }
    if (!ts_node_is_null(type_cast_node)) {
      report_error(lvalue,
                   "tuple-path-type",
                   "type",
                   std::format("cannot type-annotate a tuple-path lvalue: `{}`", get_text(lvalue)),
                   "annotate the field at the base's declaration");
    }
    // Extract a deep path so `t.b[0] = …` lowers to `tuple_set t b 0 …`
    // (one tuple_set rooted on the actual variable) instead of going
    // through `tuple_get tmp t b; tuple_set tmp 0 …` — the tmp would not
    // propagate the update back to t. The walker handles arbitrarily
    // nested member_selection / dot_expression chains.
    std::vector<Lnast_node>     path;
    Lnast_node                  root;
    bool                        have_root = false;
    std::function<void(TSNode)> collect   = [&](TSNode n) {
      std::string_view t(ts_node_type(n));
      if (t == "dot_expression") {
        TSNode item = child_by_field(n, "item");
        if (ts_node_is_null(item)) {
          uint32_t nc = ts_node_named_child_count(n);
          if (nc > 0) {
            collect(ts_node_named_child(n, 0));
          }
          for (uint32_t i = 1; i < nc; i++) {
            TSNode           c = ts_node_named_child(n, i);
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(trim(get_text(c))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        } else {
          collect(item);
          uint32_t nc = ts_node_named_child_count(n);
          for (uint32_t i = 0; i < nc; i++) {
            TSNode c = ts_node_named_child(n, i);
            if (ts_node_eq(c, item)) {
              continue;
            }
            std::string_view ct(ts_node_type(c));
            if (ct == "identifier") {
              path.push_back(Lnast_node::create_const(trim(get_text(c))));
            } else {
              path.push_back(expr_to_node(c));
            }
          }
        }
      } else if (t == "member_selection") {
        TSNode arg = child_by_field(n, "argument");
        if (!ts_node_is_null(arg)) {
          collect(arg);
        }
        uint32_t nnc = ts_node_named_child_count(n);
        for (uint32_t i = 0; i < nnc; i++) {
          TSNode c = ts_node_named_child(n, i);
          if (!ts_node_is_null(arg) && ts_node_eq(c, arg)) {
            continue;
          }
          // Each `select` carries a single index expression, or an
          // open/from-zero range. Only the index form is supported as an
          // lvalue path today.
          TSNode idx_node = child_by_field(c, "index");
          if (!ts_node_is_null(idx_node)) {
            path.push_back(expr_to_node(idx_node));
          }
        }
      } else if (t == "identifier") {
        root      = identifier_to_node(n, /*for_lvalue=*/true);
        have_root = true;
      } else {
        root      = expr_to_node(n);
        have_root = true;
      }
    };
    collect(lvalue);
    if (have_root) {
      auto idx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(idx, root);
      for (auto& p : path) {
        lnast->add_child(idx, p);
      }
      lnast->add_child(idx, rvalue);
      return root;
    }
  }
  // Fallback: treat as a single assign using the text span.
  auto       name = trim(get_text(lvalue));
  Lnast_node ref  = Lnast_node::create_ref(name);
  auto       aidx = builder.add_child(Lnast_ntype::create_store());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, rvalue);
  return ref;
}

void Prp2lnast::process_assignment(TSNode n) {
  TSNode decl = child_by_field(n, "decl");
  TSNode lv   = child_by_field(n, "lvalue");
  TSNode op   = child_by_field(n, "operator");
  TSNode rv   = child_by_field(n, "rvalue");
  TSNode tc   = child_by_field(n, "type");  // outer type_cast on complex lvalue

  // ── Reject an unparenthesized multi-element tuple on either side ──────────
  //
  // A multi-target assignment requires explicit parentheses around the tuple:
  // `(a, b) = (2, 3)` is legal, but `mut a,b = (2,3)` and `(a,b) = 2,3` are not.
  // Without the parens the grammar can only consume the FIRST element as the
  // lvalue/rvalue and parks the remaining comma-separated element(s) in an
  // ERROR node that is a direct child of the `assignment` — which the rest of
  // this function would otherwise silently drop (e.g. lowering `mut a,b=(2,3)`
  // to just `a = (2,3)`, dropping `b`).
  //
  // The pyrope grammar also emits ERROR nodes for benign quirks (a `uint`/`sint`
  // type inside a `type_cast`, or the stray `:` in `const p::[attr] :u8`); those
  // carry NO comma, so a direct `,` child of the ERROR is the unambiguous
  // tell-tale of a dropped tuple slot. See prp2lnast's tree-sitter notes /
  // check_parse_errors (ERROR nodes are otherwise tolerated).
  {
    uint32_t nc = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; i++) {
      TSNode c = ts_node_child(n, i);
      if (std::string_view(ts_node_type(c)) != "ERROR") {
        continue;
      }
      bool     has_comma = false;
      uint32_t ec        = ts_node_child_count(c);
      for (uint32_t j = 0; j < ec; j++) {
        if (std::string_view(ts_node_type(ts_node_child(c, j))) == ",") {
          has_comma = true;
          break;
        }
      }
      if (!has_comma) {
        continue;  // benign grammar quirk (uint/sint type_cast, `::[attr] :T`), not a dropped tuple
      }
      // lhs vs rhs: the dropped element sits before the `=` for an
      // unparenthesized lhs tuple, after it for an unparenthesized rhs tuple.
      const bool before_op = ts_node_is_null(op) || ts_node_start_byte(c) < ts_node_start_byte(op);
      report_error(c,
                   "tuple-requires-parens",
                   "syntax",
                   std::format("a multi-element tuple on the {} of an assignment requires explicit parentheses",
                               before_op ? "left-hand-side" : "right-hand-side"),
                   "wrap the targets in parentheses, e.g. `(a, b) = (2, 3)`");
    }
  }

  // The assignment_operator wrapper has a single named child of the aliased
  // op kind (`assign`, `assign_add`, …). Drives the compound-op lowering, no
  // text compares.
  std::string_view op_kind;
  if (!ts_node_is_null(op)) {
    TSNode op_inner = ts_node_named_child(op, 0);
    op_kind         = ts_node_is_null(op_inner) ? std::string_view{} : std::string_view(ts_node_type(op_inner));
  } else {
    op_kind = "assign";  // assignment without an explicit operator defaults to plain `=`
  }

  // The grammar attaches a statement-level `wrap`/`sat` prefix as the
  // `overflow` field of the enclosing _statement. Passed to
  // process_lvalue_for_assign, which lowers the scalar write through a
  // `wrap|sat(v=<value>, type=<lhs>)` library call (Task 1t). Consumed once
  // per assignment.
  const std::string_view overflow_kind = pending_overflow_kind;
  pending_overflow_kind                = {};

  // Get rvalue node or fallback text for hidden tokens
  Lnast_node rvalue_node;
  if (ts_node_is_null(rv)) {
    // Hidden rvalue (constant). Extract text after operator.
    auto op_end  = ts_node_end_byte(op);
    auto par_end = ts_node_end_byte(n);
    auto text    = trim(text_between(op_end, par_end));
    rvalue_node  = constant_text_to_node(text);
  } else {
    rvalue_node = expr_to_node(rv);
  }

  // Compound assignment: lower `a OP= b` to `OP tmp a b; assign a tmp`.
  // The aliased kind on the assignment_operator child names the underlying op.
  if (op_kind != "assign") {
    using Factory                                                                = Lnast_ntype::Lnast_ntype_int (*)();
    static const absl::flat_hash_map<std::string_view, Factory> compound_factory = {
        {         "assign_add",         [] { return Lnast_ntype::create_plus(); }},
        {         "assign_sub",        [] { return Lnast_ntype::create_minus(); }},
        {         "assign_mul",         [] { return Lnast_ntype::create_mult(); }},
        {         "assign_div",          [] { return Lnast_ntype::create_div(); }},
        {      "assign_bit_or",       [] { return Lnast_ntype::create_bit_or(); }},
        {     "assign_bit_and",      [] { return Lnast_ntype::create_bit_and(); }},
        {     "assign_bit_xor",      [] { return Lnast_ntype::create_bit_xor(); }},
        {         "assign_shl",          [] { return Lnast_ntype::create_shl(); }},
        {         "assign_sra",          [] { return Lnast_ntype::create_sra(); }},
        {"assign_tuple_concat", [] { return Lnast_ntype::create_tuple_concat(); }},
        {      "assign_log_or",       [] { return Lnast_ntype::create_log_or(); }},
        {     "assign_log_and",      [] { return Lnast_ntype::create_log_and(); }},
    };
    auto fit              = compound_factory.find(op_kind);
    auto compound_op_type = (fit == compound_factory.end()) ? Lnast_ntype::create_invalid() : fit->second();
    if (Lnast_ntype::is_invalid(compound_op_type)) {
      std::print("prp2lnast: unhandled compound op `{}`\n", op_kind);
      return;
    }
    Lnast_node left_ref = expr_to_node(lv);
    Lnast_node result   = builder.mint_tmp_ref();
    auto       idx      = builder.add_child(compound_op_type);
    lnast->add_child(idx, result);
    lnast->add_child(idx, left_ref);
    lnast->add_child(idx, rvalue_node);
    (void)process_lvalue_for_assign(lv, result, decl, tc, false, {}, overflow_kind);
    return;
  }

  const bool  rhs_is_fcall = !ts_node_is_null(rv) && std::string_view(ts_node_type(rv)) == "function_call_expression";
  std::string rhs_fcall_name;
  if (rhs_is_fcall) {
    TSNode fn = child_by_field(rv, "function");
    if (!ts_node_is_null(fn)) {
      rhs_fcall_name = std::string(trim(get_text(fn)));
    }
  }
  (void)process_lvalue_for_assign(lv, rvalue_node, decl, tc, rhs_is_fcall, rhs_fcall_name, overflow_kind);
}

// ---------------- Assert ----------------

void Prp2lnast::process_assert_statement(TSNode n) {
  TSNode cond = child_by_field(n, "condition");
  if (ts_node_is_null(cond)) {
    // Hidden condition — extract text after the `cassert`/`assert` keyword.
    // Find the 'condition' start position: the first non-'always'/assert token.
    uint32_t start = 0;
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c   = child(n, i);
      std::string_view txt = get_text(c);
      if (txt == "always" || txt == "assert" || txt == "cassert") {
        start = ts_node_end_byte(c);
      } else {
        break;
      }
    }
    auto txt = trim(text_between(start, ts_node_end_byte(n)));
    auto idx = builder.add_child(Lnast_ntype::create_cassert());
    attach_loc(idx, n);
    lnast->add_child(idx, constant_text_to_node(txt));
    return;
  }
  Lnast_node cond_ref = expr_to_node(cond);
  auto       idx      = builder.add_child(Lnast_ntype::create_cassert());
  attach_loc(idx, n);
  lnast->add_child(idx, cond_ref);
}

// ---------------- Control Flow ----------------

void Prp2lnast::process_while_statement(TSNode n) {
  TSNode cond = child_by_field(n, "condition");
  TSNode code = child_by_field(n, "code");
  TSNode init = child_by_field(n, "init");

  if (!ts_node_is_null(init)) {
    // Init stmt_list hoists outside
    uint32_t nnc = ts_node_named_child_count(init);
    for (uint32_t i = 0; i < nnc; i++) {
      process_statement(ts_node_named_child(init, i));
    }
  }

  Lnast_node cond_ref;
  if (ts_node_is_null(cond)) {
    cond_ref = Lnast_node::create_const("true");
  } else {
    cond_ref = expr_to_node(cond);
  }
  auto while_idx = builder.add_child(Lnast_ntype::create_while());
  attach_loc(while_idx, n);  // span → upass/typecheck cond-not-bool can point here
  lnast->add_child(while_idx, cond_ref);
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);

  if (ts_node_is_null(cond)) {
    // `while { … }` (no condition, like `loop {}`): the body must `break`.
    if (!ts_node_is_null(code)) {
      process_scope_statement(code, body_idx);
    }
  } else {
    // Pyrope loops are comptime-only — a `while` must fully unroll (codegen has
    // no runtime-loop lowering). The runner only unrolls the infinite form
    // `while true { … break }`, re-walking the body each iteration. So lower
    //   while cond { body }  ≡  while cond { if cond { body } else { break } }
    // The condition re-lowered INSIDE the body is re-evaluated every iteration
    // (the unroll re-walks the body), and the synthesized `break` terminates
    // the loop when it turns false. The outer `while` keeps the real condition
    // so typecheck still bool-checks it and a statically-false condition drops
    // the loop (zero iterations) without entering the body.
    Lnast_node inner_cond = expr_to_node(cond);  // emitted into the body → recomputed per iteration
    auto       if_idx     = builder.add_child(Lnast_ntype::create_if());
    attach_loc(if_idx, cond);
    lnast->add_child(if_idx, inner_cond);

    auto then_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(then_idx);
    if (!ts_node_is_null(code)) {
      process_scope_statement(code, then_idx);
    }
    builder.pop_stmts();

    auto else_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(else_idx);
    auto brk = builder.add_child(Lnast_ntype::create_func_break());
    lnast->add_child(brk, builder.mint_tmp_ref());
    builder.pop_stmts();
  }

  builder.pop_stmts();
}

void Prp2lnast::process_for_statement(TSNode n) {
  // Pyrope `for` is comptime-only: every loop must fully unroll (there is no
  // runtime-loop lowering). prp2lnast is purely structural — it emits a raw
  // `for` LNAST node for EVERY loop and the upass runner does all expansion at
  // comptime (uPass_runner::unroll_for), beside the comb inliner. Keeping the
  // unroll in one place (the runner) avoids the parse-time/runner duplication
  // and lets `for x in args` / `for x in <runtime tuple>` unroll where the
  // iterable is bound. (task 2u)
  //
  // for-node layout — producer/consumer contract with uPass_runner::unroll_for:
  //   for( value_ref, iterable_ref, stmts(body), const(mode) [, idx_ref [, key_ref]] )
  //   - value_ref     : the value iteration variable.
  //   - iterable_ref  : a ref the runner resolves via constprop — a `range` tmp
  //     (try_range), a tuple name/tmp (try_tuple_shape), or a var-arg. Bare
  //     `NAME` / `ref NAME` emit a direct ref so a `ref` write-back targets the
  //     source tuple; ranges / inline tuples emit a tmp via expr_to_node (which
  //     lowers the range/tuple statements as preceding siblings).
  //   - mode          : "ref" for mutable-element iteration `for i in ref d`
  //     (the runner writes each (possibly mutated) value back into the slot
  //     after the body), else "val".
  //   - idx_ref/key_ref : the optional position / key bindings of
  //     `for (value, idx, key) in t` (idx present with 2+ binds, key with 3).
  //
  // read_is_visible already treats a `for` node as declaring its iterator, so no
  // visibility change is needed here.

  TSNode code = child_by_field(n, "code");
  TSNode data = child_by_field(n, "data");
  if (ts_node_is_null(data)) {
    // ref_identifier form — find and use it as data
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c = child(n, i);
      std::string_view t(ts_node_type(c));
      if (t == "ref_identifier") {
        data = c;
        break;
      }
    }
  }
  // Find binding: first typed_identifier or typed_identifier_list under arg_list
  TSNode binding = ts_node_child_by_field_name(n, "index", 5);
  if (ts_node_is_null(binding)) {
    for (uint32_t i = 0; i < child_count(n); i++) {
      TSNode           c = child(n, i);
      std::string_view t(ts_node_type(c));
      if (t == "typed_identifier") {
        binding = c;
        break;
      }
    }
  }

  // Collect 1..3 binding ids. `for i in …` parses as a single typed_identifier;
  // `for (e, idx, key) in …` parses as typed_identifier_list with 1–3 items
  // (value, position, key).
  std::vector<TSNode> bind_ids;
  if (!ts_node_is_null(binding)) {
    std::string_view bt(ts_node_type(binding));
    if (bt == "typed_identifier") {
      TSNode id = child_by_field(binding, "identifier");
      if (!ts_node_is_null(id)) {
        bind_ids.push_back(id);
      }
    } else if (bt == "typed_identifier_list") {
      uint32_t nnc = ts_node_named_child_count(binding);
      for (uint32_t i = 0; i < nnc && bind_ids.size() < 3; i++) {
        TSNode           item = ts_node_named_child(binding, i);
        std::string_view it(ts_node_type(item));
        if (it == "typed_identifier") {
          TSNode id = child_by_field(item, "identifier");
          if (!ts_node_is_null(id)) {
            bind_ids.push_back(id);
          }
        }
      }
    }
  }

  // Is the iterable a range expression (`a..b`, `a..<b`, `a..+n`)? Detected
  // syntactically (independent of whether the bounds resolve at parse time) so a
  // parenthesized range `(0..<n)` — which parses as a single-item `tuple` — is
  // unwrapped to the inner range expr; otherwise expr_to_node would build a
  // 1-tuple instead of a range tmp.
  auto is_range_data = [&](TSNode d) -> bool {
    if (ts_node_is_null(d)) {
      return false;
    }
    TSNode           inner = d;
    std::string_view dt(ts_node_type(d));
    if (dt == "expression_list" || dt == "tuple") {
      uint32_t count = 0;
      TSNode   first{};
      for (uint32_t i = 0; i < child_count(d); i++) {
        const char* fn = ts_node_field_name_for_child(d, i);
        if (fn && std::string_view(fn) == "item") {
          if (count == 0) {
            first = child(d, i);
          }
          ++count;
        }
      }
      if (count != 1) {
        return false;
      }
      inner = first;
      dt    = std::string_view(ts_node_type(inner));
    }
    if (dt != "expression_item") {
      return false;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(inner); i++) {
      TSNode c = ts_node_named_child(inner, i);
      if (std::string_view(ts_node_type(c)) != "binary_other_op") {
        continue;
      }
      TSNode op_inner = ts_node_named_child(c, 0);
      if (ts_node_is_null(op_inner)) {
        continue;
      }
      std::string_view k(ts_node_type(op_inner));
      if (k == "op_range_inclusive" || k == "op_range_exclusive" || k == "op_range_count") {
        return true;
      }
    }
    return false;
  };

  if (bind_ids.empty() || ts_node_is_null(code) || ts_node_is_null(data)) {
    return;  // malformed `for` — nothing to lower
  }

  // Bindings first (value, then optional idx/key) so identifier_to_node records
  // them as for-declared before the body is walked.
  Lnast_node value_ref = identifier_to_node(bind_ids[0], true);
  const bool have_idx  = bind_ids.size() >= 2;
  const bool have_key  = bind_ids.size() >= 3;
  Lnast_node idx_ref   = have_idx ? identifier_to_node(bind_ids[1], true) : Lnast_node::create_invalid();
  Lnast_node key_ref   = have_key ? identifier_to_node(bind_ids[2], true) : Lnast_node::create_invalid();

  // Resolve the iterable into a single ref child + detect `ref` (write-back)
  // iteration. A bare `NAME` / `ref NAME` (possibly wrapped in a single-child
  // expression_list/expression_item) emits a direct ref so the runner's
  // write-back targets the source tuple; everything else (a range, an inline
  // tuple `(10,20,30)`) is lowered by expr_to_node, which emits the range/tuple
  // statements as siblings *before* the for node and returns the tmp ref.
  bool       is_ref = false;
  Lnast_node iterable_ref;
  {
    TSNode src = data;
    if (std::string_view(ts_node_type(src)) == "ref_identifier") {
      is_ref      = true;
      uint32_t nc = ts_node_named_child_count(src);
      if (nc >= 1) {
        src = ts_node_named_child(src, 0);
      }
    }
    while (true) {
      std::string_view st(ts_node_type(src));
      if (st == "ref_identifier" || st == "identifier") {
        break;
      }
      if ((st == "expression_list" || st == "expression_item") && ts_node_named_child_count(src) == 1) {
        src = ts_node_named_child(src, 0);
        continue;
      }
      break;
    }
    std::string_view st(ts_node_type(src));
    if (st == "identifier" || st == "ref_identifier") {
      iterable_ref = Lnast_node::create_ref(std::string(trim(get_text(src))));
    } else {
      // Range / inline-tuple / other expression. Unwrap a parenthesized range
      // (single-item `tuple`) to the inner range expr so expr_to_node lowers a
      // range tmp rather than a 1-tuple.
      TSNode iter_src = data;
      if (is_range_data(data)) {
        std::string_view rt(ts_node_type(iter_src));
        if (rt == "tuple" || rt == "expression_list") {
          for (uint32_t i = 0; i < child_count(iter_src); i++) {
            const char* fn = ts_node_field_name_for_child(iter_src, i);
            if (fn && std::string_view(fn) == "item") {
              iter_src = child(iter_src, i);
              break;
            }
          }
        }
      }
      iterable_ref = expr_to_node(iter_src);  // emits range/tuple stmts before the for; returns the tmp ref
    }
  }

  auto for_idx = builder.add_child(Lnast_ntype::create_for());
  attach_loc(for_idx, n);  // span at the `for` → shadow/non-comptime-iterable diagnostics locate here
  lnast->add_child(for_idx, value_ref);
  lnast->add_child(for_idx, iterable_ref);
  auto body_idx = lnast->add_child(for_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  process_scope_statement(code, body_idx);
  builder.pop_stmts();

  // Trailing metadata (after the body): iteration mode + optional idx/key binds.
  lnast->add_child(for_idx, Lnast_node::create_const(is_ref ? "ref" : "val"));
  if (have_idx) {
    lnast->add_child(for_idx, idx_ref);
  }
  if (have_key) {
    lnast->add_child(for_idx, key_ref);
  }
}

void Prp2lnast::process_loop_statement(TSNode n) {
  // loop { body } -> while true body
  TSNode code      = child_by_field(n, "code");
  auto   while_idx = builder.add_child(Lnast_ntype::create_while());
  lnast->add_child(while_idx, Lnast_node::create_const("true"));
  auto body_idx = lnast->add_child(while_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();
}

void Prp2lnast::process_control_statement(TSNode n) {
  // control_statement now wraps a single named child of break_statement,
  // continue_statement, or return_statement — dispatch on its node kind.
  TSNode inner = ts_node_named_child(n, 0);
  if (ts_node_is_null(inner)) {
    return;
  }
  std::string_view it(ts_node_type(inner));
  auto             emit_marker = [&](Lnast_ntype::Lnast_ntype_int head, Lnast_node arg) {
    auto idx = builder.add_child(head);
    lnast->add_child(idx, builder.mint_tmp_ref());
    if (!arg.is_invalid()) {
      lnast->add_child(idx, arg);
    }
  };
  if (it == "break_statement") {
    emit_marker(Lnast_ntype::create_func_break(), Lnast_node::create_invalid());
    return;
  }
  if (it == "continue_statement") {
    emit_marker(Lnast_ntype::create_func_continue(), Lnast_node::create_invalid());
    return;
  }
  TSNode arg = child_by_field(inner, "argument");
  emit_marker(Lnast_ntype::create_func_return(), ts_node_is_null(arg) ? Lnast_node::create_invalid() : expr_to_node(arg));
}

std::vector<Prp2lnast::Call_arg> Prp2lnast::collect_call_args(TSNode arg_tuple) {
  std::vector<Call_arg> call_args;
  if (ts_node_is_null(arg_tuple)) {
    return call_args;
  }

  // Statement form (`process_function_call_statement`) wraps the call's
  // argument tuple in an outer `expression_list`. Unwrap to the inner tuple
  // so the loop below iterates the real arg children (not a single
  // wrapped-tuple "arg" that would emit `f((a,b))` instead of `f(a,b)`).
  if (std::string_view(ts_node_type(arg_tuple)) == "expression_list" && ts_node_named_child_count(arg_tuple) == 1) {
    TSNode           inner = ts_node_named_child(arg_tuple, 0);
    std::string_view it(ts_node_type(inner));
    if (it == "tuple" || it == "tuple_sq") {
      arg_tuple = inner;
    }
  }

  uint32_t nnc = ts_node_named_child_count(arg_tuple);
  call_args.reserve(nnc);
  for (uint32_t i = 0; i < nnc; ++i) {
    TSNode           c = ts_node_named_child(arg_tuple, i);
    std::string_view t(ts_node_type(c));

    Call_arg arg;
    if (t == "assignment") {
      arg.is_assign = true;
      TSNode lv     = child_by_field(c, "lvalue");
      TSNode rv     = child_by_field(c, "rvalue");

      std::string_view lvt(ts_node_type(lv));
      if (lvt == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          arg.assign_key = trim(get_text(id));
        }
      } else {
        arg.assign_key = trim(get_text(lv));
      }

      if (arg.assign_key.empty()) {
        arg.assign_key = builder.create_lnast_tmp();
      }

      if (!ts_node_is_null(rv)) {
        arg.value = expr_to_node(rv);
      } else {
        TSNode op    = child_by_field(c, "operator");
        auto   start = ts_node_is_null(op) ? ts_node_end_byte(lv) : ts_node_end_byte(op);
        arg.value    = constant_text_to_node(trim(text_between(start, ts_node_end_byte(c))));
      }
    } else if (t == "ref_identifier") {
      arg.is_ref = true;
      arg.value  = expr_to_node(c);
    } else {
      arg.value = expr_to_node(c);
    }
    call_args.emplace_back(std::move(arg));
  }

  if (call_args.empty() && ts_node_child_count(arg_tuple) > 0) {
    auto inner = trim(text_between(ts_node_start_byte(arg_tuple) + 1, ts_node_end_byte(arg_tuple) - 1));
    if (!inner.empty()) {
      Call_arg arg;
      arg.value = constant_text_to_node(inner);
      call_args.emplace_back(std::move(arg));
    }
  }

  return call_args;
}

void Prp2lnast::add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args) {
  for (const auto& arg : call_args) {
    if (arg.is_ufcs) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(call_ufcs_arg_marker));
      lnast->add_child(aidx, arg.value);
    } else if (arg.is_ref) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(call_ref_arg_marker));
      lnast->add_child(aidx, arg.value);
    } else if (arg.is_assign) {
      auto aidx = lnast->add_child(fcall_idx, Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(arg.assign_key));
      lnast->add_child(aidx, arg.value);
    } else {
      lnast->add_child(fcall_idx, arg.value);
    }
  }
}

void Prp2lnast::process_function_call_statement(TSNode n) {
  TSNode     func = child_by_field(n, "function");
  TSNode     args = child_by_field(n, "argument");
  Lnast_node func_ref;
  if (ts_node_is_null(func)) {
    func_ref = Lnast_node::create_const("nil");
  } else {
    auto name = trim(get_text(func));
    func_ref  = Lnast_node::create_ref(name);
  }
  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  lnast->add_child(idx, builder.mint_tmp_ref());
  lnast->add_child(idx, func_ref);
  add_call_args_to_fcall(idx, collect_call_args(args));
}

void Prp2lnast::process_lambda_statement(TSNode n) { process_lambda_statement_named(n, {}); }

void Prp2lnast::process_lambda_statement_named(TSNode n, std::string_view hoist_name) {
  TSNode func_type_node = child_by_field(n, "func_type");
  TSNode name_node      = child_by_field(n, "name");
  // function_definition_decl is a named child that isn't directly a field
  TSNode fdef;
  for (uint32_t i = 0; i < ts_node_named_child_count(n); i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view t(ts_node_type(c));
    if (t == "function_definition_decl") {
      fdef = c;
      break;
    }
  }
  TSNode code = child_by_field(n, "code");

  std::string_view kind       = ts_node_is_null(func_type_node) ? std::string_view{"comb"} : trim(get_text(func_type_node));
  // Task 1q — a pipe lambda's depth attribute (`pipe[3]`, `pipe[2..=5]`,
  // `pipe[1..<4]`, bare `pipe`) resolves to a per-output stages(min,max)
  // annotation stamped on the io list below; the kind const stays the plain
  // string "pipe". The grammar exposes the depth as the pipe_lambda node's
  // `depth` field (a `select`).
  bool             is_pipe    = kind.size() >= 4 && kind.substr(0, 4) == "pipe";
  int64_t          stages_min = 0;
  int64_t          stages_max = 0;
  if (is_pipe) {
    std::tie(stages_min, stages_max) = parse_pipe_depth(func_type_node);
    kind                             = "pipe";
  }

  // Task 1m — `pub comb/mod/pipe/fluid name …` marks the definition
  // exportable. Only file-scope named definitions may be pub.
  if (TSNode pub_node = child_by_field(n, "pub"); !ts_node_is_null(pub_node)) {
    if (!builder.at_top_stmts() || !lambda_kind_stack_.empty() || conditional_depth_ > 0) {
      report_error(pub_node,
                   "pub-not-file-scope",
                   "syntax",
                   "`pub` is only valid on file-scope definitions",
                   "move the definition to the file's top level");
    }
    if (ts_node_is_null(name_node)) {
      report_error(pub_node,
                   "pub-needs-name",
                   "syntax",
                   "`pub` requires a named definition",
                   "anonymous lambdas cannot be exported");
    }
    // Normalize the fluid kind text ("fluid …" variants) to the bare kind.
    std::string_view pub_kind = kind.size() >= 5 && kind.substr(0, 5) == "fluid" ? "fluid" : kind;
    lnast->add_pub(trim(get_text(name_node)), pub_kind);
  }

  // Workaround for tree-sitter not always attaching the body to `code`:
  // `comb mytest(a,b) -> (r) { ... }` parses as a `lambda` node followed
  // by a sibling `scope_statement` instead of attaching the block to the
  // lambda's `code` field. When we detect that, adopt the sibling as the
  // body and mark its start byte so the enclosing walker
  // (process_description / process_scope_statement) skips it.
  if (ts_node_is_null(code)) {
    for (TSNode sib = ts_node_next_named_sibling(n); !ts_node_is_null(sib); sib = ts_node_next_named_sibling(sib)) {
      std::string_view st(ts_node_type(sib));
      if (st == "comment") {
        continue;
      }
      if (st == "scope_statement") {
        code = sib;
        consumed_lambda_body_starts.insert(ts_node_start_byte(sib));
      }
      break;
    }
  }

  // 06-functions.md "Output tuple": outputs are always declared explicitly —
  // `-> (out, …)`, or `-> ()` for none. A missing `->` clause is a compile
  // error, never an implicit "no outputs": the old last-expression /
  // implicit-return sugar is gone, so an undeclared-output lambda can only be
  // a mistake (its caller has nothing to bind — see named_tuple.prp history).
  // Exemption: `self` methods (first input param named `self`, `ref` or not)
  // act through the receiver — setters, ctors, or debug-only prints/asserts —
  // and may omit the clause.
  if (!ts_node_is_null(fdef)) {
    bool has_output_clause = false;
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (fname && std::string_view(fname) == "output") {
        has_output_clause = true;  // the `->` anchor token carries the field tag
        break;
      }
    }
    if (!has_output_clause) {
      bool   first_param_is_self = false;
      TSNode inp                 = child_by_field(fdef, "input");
      if (!ts_node_is_null(inp)) {
        for (uint32_t i = 0; i < child_count(inp); i++) {
          TSNode ci = child(inp, i);
          if (std::string_view(ts_node_type(ci)) != "typed_identifier") {
            continue;
          }
          TSNode id           = child_by_field(ci, "identifier");
          first_param_is_self = !ts_node_is_null(id) && trim(get_text(id)) == "self";
          break;  // only the FIRST param can be the receiver
        }
      }
      if (!first_param_is_self) {
        // Prefer the SOURCE name; hoisted in-tuple methods fall back to the
        // mangled hoist name only when anonymous.
        std::string lname(!ts_node_is_null(name_node) ? trim(get_text(name_node))
                          : !hoist_name.empty()       ? hoist_name
                                                      : std::string_view{"lambda"});
        report_error(n,
                     "lambda-missing-output",
                     "syntax",
                     std::format("`{}` declares no outputs: the `-> (...)` output list is missing", lname),
                     "declare the outputs explicitly; use `-> ()` for a lambda with no outputs");
      }
    }
  }

  Lnast_node lambda_ref = !hoist_name.empty()        ? Lnast_node::create_ref(hoist_name)
                          : ts_node_is_null(name_node) ? builder.mint_tmp_ref()
                                                       : Lnast_node::create_ref(get_text(name_node));

  auto fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  lnast->add_child(fd_idx, lambda_ref);
  lnast->add_child(fd_idx, Lnast_node::create_const(kind));
  // generics tuple (Task 1p seam): each `<T, U>` name becomes a `ref` child of
  // this tuple_add. Empty when absent. func_extract copies these onto the
  // extracted Lnast (Lnast::generics_) so a generic signature is detected as a
  // template; the per-`T` body substitution is a deferred follow-up goal.
  // grammar.js: `function_definition_decl` carries the names under the
  // `generic` field as a `typed_identifier_list` between `<` and `>`.
  auto gen_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  if (!ts_node_is_null(fdef)) {
    uint32_t fcount = child_count(fdef);
    for (uint32_t i = 0; i < fcount; i++) {
      TSNode      ci = child(fdef, i);
      const char* fn = ts_node_field_name_for_child(fdef, i);
      if (!fn || std::string_view(fn) != "generic") {
        continue;
      }
      std::string_view ct(ts_node_type(ci));
      auto             add_one = [&](TSNode ti) {
        TSNode id = child_by_field(ti, "identifier");
        if (!ts_node_is_null(id)) {
          lnast->add_child(gen_idx, Lnast_node::create_ref(get_text(id)));
        }
      };
      if (ct == "typed_identifier_list") {
        uint32_t gc = ts_node_named_child_count(ci);
        for (uint32_t k = 0; k < gc; k++) {
          TSNode item = ts_node_named_child(ci, k);
          if (std::string_view(ts_node_type(item)) == "typed_identifier") {
            add_one(item);
          }
        }
      } else if (ct == "typed_identifier") {
        add_one(ci);
      } else if (ct == "identifier") {
        lnast->add_child(gen_idx, Lnast_node::create_ref(get_text(ci)));
      }
    }
  }

  // Emit input args. The grammar tags `ref`/`reg`/`...` prefixes on each arg
  // via the `mod` field and `= expr` defaults via the `definition` field;
  // iterate ALL children to pair `mod` and `definition` with the typed_ident
  // they bracket. The `ref` mod is encoded as the assign's RHS const text
  // ("ref") when no explicit default is present; downstream passes
  // (func_extract, constprop) detect it without inventing a new ntype.
  auto                    in_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  std::vector<Param_attr> input_attrs;
  // 2d-reg — `-> (reg q:T[@N] [= init])` output registers: the q pin IS the
  // output (the counter idiom). Collected here; a matching `reg` declare is
  // synthesized at body entry below so tolg lowers the output as a Flop.
  std::vector<std::pair<TSNode, TSNode>> output_regs;  // (typed_identifier, definition)
  auto                    collect_args = [&](TSNode                    container,
                                             const Lnast_nid&          parent_tup,
                                             std::vector<std::string>* names_out,
                                             std::vector<Param_attr>*  attrs_out,
                                             bool                      is_io_output) {
    TSNode pending_typed{};
    TSNode pending_def{};
    bool   pending_is_ref    = false;
    bool   pending_is_reg    = false;
    bool   pending_is_vararg = false;
    bool   next_is_ref       = false;
    bool   next_is_reg       = false;
    bool   next_is_vararg    = false;
    auto   flush             = [&]() {
      if (!ts_node_is_null(pending_typed)) {
        emit_arg_assign(parent_tup, pending_typed, pending_def, pending_is_ref, attrs_out, kind, is_io_output, pending_is_vararg);
        TSNode id = child_by_field(pending_typed, "identifier");
        if (!ts_node_is_null(id)) {
          // Earlier params are readable by LATER params' default values
          // (`comb f(a, b = a+5)` — 04-variables.md "Tuple scope" default
          // example): expose through the in-flight signature frame.
          inflight_name_scopes_.back().emplace_back(get_text(id));
          if (names_out) {
            names_out->emplace_back(get_text(id));
          }
        }
        // 2d-reg — a `reg` modifier on an OUTPUT marks an output register
        // (q is the output); on an input it is meaningless.
        if (pending_is_reg) {
          if (is_io_output) {
            output_regs.emplace_back(pending_typed, pending_def);
          } else {
            report_error(pending_typed,
                         "reg-input",
                         "type",
                         "a `reg` modifier is not valid on an input",
                         "registers hold state inside the body — declare the input plain and `reg` a body variable");
          }
        }
      }
      pending_typed     = TSNode{};
      pending_def       = TSNode{};
      pending_is_ref    = false;
      pending_is_reg    = false;
      pending_is_vararg = false;
    };
    uint32_t nc = child_count(container);
    for (uint32_t i = 0; i < nc; i++) {
      TSNode           ci    = child(container, i);
      const char*      fname = ts_node_field_name_for_child(container, i);
      std::string_view ct(ts_node_type(ci));
      if (fname && std::string_view(fname) == "mod") {
        // grammar arg_list `mod` field: choice('...', 'ref', 'reg').
        next_is_ref    = (ct == "ref");
        next_is_reg    = (ct == "reg");
        next_is_vararg = (ct == "...");  // Task 1p var-args param
        continue;
      }
      if (ct == "typed_identifier") {
        flush();
        pending_typed     = ci;
        pending_is_ref    = next_is_ref;
        pending_is_reg    = next_is_reg;
        pending_is_vararg = next_is_vararg;
        next_is_ref       = false;
        next_is_reg       = false;
        next_is_vararg    = false;
        continue;
      }
      if (fname && std::string_view(fname) == "definition") {
        // The `definition` field wraps `= expr`; the `=` token also carries
        // the field tag. Skip the `=` token and keep the expression.
        if (ct != "=") {
          pending_def = ci;
        }
        continue;
      }
    }
    flush();
  };
  // In-flight signature frame: params/outputs become readable (by later
  // default-value expressions, which lower into the ENCLOSING stmts frame) as
  // collect_args flushes them. Popped before the body — body reads resolve
  // against the func_def signature in read_is_visible instead.
  inflight_name_scopes_.emplace_back();
  if (!ts_node_is_null(fdef)) {
    TSNode inp = child_by_field(fdef, "input");
    if (!ts_node_is_null(inp)) {
      collect_args(inp, in_idx, nullptr, &input_attrs, /*is_io_output=*/false);
    }
  }

  // Output args
  auto                     out_idx = lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());
  std::vector<std::string> output_refs;  // for placeholder-lambda implicit assign
  if (!ts_node_is_null(fdef)) {
    // Collect ALL output field occurrences (there may be multiple due to "->" anchor + arg_list form)
    for (uint32_t i = 0; i < child_count(fdef); i++) {
      const char* fname = ts_node_field_name_for_child(fdef, i);
      if (!fname || std::string_view(fname) != "output") {
        continue;
      }
      TSNode           o = child(fdef, i);
      std::string_view ot(ts_node_type(o));
      if (ot == "arg_list") {
        collect_args(o, out_idx, &output_refs, nullptr, /*is_io_output=*/true);
      } else if (ot == "typed_identifier") {
        emit_arg_assign(out_idx, o, TSNode{}, /*is_ref_mod=*/false, nullptr, kind, /*is_io_output=*/true);
        TSNode id = child_by_field(o, "identifier");
        if (!ts_node_is_null(id)) {
          output_refs.emplace_back(get_text(id));
        }
      }
    }
  }

  inflight_name_scopes_.pop_back();  // signature frame ends here

  // Task 1q — stamp stages(min,max) as the TRAILING child of every OUTPUT io
  // store entry (after the optional type subtree; downstream identifies it by
  // ntype, never by position). Inputs never carry the annotation.
  if (is_pipe) {
    for (auto c = lnast->get_first_child(out_idx); !c.is_invalid(); c = lnast->get_sibling_next(c)) {
      if (!Lnast_ntype::is_store(lnast->get_type(c))) {
        continue;
      }
      auto st = lnast->add_child(c, Lnast_ntype::create_stages());
      lnast->add_child(st, Lnast_node::create_const(std::to_string(stages_min)));
      lnast->add_child(st, Lnast_node::create_const(std::to_string(stages_max)));
    }
  }

  // Body. A lambda body does NOT see the enclosing in-flight constructs — a
  // method hoisted out of a tuple literal must not resolve bare names against
  // the literal's fields (runtime upper-scope names need `self` or explicit
  // args; 04-variables.md "Lambda scope"). Suspend the stack; params resolve
  // via the func_def signature in read_is_visible.
  auto saved_inflight_scopes = std::exchange(inflight_name_scopes_, {});
  auto body_idx              = lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  // 2d-reg — synthesize the body `reg` declare for each `-> (reg q:T)` output
  // register, the same cluster process_lvalue_for_assign emits for a body
  // `reg q:T = init` (the decl-merge folds it into declare(q, T, reg,
  // [init])). The q pin is the output; body stores are its din writes.
  for (const auto& [or_typed, or_def] : output_regs) {
    if (kind == "comb") {
      report_error(or_typed,
                   "reg-in-comb",
                   "type",
                   "a `comb` may not declare a `reg` (combinational logic holds no state)",
                   "move the register into a `pipe`/`mod` body, or pass the value through an output");
    }
    TSNode or_id = child_by_field(or_typed, "identifier");
    if (ts_node_is_null(or_id)) {
      continue;
    }
    auto or_ref = Lnast_node::create_ref(get_text(or_id));
    auto setix  = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(setix, or_ref);
    lnast->add_child(setix, Lnast_node::create_const("type"));
    lnast->add_child(setix, Lnast_node::create_const("reg"));
    attach_loc(setix, or_id);
    if (!ts_node_is_null(or_def)) {
      // Literal initializer only (a reset value must be a compile-time
      // constant); tolg rejects anything unresolved.
      lnast->add_child(setix, Lnast_node::create_const(trim(get_text(or_def))));
    }
    TSNode or_tc = child_by_field(or_typed, "type");
    if (!ts_node_is_null(or_tc)) {
      emit_type_spec(or_ref, or_tc);
    }
  }
  // Parameter-side attribute carriers (`a::[comptime]`, …) are sugar for an
  // `attr_set(a, key, val)` + a `cassert(a.[key])` at body entry. The attr_set
  // marks the local symbol so reads inside the body fold; for `comptime` the
  // cassert enforces the constraint once function-call inlining substitutes
  // the actual argument.
  for (const auto& pa : input_attrs) {
    auto sref  = Lnast_node::create_ref(pa.param);
    auto setix = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(setix, sref);
    lnast->add_child(setix, Lnast_node::create_const(pa.key));
    lnast->add_child(setix, Lnast_node::create_const(pa.value.empty() ? std::string{"true"} : pa.value));
    if (pa.key == "comptime") {
      auto tmp = builder.mint_tmp_ref();
      auto gix = builder.add_child(Lnast_ntype::create_attr_get());
      lnast->add_child(gix, tmp);
      lnast->add_child(gix, sref);
      lnast->add_child(gix, Lnast_node::create_const("comptime"));
      auto cix = builder.add_child(Lnast_ntype::create_cassert());
      lnast->add_child(cix, tmp);
    }
  }
  // Capture formal-parameter widths so parse_int_const can resolve
  // `x.[bits]` while processing the body (see param_bits_stack_).
  // Walk the input arg container looking for typed_identifier nodes whose
  // type carries a concrete uint_type/sint_type width. Each frame is a
  // lexical scope; outer scopes remain visible for nested lambdas.
  std::unordered_map<std::string, int64_t> body_param_bits;
  std::function<void(TSNode)>              capture_param_bits = [&](TSNode node) {
    if (ts_node_is_null(node)) {
      return;
    }
    std::string_view nt(ts_node_type(node));
    if (nt == "typed_identifier") {
      TSNode id = child_by_field(node, "identifier");
      TSNode tc = child_by_field(node, "type");
      if (!ts_node_is_null(id) && !ts_node_is_null(tc)) {
        std::string                                   nm(trim(get_text(id)));
        // Walk the type_cast subtree for a uint_type/sint_type carrying a
        // concrete width as its first integer-literal child.
        std::function<std::optional<int64_t>(TSNode)> find_width = [&](TSNode t) -> std::optional<int64_t> {
          if (ts_node_is_null(t)) {
            return std::nullopt;
          }
          std::string_view tt(ts_node_type(t));
          // uint_type / sint_type carry their width as a HIDDEN integer token
          // (per [[tree_sitter_pyrope_hidden_tokens]]), so ts_node_named_child
          // returns nothing — extract the width from the node's TEXT instead.
          // `u6` → strip leading `u` → "6"; `s12` → strip `s` → "12".
          if (tt == "uint_type" || tt == "sint_type") {
            auto txt = trim(get_text(t));
            if (txt.size() >= 2 && (txt.front() == 'u' || txt.front() == 's')) {
              auto digits = txt.substr(1);
              try {
                size_t  pos = 0;
                int64_t v   = std::stoll(std::string(digits), &pos);
                if (pos == digits.size() && v > 0) {
                  return v;
                }
              } catch (...) {
              }
            }
            return std::nullopt;
          }
          if (tt == "bool_type") {
            return 1;
          }
          uint32_t cnc = ts_node_named_child_count(t);
          for (uint32_t j = 0; j < cnc; j++) {
            if (auto v = find_width(ts_node_named_child(t, j))) {
              return v;
            }
          }
          return std::nullopt;
        };
        if (auto w = find_width(tc)) {
          body_param_bits[nm] = *w;
        }
      }
    }
    uint32_t cnc = ts_node_named_child_count(node);
    for (uint32_t j = 0; j < cnc; j++) {
      capture_param_bits(ts_node_named_child(node, j));
    }
  };
  // Inputs: walk the lambda's arg_list / typed_identifier_list. Easier and
  // safer than reconstructing from the collect_args call — that path also
  // walks `mod`/`definition` siblings we don't care about here. Just look
  // through every TS child of the lambda node for typed_identifiers.
  capture_param_bits(n);
  param_bits_stack_.push_back(std::move(body_param_bits));
  // Task 1r — body-processing context: gates `stage[N]` (mod-only) and
  // `x@[N]` timecheck emission (mod/pipe only).
  lambda_kind_stack_.emplace_back(kind);

  if (!ts_node_is_null(code)) {
    // Placeholder lambda (06-functions.md "Output tuple"): when the body of
    // a single-output `comb` is a bare expression, the value is implicitly
    // assigned to the single output. Detect: scope_statement has exactly
    // one named child and that child is an expression-as-statement node.
    //
    // `if_expression` / `match_expression` are dual-use: they parse as
    // expressions but the body branches often contain plain statements that
    // explicitly write to the output (`if cond { res = a + 1 } else { … }`).
    // For those, the placeholder treatment would inject a synthetic tmp
    // (`assign output = ___1`) that overrides the in-branch writes and the
    // function ends up returning the tmp's default of 0. So only treat
    // if/match as placeholder when no branch contains an `assignment` to
    // (or other statement-level write into) the named outputs.
    bool   placeholder = false;
    TSNode expr_body{};
    if (kind == "comb" && output_refs.size() == 1 && std::string_view(ts_node_type(code)) == "scope_statement"
        && ts_node_named_child_count(code) == 1) {
      TSNode           only = ts_node_named_child(code, 0);
      std::string_view ot(ts_node_type(only));
      if (ot == "expression_item" || ot == "constant" || ot == "identifier" || ot == "unary_expression"
          || ot == "function_call_expression" || ot == "tuple" || ot == "tuple_sq" || ot == "bit_selection"
          || ot == "member_selection" || ot == "attribute_read" || ot == "dot_expression") {
        placeholder = true;
        expr_body   = only;
      } else if (ot == "if_expression" || ot == "match_expression") {
        // Recursively walk the if/match: if any descendant is an `assignment`
        // (any flavor) or a `control_statement`/`while`/`for`/`loop`, the
        // body is statement-style and the placeholder treatment is wrong.
        std::function<bool(TSNode)> has_stmt = [&](TSNode n) -> bool {
          if (ts_node_is_null(n)) {
            return false;
          }
          std::string_view tt(ts_node_type(n));
          if (tt == "assignment" || tt == "declaration_statement" || tt == "control_statement" || tt == "while_statement"
              || tt == "for_statement" || tt == "loop_statement" || tt == "assert_statement" || tt == "function_call_statement") {
            return true;
          }
          uint32_t cnc = ts_node_named_child_count(n);
          for (uint32_t i = 0; i < cnc; i++) {
            if (has_stmt(ts_node_named_child(n, i))) {
              return true;
            }
          }
          return false;
        };
        if (!has_stmt(only)) {
          placeholder = true;
          expr_body   = only;
        }
      }
    }
    if (placeholder) {
      Lnast_node val  = expr_to_node(expr_body);
      auto       aidx = builder.add_child(Lnast_ntype::create_store());
      lnast->add_child(aidx, Lnast_node::create_ref(output_refs.front()));
      lnast->add_child(aidx, val);
    } else {
      process_scope_statement(code, body_idx);
    }
  }
  // Drop the body's param-bits frame even if there was no `code` (defensive:
  // we pushed unconditionally above).
  if (!param_bits_stack_.empty()) {
    param_bits_stack_.pop_back();
  }
  if (!lambda_kind_stack_.empty()) {
    lambda_kind_stack_.pop_back();
  }
  builder.pop_stmts();
  inflight_name_scopes_ = std::move(saved_inflight_scopes);
}

// Task 1r/1q — resolve a timing-index node to a compile-time integer. A
// `constant` CST node parses through the Pyrope literal parser (so hex/octal/
// underscore-grouped forms work); an `identifier` looks up a recorded
// `const NAME = <int literal>` binding (see const_int_bindings_). Anything
// else (a mut/input/runtime ref, a non-integer const, a too-wide literal) is
// std::nullopt so the caller can emit its own diagnostic.
std::optional<int64_t> Prp2lnast::resolve_cycle_value(TSNode n) const {
  if (ts_node_is_null(n)) {
    return std::nullopt;
  }
  std::string_view t(ts_node_type(n));
  if (t == "constant") {
    auto txt = trim(get_text(n));
    if (txt.empty()) {
      return std::nullopt;
    }
    if (auto cst = Dlop::from_pyrope(txt); cst && cst->is_integer() && cst->is_i()) {
      return cst->to_i();
    }
    return std::nullopt;
  }
  if (t == "identifier") {
    auto it = const_int_bindings_.find(std::string(trim(get_text(n))));
    if (it != const_int_bindings_.end()) {
      return it->second;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

// Task 1q — resolve the pipe_lambda `depth` field to the (min,max) stages
// pair (see hpp). 06c-pipelining.md admits `pipe[N]`, `pipe[A..=B]`,
// `pipe[A..<B]` and bare `pipe` — open-ended ranges (`pipe[2..]`) and
// non-literal depths are rejected.
std::pair<int64_t, int64_t> Prp2lnast::parse_pipe_depth(TSNode pipe_lambda_node) {
  TSNode depth = child_by_field(pipe_lambda_node, "depth");
  if (ts_node_is_null(depth)) {
    return {1, 0};  // bare `pipe`: min 1 (contract: N >= 1), max 0 = unconstrained
  }

  // A `constant` literal goes through the Pyrope literal parser (so hex/octal/
  // underscore-grouped forms like `0x3` / `1_0` work), or a compile-time-
  // resolvable `const NAME` is looked up — the same helper as the @[N] /
  // stage[N] timing slots.
  auto lit_int = [&](TSNode c) -> std::optional<int64_t> { return resolve_cycle_value(c); };

  // Depths ride int32 fields downstream (Lnast_io_entry::stages_min/max);
  // diagnose anything beyond instead of silently truncating.
  constexpr int64_t k_max_depth = std::numeric_limits<int32_t>::max();
  auto              check_max   = [&](int64_t v, TSNode anchor) {
    if (v > k_max_depth) {
      report_error(anchor,
                   "invalid-pipe-depth",
                   "type",
                   std::format("invalid pipe depth {}: exceeds the maximum of {}", v, k_max_depth),
                   "use a realistic stage count");
    }
  };

  TSNode index_n = child_by_field(depth, "index");

  // `pipe[N]` — a single integer literal.
  if (auto v = lit_int(index_n)) {
    if (*v < 1) {
      report_error(index_n,
                   "invalid-pipe-depth",
                   "type",
                   std::format("invalid pipe depth {}: a pipe has at least one stage", *v),
                   "use `pipe[1]` for a single-stage pipe, or `comb` for pure combinational logic");
    }
    check_max(*v, index_n);
    return {*v, *v};
  }

  // `pipe[A..=B]` / `pipe[A..<B]` — closed literal ranges parse as an
  // expression_item (const, binary_other_op(op_range_inclusive|exclusive),
  // const). The select's `range` field only carries open-ended forms, which
  // are not valid pipe depths.
  if (!ts_node_is_null(index_n) && std::string_view(ts_node_type(index_n)) == "expression_item"
      && ts_node_named_child_count(index_n) == 3) {
    TSNode lhs = ts_node_named_child(index_n, 0);
    TSNode op  = ts_node_named_child(index_n, 1);
    TSNode rhs = ts_node_named_child(index_n, 2);
    if (std::string_view(ts_node_type(op)) == "binary_other_op") {
      TSNode op_inner = ts_node_named_child(op, 0);
      if (!ts_node_is_null(op_inner)) {
        std::string_view op_kind(ts_node_type(op_inner));
        bool             inclusive = op_kind == "op_range_inclusive";
        if (inclusive || op_kind == "op_range_exclusive") {
          auto lo = lit_int(lhs);
          auto hi = lit_int(rhs);
          if (lo && hi) {
            if (*lo < 1) {
              report_error(lhs,
                           "invalid-pipe-depth",
                           "type",
                           std::format("invalid pipe depth range minimum {}: a pipe has at least one stage", *lo),
                           "start the range at 1 or higher, e.g. `pipe[1..=4]`");
            }
            int64_t max = inclusive ? *hi : *hi - 1;
            if (max < *lo) {
              report_error(
                  op,
                  "invalid-pipe-depth",
                  "type",
                  std::format("invalid pipe depth range: {} never reaches {} (only ascending ranges are allowed)", *lo, *hi),
                  "swap the bounds so the range ascends, e.g. `pipe[2..=5]`");
            }
            check_max(max, rhs);
            return {*lo, max};
          }
        }
      }
    }
  }

  report_error(depth,
               "invalid-pipe-depth",
               "type",
               "pipe depth must be a literal integer (or a compile-time constant) or a literal ascending range",
               "write `pipe[3]`, `pipe[2..=5]`, `pipe[1..<4]`, or bare `pipe`");
}

// Task 1r — stage_decl timing slot → (min,max) const texts (see hpp).
std::pair<std::string, std::string> Prp2lnast::parse_stage_slot(TSNode storage_node) {
  TSNode slot{};
  if (!ts_node_is_null(storage_node)) {
    for (uint32_t i = 0, nc = ts_node_named_child_count(storage_node); i < nc; ++i) {
      TSNode c = ts_node_named_child(storage_node, i);
      if (std::string_view(ts_node_type(c)) == "timing_slot") {
        slot = c;
        break;
      }
    }
  }
  // Bare `stage` / `stage[]` — count unconstrained, the toolchain picks.
  if (ts_node_is_null(slot)) {
    return {"nil", "nil"};
  }
  TSNode index_n = child_by_field(slot, "index");
  if (ts_node_is_null(index_n)) {
    if (TSNode range_n = child_by_field(slot, "range"); !ts_node_is_null(range_n)) {
      report_error(range_n,
                   "invalid-stage-count",
                   "type",
                   "stage count must be a literal integer or a literal ascending closed range",
                   "write `stage[3]`, `stage[2..=5]`, or `stage[]` to let the toolchain pick");
    }
    return {"nil", "nil"};
  }

  // `constant` literal or a compile-time-resolvable `const NAME`.
  auto lit_int = [&](TSNode c) -> std::optional<int64_t> { return resolve_cycle_value(c); };

  // `stage[N]` — a single literal.
  if (auto v = lit_int(index_n)) {
    if (*v < 1) {
      report_error(index_n,
                   "invalid-stage-count",
                   "type",
                   std::format("invalid stage count {}: a stage delivers its value at least one cycle later", *v),
                   "use `stage[1]`, or a plain assignment for a same-cycle value");
    }
    auto txt = std::to_string(*v);
    return {txt, txt};
  }

  // `stage[A..=B]` / `stage[A..<B]` — closed literal ranges (expression_item).
  if (std::string_view(ts_node_type(index_n)) == "expression_item" && ts_node_named_child_count(index_n) == 3) {
    TSNode lhs = ts_node_named_child(index_n, 0);
    TSNode op  = ts_node_named_child(index_n, 1);
    TSNode rhs = ts_node_named_child(index_n, 2);
    if (std::string_view(ts_node_type(op)) == "binary_other_op") {
      TSNode op_inner = ts_node_named_child(op, 0);
      if (!ts_node_is_null(op_inner)) {
        std::string_view op_kind(ts_node_type(op_inner));
        bool             inclusive = op_kind == "op_range_inclusive";
        if (inclusive || op_kind == "op_range_exclusive") {
          auto lo = lit_int(lhs);
          auto hi = lit_int(rhs);
          if (lo && hi) {
            if (*lo < 1) {
              report_error(lhs,
                           "invalid-stage-count",
                           "type",
                           std::format("invalid stage count range minimum {}: a stage delivers at least one cycle later", *lo),
                           "start the range at 1 or higher, e.g. `stage[1..=4]`");
            }
            int64_t max = inclusive ? *hi : *hi - 1;
            if (max < *lo) {
              report_error(
                  op,
                  "invalid-stage-count",
                  "type",
                  std::format("invalid stage count range: {} never reaches {} (only ascending ranges are allowed)", *lo, *hi),
                  "swap the bounds so the range ascends, e.g. `stage[2..=5]`");
            }
            return {std::to_string(*lo), std::to_string(max)};
          }
        }
      }
    }
  }

  report_error(index_n,
               "invalid-stage-count",
               "type",
               "stage count must be a literal integer (or a compile-time constant) or a literal ascending closed range",
               "write `stage[3]`, `stage[2..=5]`, or `stage[]` to let the toolchain pick");
}

// Task 1r — `x@[N]` → inert timecheck statement (see hpp).
void Prp2lnast::maybe_emit_timecheck(TSNode timing_slot, TSNode id_node) {
  if (ts_node_is_null(timing_slot)) {
    return;
  }
  if (lambda_kind_stack_.empty() || lambda_kind_stack_.back() == "comb") {
    report_error(timing_slot,
                 "cycle-check-not-in-comb",
                 "type",
                 "`@[N]` cycle checks are only meaningful inside `mod` and `pipe` bodies",
                 "comb values are all at cycle 0 by definition — drop the `@[...]`");
  }
  TSNode index_n = child_by_field(timing_slot, "index");
  if (ts_node_is_null(index_n)) {
    if (TSNode range_n = child_by_field(timing_slot, "range"); !ts_node_is_null(range_n)) {
      report_error(range_n,
                   "invalid-cycle-check",
                   "type",
                   "a cycle check asserts a single landing cycle, not a range",
                   "write `@[N]` with a literal N, or opt out with `@[]`");
    }
    return;  // `@[]` — explicit opt-out, nothing recorded
  }
  std::optional<int64_t> lit = resolve_cycle_value(index_n);
  if (!lit || *lit < 0) {
    report_error(index_n,
                 "invalid-cycle-check",
                 "type",
                 "a cycle check must resolve to a cycle >= 0 (a literal or compile-time constant, counted from the "
                 "enclosing lambda's inputs)",
                 "write `@[N]` with a literal N (or a `const` resolvable to one), or opt out with `@[]`");
  }
  auto txt  = std::to_string(*lit);
  auto tcix = builder.add_child(Lnast_ntype::create_timecheck());
  lnast->add_child(tcix, identifier_to_node(id_node, /*for_lvalue=*/true));
  lnast->add_child(tcix, Lnast_node::create_const(txt));
  lnast->add_child(tcix, Lnast_node::create_const(txt));
  attach_loc(tcix, timing_slot);
}

Lnast_node Prp2lnast::lower_enum_def(std::string_view enum_name, TSNode enum_level_type, const std::vector<Enum_entry>& entries) {
  // Payload type text of a type_cast / expression_type / bare identifier.
  auto type_text_of = [&](TSNode tc) -> std::string {
    if (ts_node_is_null(tc)) {
      return {};
    }
    if (TSNode ty = child_by_field(tc, "type"); !ts_node_is_null(ty)) {
      tc = ty;
    }
    return std::string(trim(get_text(tc)));
  };
  const std::string level_pt = type_text_of(enum_level_type);

  // Ordinal mode (payload-less): one-hot when NO entry is explicit, else a
  // traditional sequence (explicit value resets the counter) — 03-bundle.md.
  bool any_explicit = false;
  for (const auto& e : entries) {
    if (e.has_value && !e.has_type && level_pt.empty()) {
      any_explicit = true;
      break;
    }
  }
  // Literal decimal parse for explicit ordinal values (underscores allowed).
  auto parse_ordinal = [&](TSNode v) -> std::optional<int64_t> {
    if (ts_node_is_null(v)) {
      return std::nullopt;
    }
    std::string txt(trim(get_text(v)));
    std::erase(txt, '_');
    try {
      size_t  pos = 0;
      int64_t r   = std::stoll(txt, &pos);
      if (pos == txt.size()) {
        return r;
      }
    } catch (...) {
    }
    return std::nullopt;
  };

  std::vector<std::pair<std::string, Lnast_node>> fields;
  fields.reserve(entries.size());
  int64_t seq_next = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto&       e    = entries[i];
    const std::string pt   = e.has_type ? type_text_of(e.type_node) : level_pt;
    const std::string full = absl::StrCat(enum_name, ".", e.name);
    Lnast_node        carrier{Lnast_node::create_invalid()};

    if (!pt.empty() && e.has_value) {
      // Typed payload entry (`Yellow:Rgb = 0xff_ff00`): construct `Rgb(v)`.
      // The runner's init-construction hook splices the payload type's init.
      auto val  = expr_to_node(e.value_node);
      auto fidx = builder.add_child(Lnast_ntype::create_func_call());
      carrier   = builder.mint_tmp_ref();
      lnast->add_child(fidx, carrier);
      lnast->add_child(fidx, Lnast_node::create_ref(pt));
      lnast->add_child(fidx, val);
    } else if (e.has_value) {
      if (auto ov = parse_ordinal(e.value_node); ov.has_value() && level_pt.empty() && !e.has_type) {
        // Explicit ordinal literal (`b=5`) — resets the sequence counter.
        seq_next  = *ov + 1;
        auto tidx = builder.add_child(Lnast_ntype::create_tuple_add());
        carrier   = builder.mint_tmp_ref();
        lnast->add_child(tidx, carrier);
        lnast->add_child(tidx, Lnast_node::create_const(std::to_string(*ov)));
      } else {
        // Expression value (`Green = Rgb(0x00FF00)`, `a = c`): the lowered
        // expr is the carrier when it is already a bundle-producing ref;
        // otherwise wrap the scalar into a one-slot bundle.
        auto val = expr_to_node(e.value_node);
        if (val.is_ref()) {
          carrier = val;
        } else {
          auto tidx = builder.add_child(Lnast_ntype::create_tuple_add());
          carrier   = builder.mint_tmp_ref();
          lnast->add_child(tidx, carrier);
          lnast->add_child(tidx, val);
        }
      }
    } else {
      // Auto ordinal: one-hot bit i, or the running sequence value.
      const int64_t v = any_explicit ? seq_next : (int64_t(1) << i);
      seq_next        = v + 1;
      auto tidx       = builder.add_child(Lnast_ntype::create_tuple_add());
      carrier         = builder.mint_tmp_ref();
      lnast->add_child(tidx, carrier);
      lnast->add_child(tidx, Lnast_node::create_const(std::to_string(v)));
    }

    // Identity tag: `carrier.__enumentry = 'NAME.entry'` (a 3-child store —
    // the `__`-prefixed key lands as a bundle ATTR leaf, so value compares
    // ignore it while enum-aware `in`/`string()` read it).
    auto sidx = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(sidx, carrier);
    lnast->add_child(sidx, Lnast_node::create_const("__enumentry"));
    lnast->add_child(sidx, Lnast_node::create_const(absl::StrCat("'", full, "'")));

    fields.emplace_back(e.name, carrier);
  }

  // The enum-type bundle: entry name → carrier.
  auto bidx = builder.add_child(Lnast_ntype::create_tuple_add());
  auto bref = builder.mint_tmp_ref();
  lnast->add_child(bidx, bref);
  for (const auto& [k, v] : fields) {
    auto aidx = lnast->add_child(bidx, Lnast_ntype::create_store());
    lnast->add_child(aidx, Lnast_node::create_ref(k));
    lnast->add_child(aidx, v);
  }
  return bref;
}

void Prp2lnast::process_enum_assignment(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  TSNode etype  = child_by_field(n, "type");    // `enum Color2:Rgb = (…)` payload type
  TSNode values = child_by_field(n, "values");  // the entries tuple

  std::vector<Enum_entry> entries;
  if (!ts_node_is_null(values)) {
    for (uint32_t i = 0, nnc = ts_node_named_child_count(values); i < nnc; ++i) {
      TSNode           c = ts_node_named_child(values, i);
      std::string_view t(ts_node_type(c));
      if (t == "comment") {
        continue;
      }
      Enum_entry e;
      if (t == "identifier") {
        e.name = trim(get_text(c));
      } else if (t == "typed_identifier") {
        TSNode id = child_by_field(c, "identifier");
        e.name    = trim(get_text(ts_node_is_null(id) ? c : id));
        if (TSNode tc = child_by_field(c, "type"); !ts_node_is_null(tc)) {
          e.type_node = tc;
          e.has_type  = true;
        }
      } else if (t == "assignment") {
        TSNode lv = child_by_field(c, "lvalue");
        TSNode rv = child_by_field(c, "rvalue");
        if (ts_node_is_null(lv)) {
          continue;
        }
        if (std::string_view(ts_node_type(lv)) == "typed_identifier") {
          TSNode id = child_by_field(lv, "identifier");
          e.name    = trim(get_text(ts_node_is_null(id) ? lv : id));
          if (TSNode tc = child_by_field(lv, "type"); !ts_node_is_null(tc)) {
            e.type_node = tc;
            e.has_type  = true;
          }
        } else {
          e.name = trim(get_text(lv));
        }
        if (!ts_node_is_null(rv)) {
          e.value_node = rv;
          e.has_value  = true;
        }
      } else {
        continue;  // spreads / exotic items: not lowered yet
      }
      if (!e.name.empty()) {
        entries.push_back(std::move(e));
      }
    }
  }

  auto bref = lower_enum_def(get_text(name), etype, entries);

  // Mirror `type Foo = (…)`: declare with mode `type`, then bind the bundle —
  // so `mut x:Color2 = …` resolves the typename and `Color2.Green` folds.
  auto didx = builder.add_child(Lnast_ntype::create_declare());
  attach_loc(didx, n);
  lnast->add_child(didx, Lnast_node::create_ref(get_text(name)));
  lnast->add_child(didx, Lnast_ntype::create_prim_type_none());
  lnast->add_child(didx, Lnast_node::create_const("type"));
  auto sidx = builder.add_child(Lnast_ntype::create_store());
  attach_loc(sidx, n);
  lnast->add_child(sidx, Lnast_node::create_ref(get_text(name)));
  lnast->add_child(sidx, bref);
}

void Prp2lnast::process_type_statement(TSNode n) {
  TSNode name = child_by_field(n, "name");
  if (ts_node_is_null(name)) {
    return;
  }
  // Task 1t — `type Foo = …` is a declaration whose mode is `type` (replaces
  // the former type_def node). The type slot is `prim_type_none` and the mode
  // const carries "type".
  auto idx = builder.add_child(Lnast_ntype::create_declare());
  lnast->add_child(idx, Lnast_node::create_ref(get_text(name)));
  lnast->add_child(idx, Lnast_ntype::create_prim_type_none());
  lnast->add_child(idx, Lnast_node::create_const("type"));

  // Task 1t — keep the type's bundle as a VALUE in the symbol table so a later
  // `mut v:Foo = (…)` can materialize Foo's default fields/values (named-type
  // borrowing — see upass_constprop process_assign). `type Foo = (tuple)` puts
  // the tuple in the `alias` field (the `= _type` form) or `definition` (the
  // no-`=` `type Foo(…)` trait form). A `type Foo = OtherType` alias (an
  // identifier/scalar type, not a tuple) carries no field bundle, so we only
  // store a tuple RHS. Mirrors the `const Foo = (…)` lowering.
  TSNode rhs = child_by_field(n, "definition");  // `type Foo ( … )` trait form
  if (ts_node_is_null(rhs)) {
    rhs = child_by_field(n, "alias");  // `type Foo = …` form (an expression_type)
  }
  // `type Foo = (…)` lands the tuple inside an `expression_type` wrapper; unwrap
  // to the inner `tuple`. (`type Foo = OtherType` wraps a scalar/identifier type
  // with no field bundle — skipped below since it is not a tuple.)
  if (!ts_node_is_null(rhs) && std::string_view(ts_node_type(rhs)) == "expression_type") {
    for (uint32_t i = 0, nc = ts_node_child_count(rhs); i < nc; i++) {
      TSNode c = ts_node_child(rhs, i);
      if (std::string_view(ts_node_type(c)) == "tuple") {
        rhs = c;
        break;
      }
    }
  }
  if (!ts_node_is_null(rhs) && std::string_view(ts_node_type(rhs)) == "tuple") {
    // Lower the bundle FIRST (emits the tuple_add statement), then build the
    // store referencing its result — so the producer precedes the consumer.
    auto rhs_val = expr_to_node(rhs);
    auto sidx    = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(sidx, Lnast_node::create_ref(get_text(name)));
    lnast->add_child(sidx, rhs_val);
  }
}

// Defined with the interpolated-string lowering below; needed early by the
// import-argument validation.
static std::string unescape_cooked_string(std::string_view raw);

// Task 1m — the `import` builtin (the LiveHD docs §1).
// Returns the unquoted body when `n` is a plain comptime string literal
// expression — `'…'` (raw) or `"…"` with no `{…}` interpolation — else nullopt.
std::optional<std::string> Prp2lnast::plain_string_literal_text(TSNode n) {
  if (ts_node_is_null(n)) {
    return std::nullopt;
  }
  std::string_view t(ts_node_type(n));
  if ((t == "constant" || t == "paren_group") && ts_node_named_child_count(n) == 1) {
    return plain_string_literal_text(ts_node_named_child(n, 0));
  }
  if (t == "string_literal") {  // single-quoted raw literal: '…'
    auto txt = trim(get_text(n));
    if (txt.size() >= 2) {
      return std::string(txt.substr(1, txt.size() - 2));
    }
    return std::nullopt;
  }
  if (t == "interpolated_string_literal") {  // double-quoted; reject `{expr}` parts
    if (ts_node_named_child_count(n) != 0) {
      return std::nullopt;
    }
    if (ts_node_end_byte(n) < ts_node_start_byte(n) + 2) {
      return std::nullopt;
    }
    auto body = text_between(ts_node_start_byte(n) + 1, ts_node_end_byte(n) - 1);
    return unescape_cooked_string(body);
  }
  return std::nullopt;
}

// The canonical marked-builtin call shape:
//   func_call(target, const "import", const '<unit>')
// — exactly what `lhd scan` (collect_imports) matches and the upass resolver
// (task 1m phase C) folds. The callee is a CONST (not a ref) so constprop
// leaves it unfolded and no user `import` definition can capture it.
void Prp2lnast::emit_import_call(const Lnast_node& target, std::string_view unit, TSNode loc_node) {
  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  lnast->add_child(idx, target);
  lnast->add_child(idx, Lnast_node::create_const("import"));
  lnast->add_child(idx, Lnast_node::create_const(absl::StrCat("'", unit, "'")));
  attach_loc(idx, loc_node);  // → resolution diagnostics point at the call site
}

// Expression form `import("unit")`: validate the argument (exactly one, a
// comptime string literal — imports resolve statically, so computed strings
// cannot name a unit) and lower to the canonical call.
void Prp2lnast::lower_import_call(TSNode call_node, TSNode arg_tuple, const Lnast_node& target) {
  std::vector<TSNode> items;
  if (!ts_node_is_null(arg_tuple)) {
    for (uint32_t i = 0, nc = ts_node_named_child_count(arg_tuple); i < nc; ++i) {
      TSNode c = ts_node_named_child(arg_tuple, i);
      if (std::string_view(ts_node_type(c)) == "comment") {
        continue;
      }
      items.push_back(c);
    }
  }
  if (items.size() != 1) {
    report_error(call_node,
                 "import-arity",
                 "syntax",
                 "`import` takes exactly one argument",
                 "write `import(\"unit\")`, `import(\"ln:unit.tree\")`, or `import(\"lg:graph\")`");
  }
  auto text = plain_string_literal_text(items.front());
  if (!text || text->empty()) {
    report_error(items.front(),
                 "import-not-literal",
                 "syntax",
                 "the `import` argument must be a comptime string literal",
                 "imports resolve statically — a computed string cannot name a unit");
  }
  emit_import_call(target, *text, call_node);
}

// Statement form `import "unit" as b` — exact sugar for
// `const b = import("unit")`: a const DECLARATION (declare via the decl-merge
// + store), so read-only-always and the normal no-shadowing / redeclaration
// rules apply unchanged.
void Prp2lnast::process_import_statement(TSNode n) {
  TSNode alias = child_by_field(n, "alias");
  TSNode mod   = child_by_field(n, "module");
  // String-literal-only (task 1m): the old dotted-identifier grammar form
  // (`import a.b.c as x`) is rejected.
  auto text = plain_string_literal_text(mod);
  if (!text || text->empty()) {
    report_error(ts_node_is_null(mod) ? n : mod,
                 "import-not-literal",
                 "syntax",
                 "the imported unit must be a comptime string literal",
                 "write `import \"unit\" as name`");
  }
  if (ts_node_is_null(alias)) {
    report_error(n, "import-needs-alias", "syntax", "`import … as <name>` requires an alias", "write `import \"unit\" as name`");
  }

  Lnast_node tmp = builder.mint_tmp_ref();
  emit_import_call(tmp, *text, n);

  Lnast_node ref = Lnast_node::create_ref(get_text(alias));
  {
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("type"));
    lnast->add_child(idx, Lnast_node::create_const("const"));
    // Span → the decl-merge copies this onto the synthesized `declare` so
    // declaration-site diagnostics point at the alias.
    attach_loc(idx, alias);
  }
  {
    auto idx = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(idx, ref);
    lnast->add_child(idx, tmp);
    attach_loc(idx, alias);
  }
}

void Prp2lnast::process_test_statement(TSNode n) {
  TSNode code   = child_by_field(n, "code");
  auto   fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  auto   tmp    = builder.mint_tmp_ref();
  lnast->add_child(fd_idx, tmp);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // generics
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // inputs
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // outputs
  auto body_idx = lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  builder.push_stmts(body_idx);
  if (!ts_node_is_null(code)) {
    process_scope_statement(code, body_idx);
  }
  builder.pop_stmts();
  auto aidx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(aidx, tmp);
  lnast->add_child(aidx, Lnast_node::create_const("test"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_spawn_statement(TSNode n) {
  TSNode     name   = child_by_field(n, "name");
  Lnast_node ref    = ts_node_is_null(name) ? builder.mint_tmp_ref() : Lnast_node::create_ref(get_text(name));
  auto       fd_idx = builder.add_child(Lnast_ntype::create_func_def());
  lnast->add_child(fd_idx, ref);
  lnast->add_child(fd_idx, Lnast_node::create_const("comb"));
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // generics
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // inputs
  lnast->add_child(fd_idx, Lnast_ntype::create_tuple_add());  // outputs
  lnast->add_child(fd_idx, Lnast_ntype::create_stmts());
  auto aidx = builder.add_child(Lnast_ntype::create_attr_set());
  lnast->add_child(aidx, ref);
  lnast->add_child(aidx, Lnast_node::create_const("spawn"));
  lnast->add_child(aidx, Lnast_node::create_const("true"));
}

void Prp2lnast::process_impl_statement(TSNode n) {
  // Simplified: emit as a placeholder assert to preserve node count.
  auto idx = builder.add_child(Lnast_ntype::create_cassert());
  attach_loc(idx, n);
  lnast->add_child(idx, Lnast_node::create_const("true"));
}

// ---------------- Expressions ----------------

Lnast_node Prp2lnast::expr_to_node(TSNode n) {
  if (ts_node_is_null(n)) {
    return Lnast_node::create_const("0");
  }
  std::string_view t(ts_node_type(n));
  if (t == "identifier") {
    return identifier_to_node(n, /*for_lvalue=*/false);
  }
  if (t == "timed_identifier") {
    // Task 1r — `tmp@[3]` on a RHS: the VALUE is the plain identifier read;
    // `@[3]` is a pure landing-cycle check (never a flop / never a
    // delay_assign), recorded as an inert timecheck statement.
    TSNode inner = child_by_field(n, "identifier");
    if (!ts_node_is_null(inner)) {
      maybe_emit_timecheck(child_by_field(n, "timing"), inner);
      return identifier_to_node(inner, /*for_lvalue=*/false);
    }
  }
  if (t == "expression_item") {
    return binary_expr_to_node(n);
  }
  if (t == "enum_definition") {
    // `const Color = enum(Yellow:Rgb = …, Green = Rgb(…))`. The entry-name
    // prefix ("Color.Yellow") comes from the enclosing assignment's lvalue.
    std::string ename;
    for (TSNode p = ts_node_parent(n); !ts_node_is_null(p); p = ts_node_parent(p)) {
      if (std::string_view(ts_node_type(p)) != "assignment") {
        continue;
      }
      TSNode lv = child_by_field(p, "lvalue");
      if (!ts_node_is_null(lv)) {
        TSNode id = std::string_view(ts_node_type(lv)) == "typed_identifier" ? child_by_field(lv, "identifier") : TSNode{};
        ename     = trim(get_text(ts_node_is_null(id) ? lv : id));
      }
      break;
    }
    // Entries ride in the `input` arg_list as lambda-style pairs: a
    // `typed_identifier` (name + optional payload type) optionally followed
    // by a `definition`-tagged value expression.
    std::vector<Enum_entry> entries;
    if (TSNode args = child_by_field(n, "input"); !ts_node_is_null(args)) {
      for (uint32_t i = 0, nc = child_count(args); i < nc; ++i) {
        TSNode           c     = child(args, i);
        const char*      fname = ts_node_field_name_for_child(args, i);
        std::string_view ct(ts_node_type(c));
        if (ct == "comment") {
          continue;
        }
        if (ct == "typed_identifier") {
          Enum_entry e;
          TSNode     id = child_by_field(c, "identifier");
          e.name        = trim(get_text(ts_node_is_null(id) ? c : id));
          if (TSNode tc = child_by_field(c, "type"); !ts_node_is_null(tc)) {
            e.type_node = tc;
            e.has_type  = true;
          }
          entries.push_back(std::move(e));
        } else if (fname && std::string_view(fname) == "definition" && ct != "=" && !entries.empty()) {
          entries.back().value_node = c;
          entries.back().has_value  = true;
        }
      }
    }
    return lower_enum_def(ename, TSNode{}, entries);
  }
  if (t == "constant") {
    // `constant` wraps one of: integer_literal, bool_literal, unknown_literal,
    // string_literal (single-quoted), or interpolated_string_literal (double-
    // quoted, may contain `{expr}` parts). Lower the inner literal directly
    // when it's a string variant — the interpolation/body handling lives in
    // expr_to_node's interpolated_string_literal branch. Other literals are
    // text-based; just emit the literal text.
    uint32_t nnc = ts_node_named_child_count(n);
    if (nnc == 1) {
      TSNode           c = ts_node_named_child(n, 0);
      std::string_view ct(ts_node_type(c));
      if (ct == "interpolated_string_literal") {
        return expr_to_node(c);
      }
    }
    auto txt = trim(get_text(n));
    check_binary_literal_sign(txt, n);
    return constant_text_to_node(txt);
  }
  if (t == "integer_literal" || t == "bool_literal" || t == "unknown_literal" || t == "string_literal") {
    auto txt = trim(get_text(n));
    check_binary_literal_sign(txt, n);
    return constant_text_to_node(txt);
  }
  if (t == "unary_expression") {
    return unary_expr_to_node(n);
  }
  if (t == "if_expression") {
    return if_expr_to_node(n);
  }
  if (t == "match_expression") {
    return match_expr_to_node(n);
  }
  if (t == "bit_selection") {
    return bit_selection_to_node(n);
  }
  if (t == "member_selection") {
    return member_selection_to_node(n);
  }
  if (t == "attribute_read") {
    return attribute_read_to_node(n);
  }
  if (t == "dot_expression") {
    return dot_expression_to_node(n);
  }
  if (t == "function_call_expression") {
    return function_call_expr_to_node(n);
  }
  if (t == "tuple") {
    // A single-item parenthesized expression with NO trailing comma is grouping
    // (`(x)` == x), not a 1-element tuple — only `(x,)` builds a 1-tuple. The
    // grammar parses both as a single-item `tuple` (the `,` is a hidden token),
    // so detect a trailing comma in the source text after the item. Without this,
    // `s = (b & 15)` lowers as a 1-tuple whose tuple_add tolg cannot lower (an
    // inlined caller folds the 1-tuple away; a standalone module reads it as nil).
    if (ts_node_named_child_count(n) == 1) {
      TSNode           it = ts_node_named_child(n, 0);
      std::string_view itt(ts_node_type(it));
      // Plain value item only: an `assignment` (`a=1`) / `unary_expression`
      // (`...x` spread) is a genuine 1-element bundle, never grouping.
      const bool plain = itt != "assignment" && itt != "unary_expression" && itt != "comment";
      const auto tail  = text_between(ts_node_end_byte(it), ts_node_end_byte(n));
      if (plain && tail.find(',') == std::string_view::npos) {
        return expr_to_node(it);  // grouping — the value itself
      }
    }
    return tuple_to_node(n, /*is_square=*/false);
  }
  if (t == "tuple_sq") {
    return tuple_to_node(n, /*is_square=*/true);
  }
  if (t == "paren_group") {
    // Single-expression parenthesized grouping introduced in the new grammar
    // (`(expr).foo`, `(expr)#[..]`, `(expr):type`). Pass through to the inner
    // expression — the parens carry no value semantics on their own.
    uint32_t nnc = ts_node_named_child_count(n);
    if (nnc >= 1) {
      return expr_to_node(ts_node_named_child(n, 0));
    }
    return Lnast_node::create_const("0");
  }
  if (t == "typed_identifier") {
    TSNode id = child_by_field(n, "identifier");
    if (!ts_node_is_null(id)) {
      return identifier_to_node(id, false);
    }
  }
  if (t == "type_specification") {
    return type_specification_to_node(n);
  }
  if (t == "ref_identifier") {
    TSNode id = ts_node_named_child(n, 0);
    if (!ts_node_is_null(id)) {
      return identifier_to_node(id, false);
    }
  }
  if (t == "optional_expression") {
    TSNode arg = child_by_field(n, "argument");
    return expr_to_node(arg);
  }
  if (t == "scope_statement") {
    // Inline a scope expression: run its statements in the current scope
    // and return the last computed expression.
    process_scope_statement(n, builder.idx_stmts);
    return Lnast_node::create_const("0");
  }
  if (t == "interpolated_string_literal") {
    return interpolated_string_to_node(n);
  }
  // Fallback: treat as a constant from source text.
  return constant_text_to_node(trim(get_text(n)));
}

// Decode the cooked (double-quoted) escape set: \n, \t, \\, \", \xNN.
// Anything else with a leading backslash is left as-is (the backslash and the
// following char both survive). Single-quoted (raw) strings never reach here.
static std::string unescape_cooked_string(std::string_view raw) {
  auto hex_digit = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    return -1;
  };
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (c != '\\' || i + 1 >= raw.size()) {
      out.push_back(c);
      continue;
    }
    char next = raw[i + 1];
    switch (next) {
      case 'n':
        out.push_back('\n');
        ++i;
        break;
      case 't':
        out.push_back('\t');
        ++i;
        break;
      case '\\':
        out.push_back('\\');
        ++i;
        break;
      case '"':
        out.push_back('"');
        ++i;
        break;
      case 'x': {
        int hi = (i + 2 < raw.size()) ? hex_digit(raw[i + 2]) : -1;
        int lo = (i + 3 < raw.size()) ? hex_digit(raw[i + 3]) : -1;
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 3;
        } else {
          out.push_back(c);  // malformed \x — keep verbatim
        }
        break;
      }
      default:
        out.push_back(c);  // unknown escape — keep backslash
        break;
    }
  }
  return out;
}

Lnast_node Prp2lnast::interpolated_string_to_node(TSNode n) {
  // Double-quoted string body, possibly containing `{expr[:fmt]}` chunks.
  // With no interpolations the named-child list is empty (all quote/text
  // tokens are anonymous), so re-emit the body as a single-quoted pyrope
  // string literal that `Dlop::from_pyrope` can round-trip. Escape sequences
  // (\n, \t, \\, \", \xNN) are processed here — the resulting bytes are
  // embedded directly inside the single-quoted wrapper.
  uint32_t nnc        = ts_node_named_child_count(n);
  uint32_t body_start = ts_node_start_byte(n) + 1;
  uint32_t body_end   = ts_node_end_byte(n) - 1;
  if (nnc == 0) {
    auto body    = text_between(body_start, body_end);
    auto decoded = unescape_cooked_string(body);
    return Lnast_node::create_const(absl::StrCat("'", decoded, "'"));
  }

  // Lower to `string("part0", expr0, "part1", expr1, ..., "partN")`. The
  // constprop pass folds calls whose every arg is a comptime constant into a
  // single string literal; otherwise the call survives at runtime.
  // Literal chunks aren't named children — find each `{` by scanning back
  // from the expression's start byte, and the matching `}` (past any format
  // spec, which is also anonymous) by scanning forward from its end.
  std::vector<Lnast_node> args;
  uint32_t                cursor = body_start;
  for (uint32_t i = 0; i < nnc; ++i) {
    TSNode   expr       = ts_node_named_child(n, i);
    uint32_t expr_start = ts_node_start_byte(expr);
    uint32_t expr_end   = ts_node_end_byte(expr);

    uint32_t brace_open = expr_start;
    while (brace_open > cursor && prp_file[brace_open - 1] != '{') {
      --brace_open;
    }
    if (brace_open > cursor) {
      --brace_open;  // step onto the `{`
    }
    auto pre = text_between(cursor, brace_open);
    if (!pre.empty()) {
      auto pre_decoded = unescape_cooked_string(pre);
      args.push_back(Lnast_node::create_const(absl::StrCat("'", pre_decoded, "'")));
    }

    // The closing `}` sits past an optional `:fmt` format spec, which is
    // anonymous in the grammar (`_format_spec` is a hidden rule). Find the
    // brace, then read the spec text sitting between the expression and it.
    uint32_t brace_close = expr_end;
    while (brace_close < body_end && prp_file[brace_close] != '}') {
      ++brace_close;
    }
    auto             gap = text_between(expr_end, brace_close);  // e.g. ":b", " : b", ""
    std::string_view spec;
    if (auto colon = gap.find(':'); colon != std::string_view::npos) {
      spec = trim(gap.substr(colon + 1));
    }

    auto value_node = expr_to_node(expr);
    if (spec.empty()) {
      args.push_back(value_node);
    } else {
      // `{expr:fmt}` → render `expr` through `__fmt(expr, 'fmt')`, an internal
      // cast that constprop folds into a string (e.g. `:b` → binary digits).
      // The enclosing `string(...)` then concatenates the rendered text. Emit
      // it here (before the outer string() call) so its tmp folds first.
      auto fidx = builder.add_child(Lnast_ntype::create_func_call());
      auto fref = builder.mint_tmp_ref();
      lnast->add_child(fidx, fref);
      lnast->add_child(fidx, Lnast_node::create_ref("__fmt"));
      lnast->add_child(fidx, value_node);
      lnast->add_child(fidx, Lnast_node::create_const(absl::StrCat("'", spec, "'")));
      args.push_back(fref);
    }

    cursor = brace_close < body_end ? brace_close + 1 : body_end;
  }
  if (cursor < body_end) {
    auto tail = text_between(cursor, body_end);
    if (!tail.empty()) {
      auto tail_decoded = unescape_cooked_string(tail);
      args.push_back(Lnast_node::create_const(absl::StrCat("'", tail_decoded, "'")));
    }
  }

  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, Lnast_node::create_ref("string"));
  for (auto& a : args) {
    lnast->add_child(idx, a);
  }
  return ref;
}

void Prp2lnast::check_binary_literal_sign(std::string_view text, const TSNode& node) const {
  // Binary literals must carry an explicit sign: `0ub…` (unsigned) or `0sb…`
  // (signed). The bare `0b…` form is no longer valid — it left the signedness
  // ambiguous. Detect `0` immediately followed by `b`/`B` (skipping an optional
  // leading `-`/`+`); a valid binary has `s`/`u` between the `0` and the `b`.
  size_t i = 0;
  if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
    ++i;
  }
  if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
    auto bits = text.substr(i + 2);
    report_error(node,
                 "invalid-binary-prefix",
                 "syntax",
                 std::format("binary literal `0b…` is missing its sign: use `0ub{}` (unsigned) or `0sb{}` (signed)", bits, bits),
                 "prefix binary constants with 0ub (unsigned) or 0sb (signed)");
  }
}

Lnast_node Prp2lnast::constant_text_to_node(std::string_view text) {
  if (text.empty()) {
    return Lnast_node::create_const("0");
  }
  return Lnast_node::create_const(std::string(text));
}

Lnast_node Prp2lnast::identifier_to_node(TSNode n, bool for_lvalue) {
  auto name = trim(get_text(n));
  // Keyword constants that fell through as identifiers.
  if (name == "true" || name == "false" || name == "nil") {
    return Lnast_node::create_const(name);
  }
  // Pyrope's `…` escape lets identifiers carry spaces/punctuation. When the
  // escaped text is a plain alnum/underscore identifier (the common case in
  // tests like ``a`` ↔ `a`), strip the backticks so the LNAST ref text is
  // the canonical name. The escape stays only when it actually carries a
  // special character.
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    auto inner = name.substr(1, name.size() - 2);
    bool ok    = !inner.empty();
    for (char ch : inner) {
      if (!(ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
        ok = false;
        break;
      }
    }
    if (ok && !(inner[0] >= '0' && inner[0] <= '9')) {
      // `inner` is a substring view of `name` (same backing buffer), so the
      // string_view stays valid after rebind.
      name = inner;
    }
  }
  if (for_lvalue) {
    return Lnast_node::create_ref(name);
  }
  // Value (read) context: record the source site plus the builder position so
  // check_undefined_reads can resolve it lexically (read-before-declaration,
  // out-of-scope and never-declared reads are all compile errors). Names
  // introduced by an in-flight construct (earlier tuple-literal fields,
  // earlier lambda params feeding a default value) are resolved right here —
  // they have no statement-level declaration the scope walk could find.
  if (!name_in_inflight_scope(name)) {
    read_sites_.push_back({std::string{name}, n, builder.idx_stmts, lnast->get_last_child(builder.idx_stmts)});
  }
  return Lnast_node::create_ref(name);
}

Lnast_node Prp2lnast::type_specification_to_node(TSNode n) {
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node aref = expr_to_node(arg);
  TSNode     ty   = child_by_field(n, "type");
  if (!ts_node_is_null(ty)) {
    auto idx = builder.add_child(Lnast_ntype::create_type_spec());
    lnast->add_child(idx, aref);
    emit_type_expr(idx, ty);
  }
  // The `attribute` field on `typedOrAttributed` wraps an anonymous
  // `seq(':', attribute_list)`, so `child_by_field("attribute")` returns
  // the `:` token rather than the attribute_list. Find the attribute_list
  // by walking children directly.
  uint32_t total = ts_node_child_count(n);
  for (uint32_t i = 0; i < total; i++) {
    TSNode           c     = ts_node_child(n, i);
    const char*      fname = ts_node_field_name_for_child(n, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "attribute_list") {
      emit_attribute_list(aref, c);
    } else if ((ct == "tuple_sq" || ct == "attribute_sq") && fname && std::string_view(fname) == "attribute") {
      // New grammar: write-side attribute carrier is a tuple_sq.
      emit_attribute_list(aref, c);
    }
  }
  return aref;
}

bool Prp2lnast::int_type_call_bounds(std::string_view kw, TSNode tup, std::string& max_txt, std::string& min_txt) {
  // Classify the base keyword → signedness and optional concrete width.
  bool unsigned_base = false;
  bool signed_base   = false;
  if (kw == "uint" || kw == "unsigned") {
    unsigned_base = true;
  } else if (kw == "int" || kw == "integer") {
    signed_base = true;
  } else if (kw.size() >= 2 && kw[0] == 'u'
             && std::all_of(kw.begin() + 1, kw.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    unsigned_base = true;
  } else if (kw.size() >= 2 && (kw[0] == 's' || kw[0] == 'i')
             && std::all_of(kw.begin() + 1, kw.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    signed_base = true;
  } else {
    return false;
  }
  auto bits_to_bounds = [&](int n, std::string& maxt, std::string& mint) {
    if (signed_base) {
      maxt = std::string(Dlop::get_mask_value(n - 1)->to_pyrope());
      mint = std::string(Dlop::get_neg_mask_value(n - 1)->to_pyrope());
    } else {
      maxt = std::string(Dlop::get_mask_value(n)->to_pyrope());
      mint = "0";
    }
  };
  if (unsigned_base) {
    min_txt = "0";
  }
  // Concrete width sugar (`u8(min=…)`): seed bounds from the width first.
  if (kw.size() >= 2 && (kw[0] == 'u' || kw[0] == 's' || kw[0] == 'i') && std::isdigit(static_cast<unsigned char>(kw[1]))) {
    bits_to_bounds(std::stoi(std::string(kw.substr(1))), max_txt, min_txt);
  }
  // Refine with the named args.
  uint32_t nn = ts_node_named_child_count(tup);
  for (uint32_t i = 0; i < nn; i++) {
    TSNode           item = ts_node_named_child(tup, i);
    std::string_view it(ts_node_type(item));
    if (it != "assignment" && it != "attribute_assignment") {
      continue;
    }
    TSNode lv = child_by_field(item, "lvalue");
    TSNode rv = child_by_field(item, "rvalue");
    if (ts_node_is_null(lv) || ts_node_is_null(rv)) {
      continue;
    }
    std::string key(trim(get_text(lv)));
    std::string val(trim(get_text(rv)));
    if (key == "range") {
      // No `range` type argument — it would be pure sugar for max/min, so it
      // is rejected to keep a single way to bound an integer type.
      report_error(item,
                   "int-type-range",
                   "type",
                   std::format("`{}(range=…)` is not a valid integer type bound", kw),
                   "bound the type with `max=`/`min=` (e.g. `int(max=15, min=0)`) or a width type `u4`/`s5`");
    }
    if (key == "max") {
      max_txt = val;
    } else if (key == "min") {
      min_txt = val;
    } else if (key == "bits") {
      auto bv = Dlop::from_pyrope(val);
      if (bv->is_i()) {
        bits_to_bounds(static_cast<int>(bv->to_i()), max_txt, min_txt);
      }
    }
  }
  return true;
}

bool Prp2lnast::is_prim_type_token(std::string_view txt) {
  if (txt == "int" || txt == "integer" || txt == "uint" || txt == "unsigned" || txt == "bool" || txt == "string") {
    return true;
  }
  return txt.size() >= 2 && (txt[0] == 'u' || txt[0] == 's' || txt[0] == 'i')
         && std::all_of(txt.begin() + 1, txt.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

Lnast_node Prp2lnast::does_operand_to_node(TSNode n) {
  std::string_view t(ts_node_type(n));
  if (t == "identifier") {
    auto name = trim(get_text(n));
    if (is_prim_type_token(name)) {
      // Type-token operand (`a does u32`): a bare ref with NO read site —
      // check_undefined_reads must not demand a variable here. constprop
      // decodes the name to kind+envelope; a real variable of this name
      // (e.g. `i2`) still wins because the fold consults the symbol table /
      // type-info first.
      return Lnast_node::create_ref(name);
    }
  } else if (t == "function_call_expression") {
    TSNode fn  = child_by_field(n, "function");
    TSNode arg = child_by_field(n, "argument");
    if (!ts_node_is_null(fn) && !ts_node_is_null(arg) && std::string_view(ts_node_type(fn)) == "identifier"
        && std::string_view(ts_node_type(arg)) == "tuple") {
      // Only the named-arg form is a type literal — `u8(3)` stays a
      // constructor cast. Require ≥1 argument, all `max=`/`min=`/`bits=`.
      uint32_t nn       = ts_node_named_child_count(arg);
      bool     all_size = nn >= 1;
      for (uint32_t i = 0; i < nn && all_size; i++) {
        TSNode           item = ts_node_named_child(arg, i);
        std::string_view it(ts_node_type(item));
        if (it != "assignment" && it != "attribute_assignment") {
          all_size = false;
          break;
        }
        TSNode lv = child_by_field(item, "lvalue");
        if (ts_node_is_null(lv)) {
          all_size = false;
          break;
        }
        auto key = trim(get_text(lv));
        all_size = (key == "max" || key == "min" || key == "bits");
      }
      std::string max_txt;
      std::string min_txt;
      if (all_size && int_type_call_bounds(trim(get_text(fn)), arg, max_txt, min_txt)) {
        // Integer type-call (`int(max=…)`): declare a type tmp carrying the
        // canonical prim_type_int(max,min); the fold reads it back through
        // the attributes Type_info seam exactly like a declared variable.
        auto didx = builder.add_child(Lnast_ntype::create_declare());
        auto ref  = builder.mint_tmp_ref();
        lnast->add_child(didx, ref);
        auto pt = lnast->add_child(didx, Lnast_ntype::create_prim_type_int());
        lnast->add_child(pt, Lnast_node::create_const(max_txt.empty() ? "nil" : max_txt));
        lnast->add_child(pt, Lnast_node::create_const(min_txt.empty() ? "nil" : min_txt));
        lnast->add_child(didx, Lnast_node::create_const("type"));
        return ref;
      }
    }
  }
  return expr_to_node(n);
}

bool Prp2lnast::emit_int_type_call(const Lnast_node& target, TSNode type_cast_node) {
  // Recognize the integer type-call `int(max=,min=,bits=)` / `uint(bits=)` /
  // `uN(min=…)`. Because `int`/`uint`/`uN` are keyword tokens, the grammar
  // cannot match the parenthesized form as `function_call_type`; it surfaces
  // as an ERROR node wrapping the keyword (uint_type/sint_type) followed by a
  // bare `(…)` tuple in the `type` field. Reconstruct the intended range and
  // emit `type_spec(target, prim_type_int(max, min))`. "nil" marks an
  // unbounded bound (e.g. `int(max=3)` pins only max).
  TSNode   prim      = {};
  bool     have_prim = false;
  uint32_t total     = ts_node_child_count(type_cast_node);
  for (uint32_t i = 0; i < total && !have_prim; i++) {
    TSNode c = ts_node_child(type_cast_node, i);
    if (std::string_view(ts_node_type(c)) != "ERROR") {
      continue;
    }
    uint32_t en = ts_node_child_count(c);
    for (uint32_t j = 0; j < en; j++) {
      TSNode           ec = ts_node_child(c, j);
      std::string_view et(ts_node_type(ec));
      if (et == "uint_type" || et == "sint_type") {
        prim      = ec;
        have_prim = true;
        break;
      }
    }
  }
  if (!have_prim) {
    return false;
  }
  // The argument tuple lives in the `type` field (expression_type → tuple, or
  // the tuple directly).
  TSNode ty = child_by_field(type_cast_node, "type");
  if (ts_node_is_null(ty)) {
    return false;
  }
  TSNode tup      = {};
  bool   have_tup = false;
  if (std::string_view(ts_node_type(ty)) == "tuple") {
    tup      = ty;
    have_tup = true;
  } else {
    uint32_t nn = ts_node_named_child_count(ty);
    for (uint32_t i = 0; i < nn && !have_tup; i++) {
      TSNode inner = ts_node_named_child(ty, i);
      if (std::string_view(ts_node_type(inner)) == "tuple") {
        tup      = inner;
        have_tup = true;
      }
    }
  }
  if (!have_tup) {
    return false;
  }
  std::string max_txt;
  std::string min_txt;
  if (!int_type_call_bounds(trim(get_text(prim)), tup, max_txt, min_txt)) {
    return false;
  }
  auto ts_idx = builder.add_child(Lnast_ntype::create_type_spec());
  lnast->add_child(ts_idx, target);
  auto pt = lnast->add_child(ts_idx, Lnast_ntype::create_prim_type_int());
  lnast->add_child(pt, Lnast_node::create_const(max_txt.empty() ? "nil" : max_txt));
  lnast->add_child(pt, Lnast_node::create_const(min_txt.empty() ? "nil" : min_txt));
  return true;
}

void Prp2lnast::emit_type_spec(const Lnast_node& target, TSNode type_cast_node) {
  // type_cast: ':' + (type + optional attribute | attribute).
  // Same field-vs-anonymous-seq quirk as `type_specification_to_node`: the
  // `attribute` field maps to a wrapping seq whose first child is `:`, so
  // walk children to find the actual attribute_list.
  //
  // Task 1t — first try the integer type-call form (`int(max=3)`); when it
  // matches it emits the canonical prim_type_int and we skip straight to the
  // attribute carriers.
  TSNode ty = emit_int_type_call(target, type_cast_node) ? TSNode{} : child_by_field(type_cast_node, "type");
  if (!ts_node_is_null(ty)) {
    // Tuple-shape type with default values (e.g. `:(x=0, y=1)`) is the only
    // way to declare a named-position tuple in Pyrope. Lower the inner
    // tuple as a normal `tuple_add` and assign it to the target so the
    // bundle starts with `x=0, y=1` named keys; subsequent positional
    // assignments use upass's shape-preserving merge to keep those names.
    // The bit-width primitive types are still ignored at the value-folding
    // layer — this only handles the shape carried by the tuple type.
    std::string_view tt(ts_node_type(ty));
    if (tt == "expression_type") {
      uint32_t inner_count = ts_node_named_child_count(ty);
      for (uint32_t i = 0; i < inner_count; i++) {
        TSNode inner = ts_node_named_child(ty, i);
        if (std::string_view(ts_node_type(inner)) == "tuple") {
          auto bundle_ref = tuple_to_node(inner, false);
          auto aidx       = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, target);
          lnast->add_child(aidx, bundle_ref);
          // Skip the empty `type_spec expr_type:` we'd otherwise emit; the
          // tuple-shape information is now encoded in the target's bundle.
          goto attrs;
        }
      }
    }
    {
      auto idx = builder.add_child(Lnast_ntype::create_type_spec());
      lnast->add_child(idx, target);
      emit_type_expr(idx, ty);
    }
    // Preserve the typename when the type is an identifier-based (named)
    // type so `target.[typename]` reads can resolve at the attribute pass.
    if (tt == "expression_type" || tt == "dot_expression_type" || tt == "function_call_type") {
      auto raw = trim(get_text(ty));
      if (!raw.empty()) {
        auto as_idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(as_idx, target);
        lnast->add_child(as_idx, Lnast_node::create_const("typename"));
        std::string quoted;
        quoted.reserve(raw.size() + 2);
        quoted.push_back('\'');
        quoted.append(raw);
        quoted.push_back('\'');
        lnast->add_child(as_idx, Lnast_node::create_const(quoted));
      }
    }
  }
attrs:
  uint32_t total = ts_node_child_count(type_cast_node);
  for (uint32_t i = 0; i < total; i++) {
    TSNode           c     = ts_node_child(type_cast_node, i);
    const char*      fname = ts_node_field_name_for_child(type_cast_node, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "attribute_list") {
      emit_attribute_list(target, c);
    } else if ((ct == "tuple_sq" || ct == "attribute_sq") && fname && std::string_view(fname) == "attribute") {
      emit_attribute_list(target, c);
    }
  }
}

void Prp2lnast::emit_type_expr(const Lnast_nid& parent, TSNode type_node) {
  std::string_view t(ts_node_type(type_node));
  if (t == "uint_type" || t == "sint_type") {
    // Task 1t — emit the canonical `prim_type_int(max, min)`. The width sugar
    // `uN`/`sN`/`iN` computes its bounds; the unsized spellings (`uint`,
    // `unsigned`, `int`, `integer`) leave both bounds "nil" (unbounded). "nil"
    // marks an unbounded bound (a non-integer the consumers read as unset).
    const bool  is_signed = (t == "sint_type");
    std::string txt(trim(get_text(type_node)));
    std::string max_txt = "nil";
    std::string min_txt = "nil";
    if (!txt.empty() && std::isdigit(static_cast<unsigned char>(txt.back()))) {
      // First char is u/s/i; the remainder is the bit width.
      const int n = std::stoi(txt.substr(1));
      if (is_signed) {
        max_txt = std::string(Dlop::get_mask_value(n - 1)->to_pyrope());
        min_txt = std::string(Dlop::get_neg_mask_value(n - 1)->to_pyrope());
      } else {
        max_txt = std::string(Dlop::get_mask_value(n)->to_pyrope());
        min_txt = "0";
      }
    }
    auto idx = lnast->add_child(parent, Lnast_ntype::create_prim_type_int());
    lnast->add_child(idx, Lnast_node::create_const(max_txt));
    lnast->add_child(idx, Lnast_node::create_const(min_txt));
  } else if (t == "bool_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_bool());
  } else if (t == "string_type") {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_string());
  } else if (t == "array_type") {
    auto   idx  = lnast->add_child(parent, Lnast_ntype::create_comp_type_array());
    TSNode base = child_by_field(type_node, "base");
    if (!ts_node_is_null(base)) {
      emit_type_expr(idx, base);
    }
    TSNode len = child_by_field(type_node, "length");
    if (!ts_node_is_null(len)) {
      lnast->add_child(idx, expr_to_node(len));
    }
  } else if (t == "expression_type" || t == "dot_expression_type" || t == "function_call_type") {
    // Task 1t — a named type is a `ref` to its symbol-table binding (resolved
    // via that name's bundle); the typename is still tracked by the separate
    // `attr_set(typename,…)` emitted in emit_type_spec. Replaces expr_type.
    lnast->add_child(parent, Lnast_node::create_ref(trim(get_text(type_node))));
  } else {
    lnast->add_child(parent, Lnast_ntype::create_prim_type_none());
  }
}

void Prp2lnast::reject_common_mistakes_attr_name(TSNode node, std::string_view name, bool has_value) const {
  // Unknown attribute names are normally pass-through (user extensibility —
  // attributes_spec.md §1.D), so this only errors on a curated list of
  // popular mistakes plus singular/plural near-misses of the built-in
  // vocabulary. Anything else is silently accepted as a user attribute.

  // Pyrope-source attribute vocabulary. Kept in sync with
  // uPass_attributes::is_builtin_attr (inou/prp cannot depend on upass);
  // `ubits`/`sbits` are added because attribute reads support them.
  static constexpr std::string_view known_attrs[] = {
      // Category A — LNAST/upass attrs
      "max", "min", "bits", "ubits", "sbits", "wrap", "saturate", "sat", "comptime", "const", "mut", "typename", "private",
      "size", "sign", "key", "crand", "rand", "loc", "file", "type",
      // Category B — LGraph wiring attrs
      "clock", "reset", "debug", "_debug", "async", "init", "clock_pin", "din", "enable", "negreset", "posclk", "reset_pin",
      "valid", "stop", "lat", "num", "addr", "fwd", "wensize", "rdport", "defer", "inputs", "outputs",
      // Category C — synthesis hints
      "critical", "delay", "donttouch", "keep", "inp_delay", "out_delay", "max_delay", "min_delay", "max_load", "max_fanout",
      "max_cap", "left_of", "right_of", "top_of", "bottom_of", "align_with",
  };
  auto is_known = [](std::string_view n) {
    return std::find(std::begin(known_attrs), std::end(known_attrs), n) != std::end(known_attrs);
  };

  if (is_known(name)) {
    // `clock`/`reset` are valid only as flag-only signal classifications
    // (`clk::[clock]`). With `=value` the user almost surely meant the
    // per-register pin override.
    if (has_value && (name == "clock" || name == "reset")) {
      report_error(node,
                   "attr-name-mistake",
                   "syntax",
                   std::format("attribute `{}` does not take a value — it marks a declared signal as a {}", name, name),
                   std::format("to pick a register's {0} use `{0}_pin=ref <wire>`", name));
    }
    return;
  }

  // Popular renames — habits from Verilog/other HDLs or the LGraph pin names.
  struct Mistake {
    std::string_view wrong;
    std::string_view hint;
  };
  static constexpr Mistake mistakes[] = {
      {"initial", "use `init`"},  // LGraph's Flop pin is `initial`; the Pyrope attribute is `init`
      {"clk", "use `clock_pin=ref <wire>` to pick a register's clock, or `clock` to classify a signal (`clk::[clock]`)"},
      {"rst", "use `reset_pin=ref <wire>` to pick a register's reset, or `reset` to classify a signal (`rst::[reset]`)"},
      {"width", "use `bits` to read a width (`x.[bits]`); to set a width declare a type, e.g. `:u8`"},
      {"en", "use `enable`"},
      {"posedge", "use `posclk` (`posclk=true`)"},
      {"negedge", "use `posclk=false`"},
      {"signed", "signedness is derived from the type — declare `:sN` (read it with `x.[sign]`)"},
      {"unsigned", "signedness is derived from the type — declare `:uN` (read it with `x.[sign]`)"},
  };
  for (const auto& m : mistakes) {
    if (name == m.wrong) {
      report_error(node,
                   "attr-name-mistake",
                   "syntax",
                   std::format("`{}` is not a Pyrope attribute name", name),
                   m.hint);
    }
  }

  // Singular/plural near-miss of a built-in name (`bit`→`bits`, `keys`→`key`).
  if (is_known(std::string(name) + "s")) {
    report_error(node,
                 "attr-name-mistake",
                 "syntax",
                 std::format("`{}` is not a Pyrope attribute name", name),
                 std::format("did you mean `{}s`?", name));
  }
  if (name.size() > 1 && name.back() == 's' && is_known(name.substr(0, name.size() - 1))) {
    report_error(node,
                 "attr-name-mistake",
                 "syntax",
                 std::format("`{}` is not a Pyrope attribute name", name),
                 std::format("did you mean `{}`?", name.substr(0, name.size() - 1)));
  }
}

void Prp2lnast::emit_arg_assign(const Lnast_nid& tuple_parent, TSNode typed_ident, TSNode definition_or_null, bool is_ref_mod,
                                std::vector<Param_attr>* attrs_out, std::string_view lambda_kind, bool is_io_output,
                                bool is_vararg_mod) {
  TSNode id = child_by_field(typed_ident, "identifier");
  if (ts_node_is_null(id)) {
    return;
  }
  auto aidx = lnast->add_child(tuple_parent, Lnast_ntype::create_store());
  lnast->add_child(aidx, Lnast_node::create_ref(get_text(id)));
  // Default-value slot. Encoding choice for the absent case:
  //   `...` mod          -> const "..."  (Task 1p var-args marker)
  //   `ref` mod          -> const "ref"  (write-back marker for the inliner)
  //   no default, no mod -> const "nil"
  //   explicit default   -> expr_to_node(definition)
  // expr_to_node may emit tmp statements; for literal defaults (the common
  // case) it returns a const node and has no side effect. `...`/`ref`/`reg`
  // are mutually exclusive in the grammar's `mod` field, so the markers never
  // collide. A var-arg never carries a default — the `...` marker WINS over any
  // (meaningless) `= expr` so downstream var-arg detection (which keys on the
  // sentinel) is never silently lost if the grammar admits `...args = e`.
  Lnast_node arg_val = is_vararg_mod                          ? Lnast_node::create_const("...")
                       : !ts_node_is_null(definition_or_null) ? expr_to_node(definition_or_null)
                       : is_ref_mod                           ? Lnast_node::create_const("ref")
                                                              : Lnast_node::create_const("nil");
  lnast->add_child(aidx, arg_val);
  // Optional type subtree (3rd child).
  TSNode type_cast = child_by_field(typed_ident, "type");
  if (!ts_node_is_null(type_cast)) {
    TSNode ty = child_by_field(type_cast, "type");
    if (!ts_node_is_null(ty)) {
      // A literal default must match the declared scalar kind, exactly as a
      // variable declaration `mut b:bool = 3` would (shared check).
      check_decl_init_kind(trim(get_text(id)), arg_val, ty, type_cast);
      emit_arg_type(aidx, ty);
    }
    // Parameter-side attribute carrier (`a::[comptime]`, `a:T::[debug=2]`).
    // attribute_sq hangs off the type_cast with field "attribute"; collect
    // (key, value) pairs into the out-vector. The lambda-def driver replays
    // them as `attr_set` (+ a `cassert` for `comptime`) at body entry.
    if (attrs_out) {
      uint32_t total = ts_node_child_count(type_cast);
      for (uint32_t i = 0; i < total; i++) {
        TSNode           c     = ts_node_child(type_cast, i);
        const char*      fname = ts_node_field_name_for_child(type_cast, i);
        std::string_view ct(ts_node_type(c));
        if (ct != "attribute_sq" && ct != "tuple_sq") {
          continue;
        }
        if (!fname || std::string_view(fname) != "attribute") {
          continue;
        }
        uint32_t nnc = ts_node_named_child_count(c);
        for (uint32_t k = 0; k < nnc; ++k) {
          TSNode           item = ts_node_named_child(c, k);
          std::string_view it(ts_node_type(item));
          if (it == "assignment" || it == "attribute_assignment") {
            TSNode lv = child_by_field(item, "lvalue");
            TSNode rv = child_by_field(item, "rvalue");
            if (ts_node_is_null(lv)) {
              continue;
            }
            std::string_view key_txt;
            std::string_view lvt(ts_node_type(lv));
            if (lvt == "typed_identifier") {
              TSNode iid = child_by_field(lv, "identifier");
              if (!ts_node_is_null(iid)) {
                key_txt = trim(get_text(iid));
              }
            } else {
              key_txt = trim(get_text(lv));
            }
            if (key_txt.empty()) {
              continue;
            }
            std::string val_txt = ts_node_is_null(rv) ? std::string{} : std::string(trim(get_text(rv)));
            reject_common_mistakes_attr_name(lv, key_txt, !ts_node_is_null(rv));
            attrs_out->push_back({std::string(get_text(id)), std::string(key_txt), std::move(val_txt)});
          } else if (it == "identifier" || it == "ref_identifier") {
            auto kt = trim(get_text(item));
            if (kt.empty()) {
              continue;
            }
            reject_common_mistakes_attr_name(item, kt, false);
            attrs_out->push_back({std::string(get_text(id)), std::string(kt), std::string{}});
          }
        }
      }
    }
  }

  // Task 1r-A — interface timing contract. Only enforced when called from the
  // lambda io driver (lambda_kind set); tuple-literal contexts skip it.
  if (!lambda_kind.empty()) {
    auto   arg_name = trim(get_text(id));
    // `@[...]` rides the type_cast's `timing` field when a type is present
    // (`x:u8@[2]`), or the typed_identifier's own `timing` field when not
    // (`x@[2]`).
    TSNode timing   = child_by_field(typed_ident, "timing");
    if (ts_node_is_null(timing) && !ts_node_is_null(type_cast)) {
      timing = child_by_field(type_cast, "timing");
    }
    const bool is_mod   = lambda_kind == "mod";
    const bool is_pipe  = lambda_kind == "pipe";

    if (!ts_node_is_null(timing) && !is_io_output) {
      report_error(timing,
                   "io-input-timing",
                   "type",
                   std::format("input '{}' declares a landing cycle: inputs are at cycle 0 by definition", arg_name),
                   "drop the `@[...]` from the input");
    }
    if (!ts_node_is_null(timing) && is_io_output && !is_mod) {
      report_error(
          timing,
          "io-output-timing",
          "type",
          std::format("{} output '{}' declares a landing cycle: per-output cycles are a `mod` feature", lambda_kind, arg_name),
          is_pipe ? "a pipe's uniform latency is declared on the keyword (`pipe[N]`); drop the `@[...]`"
                  : "drop the `@[...]` (these outputs land at cycle 0 by definition)");
    }
    // Task 1p — an untyped non-`self` `pipe`/`mod` input/output is no longer a
    // definition-time error: the lambda becomes a deferred TEMPLATE (no LGraph
    // until a call site supplies the concrete types). func_extract stamps the
    // template flag; the specializer mints a concrete module per call signature
    // (an untyped *actual* into such a port is the call-site error instead).
    // The deleted check was `(is_mod || is_pipe) && !has_type && arg_name !=
    // "self"` → "lambda-needs-types".
    if (is_mod && is_io_output) {
      if (ts_node_is_null(timing)) {
        report_error(
            id,
            "mod-output-needs-cycle",
            "type",
            std::format("mod output '{}' declares no landing cycle: every mod output carries its cycle at the interface", arg_name),
            "declare it as `name:T@[N]` (N=0 is a combinational feedthrough), or opt out with `name:T@[]`");
      }
      // Stamp stages(min,max) as the TRAILING child of this io store (after
      // the optional type subtree; downstream identifies it by ntype, never
      // by position — the same convention as the pipe stamping in
      // process_lambda_statement_named). `@[]` keeps the slot present with
      // min/max = nil (unconstrained — the form foreign Verilog modules,
      // which carry no markings, ingest as).
      std::string min_txt = "nil";
      TSNode      range_n = child_by_field(timing, "range");
      if (!ts_node_is_null(range_n)) {
        report_error(range_n,
                     "invalid-mod-cycle",
                     "type",
                     std::format("mod output '{}' declares a cycle range: an output lands at a single cycle", arg_name),
                     "write `@[N]` with a literal N, or opt out with `@[]`");
      }
      TSNode index_n = child_by_field(timing, "index");
      if (!ts_node_is_null(index_n)) {
        std::optional<int64_t> lit = resolve_cycle_value(index_n);
        if (!lit) {
          report_error(index_n,
                       "invalid-mod-cycle",
                       "type",
                       std::format("mod output '{}' must declare a single landing cycle (a literal or compile-time constant)",
                                   arg_name),
                       "write `@[N]` with a literal N (or a `const` resolvable to one), or opt out with `@[]`");
        }
        if (*lit < 0) {
          report_error(index_n,
                       "invalid-mod-cycle",
                       "type",
                       std::format("invalid landing cycle {} for mod output '{}': cycles count from the inputs and start at 0",
                                   *lit,
                                   arg_name),
                       "use `@[0]` for a combinational feedthrough output");
        }
        constexpr int64_t k_max_cycle = std::numeric_limits<int32_t>::max();
        if (*lit > k_max_cycle) {
          report_error(
              index_n,
              "invalid-mod-cycle",
              "type",
              std::format("invalid landing cycle {} for mod output '{}': exceeds the maximum of {}", *lit, arg_name, k_max_cycle),
              "use a realistic cycle count");
        }
        min_txt = std::to_string(*lit);
      }
      auto st = lnast->add_child(aidx, Lnast_ntype::create_stages());
      lnast->add_child(st, Lnast_node::create_const(min_txt));
      lnast->add_child(st, Lnast_node::create_const(min_txt));
    }
  }
}

void Prp2lnast::emit_arg_type(const Lnast_nid& assign_parent, TSNode type_node) {
  // Composite tuple type `(name:T, name2:U, ...)` lowers to a `tuple_add` whose
  // children mirror the inputs/outputs shape: each field is a recursive
  // `assign(ref, default-or-nil, type?)`. Pyrope parses each `name:T` field
  // as a `type_specification` (not a `typed_identifier`) because tuple-item
  // expressions reach the `type_specification` rule via `_restricted_expression`.
  // For `name:T = default`, the field is an `assignment` whose lvalue is a
  // `typed_identifier`.
  std::string_view t(ts_node_type(type_node));
  if (t == "expression_type") {
    uint32_t nnc = ts_node_named_child_count(type_node);
    for (uint32_t i = 0; i < nnc; i++) {
      TSNode inner = ts_node_named_child(type_node, i);
      if (std::string_view(ts_node_type(inner)) != "tuple") {
        continue;
      }
      auto     tup_idx = lnast->add_child(assign_parent, Lnast_ntype::create_tuple_add());
      uint32_t tnc     = ts_node_named_child_count(inner);
      for (uint32_t j = 0; j < tnc; j++) {
        TSNode           item = ts_node_named_child(inner, j);
        std::string_view it(ts_node_type(item));
        if (it == "typed_identifier") {
          emit_arg_assign(tup_idx, item, TSNode{}, /*is_ref_mod=*/false);
        } else if (it == "type_specification") {
          // (argument: identifier, type: <type>). Emit an arg-shape assign
          // directly: ref(argument), const "nil", type-subtree.
          TSNode arg = child_by_field(item, "argument");
          TSNode ty  = child_by_field(item, "type");
          if (ts_node_is_null(arg)) {
            continue;
          }
          auto aidx = lnast->add_child(tup_idx, Lnast_ntype::create_store());
          lnast->add_child(aidx, Lnast_node::create_ref(trim(get_text(arg))));
          lnast->add_child(aidx, Lnast_node::create_const("nil"));
          if (!ts_node_is_null(ty)) {
            emit_arg_type(aidx, ty);
          }
        } else if (it == "assignment") {
          // `name:T = default` field. Lvalue can be typed_identifier or a
          // bare identifier with optional type. Rvalue is the default.
          TSNode lv = child_by_field(item, "lvalue");
          TSNode rv = child_by_field(item, "rvalue");
          if (ts_node_is_null(lv)) {
            continue;
          }
          if (std::string_view(ts_node_type(lv)) == "typed_identifier") {
            emit_arg_assign(tup_idx, lv, rv, /*is_ref_mod=*/false);
          }
        }
      }
      return;
    }
  }
  emit_type_expr(assign_parent, type_node);
}

void Prp2lnast::emit_attribute_list(const Lnast_node& target, TSNode attr_list_node) {
  // Two shapes feed this function:
  //   1. (legacy / read-side) `attribute_list` node: `[name (= value)?, ...]`.
  //      The grammar wraps each item in an anonymous `seq(name, value?)`, so
  //      `name` and `value` field tags appear directly on the parent.
  //   2. (new write-side) `tuple_sq` node holding `[key=value, ...]`. Each
  //      item is a named child — either `assignment` (key=value) or a bare
  //      identifier (flag-only, value defaults to `true`).
  std::string_view nt(ts_node_type(attr_list_node));
  // `tuple_sq` (legacy) and `attribute_sq` (current grammar, commit 93ca079+)
  // share the same item layout — only the item-node type names differ.
  // tuple_sq holds `assignment` items (lvalue may be `typed_identifier`);
  // attribute_sq holds `attribute_assignment` items (lvalue is a plain
  // `identifier`). Both also accept bare identifier/ref_identifier flags.
  if (nt == "tuple_sq" || nt == "attribute_sq") {
    uint32_t nnc = ts_node_named_child_count(attr_list_node);
    for (uint32_t i = 0; i < nnc; ++i) {
      TSNode           item = ts_node_named_child(attr_list_node, i);
      std::string_view it(ts_node_type(item));
      if (it == "assignment" || it == "attribute_assignment") {
        TSNode lv = child_by_field(item, "lvalue");
        TSNode rv = child_by_field(item, "rvalue");
        if (ts_node_is_null(lv)) {
          continue;
        }
        // Strip an optional `:Type` wrapper on the attribute key (only
        // applies to legacy `assignment` items; `attribute_assignment`
        // already has a bare identifier lvalue).
        std::string_view key_txt;
        std::string_view lvt(ts_node_type(lv));
        if (lvt == "typed_identifier") {
          TSNode id = child_by_field(lv, "identifier");
          if (!ts_node_is_null(id)) {
            key_txt = trim(get_text(id));
          }
        } else {
          key_txt = trim(get_text(lv));
        }
        if (key_txt.empty()) {
          continue;
        }
        reject_common_mistakes_attr_name(lv, key_txt, !ts_node_is_null(rv));
        auto idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(idx, target);
        lnast->add_child(idx, Lnast_node::create_const(key_txt));
        if (!ts_node_is_null(rv)) {
          lnast->add_child(idx, expr_to_node(rv));
        } else {
          lnast->add_child(idx, Lnast_node::create_const("true"));
        }
        attach_loc(idx, item);  // span → semacheck's read-only-attr-write can point here
      } else if (it == "identifier" || it == "ref_identifier") {
        auto txt = trim(get_text(item));
        if (txt.empty()) {
          continue;
        }
        reject_common_mistakes_attr_name(item, txt, false);
        auto idx = builder.add_child(Lnast_ntype::create_attr_set());
        lnast->add_child(idx, target);
        lnast->add_child(idx, Lnast_node::create_const(txt));
        lnast->add_child(idx, Lnast_node::create_const("true"));
        attach_loc(idx, item);
      }
    }
    return;
  }
  // attribute_list: '[' (name (= value)?)*  ']'.
  // The grammar's `field('item', seq(field('name', ...), field('value', ...)))`
  // wraps an anonymous seq, so each item's inner field names appear directly
  // on the attribute_list. Walk children in source order and pair each
  // `name` with the immediately-following `value` field (if any).
  uint32_t total = ts_node_child_count(attr_list_node);
  for (uint32_t i = 0; i < total; i++) {
    const char* fname = ts_node_field_name_for_child(attr_list_node, i);
    if (!fname || std::string_view(fname) != "name") {
      continue;
    }
    TSNode name_n = ts_node_child(attr_list_node, i);
    // Look ahead for an optional `value` field that pairs with this name.
    // Stop at the next `name` field — that begins a new item. The grammar
    // wraps the value in an anonymous `seq('=', expr)` and applies
    // `field('value', ...)` to the seq, so the field tag covers BOTH the
    // `=` token and the expression child. Skip the `=` token and pick the
    // first named child carrying the `value` field.
    TSNode val_n;
    bool   have_val = false;
    for (uint32_t k = i + 1; k < total; k++) {
      const char* knm = ts_node_field_name_for_child(attr_list_node, k);
      if (!knm) {
        continue;
      }
      std::string_view knms(knm);
      if (knms == "name") {
        break;
      }
      if (knms == "value") {
        TSNode kc = ts_node_child(attr_list_node, k);
        if (!ts_node_is_named(kc)) {
          continue;  // `=` token; the real value is the next field-tagged child
        }
        val_n    = kc;
        have_val = true;
        break;
      }
    }
    reject_common_mistakes_attr_name(name_n, get_text(name_n), have_val);
    auto idx = builder.add_child(Lnast_ntype::create_attr_set());
    lnast->add_child(idx, target);
    lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
    if (have_val) {
      lnast->add_child(idx, expr_to_node(val_n));
    } else {
      lnast->add_child(idx, Lnast_node::create_const("true"));
    }
    attach_loc(idx, name_n);  // span → semacheck's read-only-attr-write can point here
  }
}

// Lower an `expression_item` (flat operand/operator chain, one per grammar
// priority tier). Four operator classes:
//
//   binary_times_op   *  /  %                                   (tier 2)
//   binary_other_op   +  -  ++  <<  >>  &  |  ^  !&  !|  !^
//                     ..=  ..<  ..+  step                       (tier 3)
//   binary_compare_op <  <=  >  >=  ==  !=
//                     has !has  in !in  case !case  does !does
//                     is  !is   equals !equals                  (tier 4)
//   binary_logical_op and !and  or !or  implies !implies        (tier 5)
//
// Compare chains (`a == b == c`) lower as Python-style
// `(a op0 b) and (b op1 c)` — one `expression_item` may hold multiple
// compare operators. Logical chains left-fold, but mixing distinct
// logical operators (`a and b or c`) is an ambiguity we flag rather than
// silently pick an associativity.
Lnast_node Prp2lnast::binary_expr_to_node(TSNode n) {
  uint32_t nnc = ts_node_named_child_count(n);
  if (nnc == 0) {
    return builder.mint_tmp_ref();
  }

  // Tier of a binary_*_op wrapper. Drives chaining behavior (compare
  // chains lower as Python-style `(a op b) and (b op c)`; the rest left-fold).
  enum class Tier : uint8_t { times, other, compare, logical, unknown };

  // Collect operands and operator kinds in source order. Each operator is the
  // inner aliased child of the binary_*_op wrapper (e.g. `op_add`, `op_eq`).
  // We carry both the inner kind (for op-specific dispatch) and the wrapper's
  // tier (for chain-vs-fold dispatch).
  struct Op {
    Tier             tier = Tier::unknown;
    std::string_view kind;    // points into ts_node_type's static string for the inner aliased op
    TSNode           node{};  // the binary_*_op wrapper node, for diagnostic spans
  };
  std::vector<Lnast_node> operands;
  std::vector<TSNode>     operand_nodes;  // parallel to `operands`, source TSNodes (for precedence checks)
  std::vector<Op>         ops;
  operands.reserve(nnc / 2 + 1);
  operand_nodes.reserve(nnc / 2 + 1);
  ops.reserve(nnc / 2);
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view ct(ts_node_type(c));
    Tier             tier = Tier::unknown;
    if (ct == "binary_times_op") {
      tier = Tier::times;
    } else if (ct == "binary_other_op") {
      tier = Tier::other;
    } else if (ct == "binary_compare_op") {
      tier = Tier::compare;
    } else if (ct == "binary_logical_op") {
      tier = Tier::logical;
    }
    if (tier != Tier::unknown) {
      // The wrapper has a single named child of the aliased op kind.
      TSNode inner = ts_node_named_child(c, 0);
      ops.push_back({tier, ts_node_is_null(inner) ? std::string_view{} : std::string_view(ts_node_type(inner)), c});
    } else {
      operand_nodes.push_back(c);
    }
  }

  // Convert operands AFTER the op kinds are known: an operand adjacent to a
  // `does`/`equals`/`case` operator is in type position (1g) — a primitive
  // type token there is a type literal, not a variable read. Conversion stays
  // in source order, so helper-stmt emission order is unchanged.
  auto is_type_pos_op = [](std::string_view k) {
    return k == "op_does" || k == "op_not_does" || k == "op_case" || k == "op_not_case" || k == "op_equals"
           || k == "op_not_equals";
  };
  for (size_t j = 0; j < operand_nodes.size(); ++j) {
    const bool type_pos = (j > 0 && j - 1 < ops.size() && is_type_pos_op(ops[j - 1].kind))
                          || (j < ops.size() && is_type_pos_op(ops[j].kind));
    operands.push_back(type_pos ? does_operand_to_node(operand_nodes[j]) : expr_to_node(operand_nodes[j]));
  }

  if (ops.empty()) {
    return operands.front();
  }
  I(operands.size() == ops.size() + 1);

  // --- Precedence-mixing check (docs/docs/pyrope/04-variables.md "Precedence").
  // Pyrope has very shallow precedence: parentheses may be omitted only when an
  // expression evaluated left-to-right gives the same result as right-to-left.
  // The grammar flattens each priority tier into one chain, so the operator
  // structure that proves (or disproves) order-independence survives only here —
  // lowering below collapses it into nested tmp refs. Flag the ambiguous mixes
  // now, while we still can.
  //
  //   tier 3 "other" (+ - ++ << >> & | ^ !& !| !^ ..= ..< ..+ step):
  //     distinct operators may be mixed only when they are all additive (+,-)
  //     or all the same associative operator (& | ^ ++). In addition, a bare
  //     mult/div operand (`a*b`, not the parenthesized `(a*b)`) may sit only
  //     next to + or - ("mult/div precedence is only against +,- operators").
  //   tier 5 "logical" (and or implies): distinct operators may not be mixed.
  {
    auto is_additive = [](std::string_view k) { return k == "op_add" || k == "op_sub"; };
    auto is_assoc_other
        = [](std::string_view k) { return k == "op_bit_and" || k == "op_bit_or" || k == "op_bit_xor" || k == "op_tuple_concat"; };
    // An unparenthesized priority-2 (`*` `/` `%`) operand. `(a*b)` parses as a
    // `tuple`, not an `expression_item`, so parentheses make this return false.
    auto is_bare_times = [](TSNode c) {
      if (std::string_view(ts_node_type(c)) != "expression_item") {
        return false;
      }
      uint32_t cc = ts_node_named_child_count(c);
      for (uint32_t i = 0; i < cc; ++i) {
        if (std::string_view(ts_node_type(ts_node_named_child(c, i))) == "binary_times_op") {
          return true;
        }
      }
      return false;
    };

    const Tier tier = ops.front().tier;  // a chain is homogeneous: one grammar tier
    if (tier == Tier::other) {
      // mult/div precedence is defined only against +/-.
      for (size_t i = 0; i < operand_nodes.size(); ++i) {
        if (!is_bare_times(operand_nodes[i])) {
          continue;
        }
        bool left_bad  = i > 0 && !is_additive(ops[i - 1].kind);
        bool right_bad = i < ops.size() && !is_additive(ops[i].kind);
        if (left_bad || right_bad) {
          report_error(left_bad ? ops[i - 1].node : ops[i].node,
                       "mixed-precedence",
                       "syntax",
                       "mult/div precedence is defined only against `+`/`-`: add parentheses around the "
                       "`*`/`/` sub-expression to make the precedence explicit",
                       "wrap the multiply/divide in parentheses, e.g. `a & (b * c)`");
        }
      }
      // Distinct same-tier operators only mix when all additive or all the same
      // associative operator.
      if (ops.size() >= 2) {
        bool all_additive = true;
        bool all_same     = true;
        for (const auto& o : ops) {
          all_additive = all_additive && is_additive(o.kind);
          all_same     = all_same && (o.kind == ops.front().kind);
        }
        if (!all_additive && !(all_same && is_assoc_other(ops.front().kind))) {
          // Blame the first operator that breaks homogeneity (or the second
          // operator for a repeated non-associative one like `<<`).
          size_t blame = 1;
          for (size_t i = 1; i < ops.size(); ++i) {
            if (ops[i].kind != ops.front().kind) {
              blame = i;
              break;
            }
          }
          report_error(ops[blame].node,
                       "mixed-precedence",
                       "syntax",
                       "operators at the same precedence cannot be mixed without parentheses — the result "
                       "depends on evaluation order; add parentheses to make the grouping explicit",
                       "add parentheses to group the sub-expressions");
        }
      }
    } else if (tier == Tier::logical && ops.size() >= 2) {
      for (size_t i = 1; i < ops.size(); ++i) {
        if (ops[i].kind != ops.front().kind) {
          report_error(ops[i].node,
                       "mixed-precedence",
                       "syntax",
                       "logical operators `and`/`or`/`implies` at the same precedence cannot be mixed "
                       "without parentheses; add parentheses to make the grouping explicit",
                       "add parentheses, e.g. `(x or y) and z`");
        }
      }
    }
  }

  // Emit `head(ref(tmp), l, r)` and return the tmp ref. Used both for plain
  // binary ops (eq/lt/plus/...) and for typed pseudo-calls (func_has, func_in)
  // — the shape is identical.
  auto make_binop = [&](Lnast_ntype::Lnast_ntype_int head, const Lnast_node& l, const Lnast_node& r) {
    auto idx = builder.add_child(head);
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  // Legacy marker-style call: `func_call(ref(tmp), const(name), l, r)` —
  // still used by ops without a dedicated LNAST ntype (step / implies).
  auto make_call = [&](const char* name, const Lnast_node& l, const Lnast_node& r) {
    auto idx = builder.add_child(Lnast_ntype::create_func_call());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const(name));
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    return ref;
  };
  auto wrap_not = [&](const Lnast_node& inner) {
    auto idx = builder.add_child(Lnast_ntype::create_log_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, inner);
    return ref;
  };

  auto emit_compare = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_eq") {
      return make_binop(Lnast_ntype::create_eq(), l, r);
    }
    if (kind == "op_ne") {
      return make_binop(Lnast_ntype::create_ne(), l, r);
    }
    if (kind == "op_lt") {
      return make_binop(Lnast_ntype::create_lt(), l, r);
    }
    if (kind == "op_le") {
      return make_binop(Lnast_ntype::create_le(), l, r);
    }
    if (kind == "op_gt") {
      return make_binop(Lnast_ntype::create_gt(), l, r);
    }
    if (kind == "op_ge") {
      return make_binop(Lnast_ntype::create_ge(), l, r);
    }
    if (kind == "op_has") {
      return make_binop(Lnast_ntype::create_func_has(), l, r);
    }
    if (kind == "op_not_has") {
      return wrap_not(make_binop(Lnast_ntype::create_func_has(), l, r));
    }
    if (kind == "op_in") {
      return make_binop(Lnast_ntype::create_func_in(), l, r);
    }
    if (kind == "op_not_in") {
      return wrap_not(make_binop(Lnast_ntype::create_func_in(), l, r));
    }
    if (kind == "op_is") {
      return make_binop(Lnast_ntype::create_is(), l, r);
    }
    if (kind == "op_not_is") {
      return wrap_not(make_binop(Lnast_ntype::create_is(), l, r));
    }
    if (kind == "op_does") {
      return make_binop(Lnast_ntype::create_func_does(), l, r);
    }
    if (kind == "op_not_does") {
      return wrap_not(make_binop(Lnast_ntype::create_func_does(), l, r));
    }
    if (kind == "op_case") {
      return make_binop(Lnast_ntype::create_func_case(), l, r);
    }
    if (kind == "op_not_case") {
      return wrap_not(make_binop(Lnast_ntype::create_func_case(), l, r));
    }
    if (kind == "op_equals") {
      return make_binop(Lnast_ntype::create_func_equals(), l, r);
    }
    if (kind == "op_not_equals") {
      return wrap_not(make_binop(Lnast_ntype::create_func_equals(), l, r));
    }
    std::print("prp2lnast: unhandled compare op `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  // Range nodes carry a source span (attach_loc) so constprop's
  // descending-range diagnostic can point at the offending `a..=b`. The
  // operator wrapper TSNode is threaded in via emit_other's `opn`.
  auto make_range = [&](const Lnast_node& l, const Lnast_node& r, const TSNode& opn) {
    auto idx = builder.add_child(Lnast_ntype::create_range());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, l);
    lnast->add_child(idx, r);
    attach_loc(idx, opn);
    return ref;
  };

  auto emit_other = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r, const TSNode& opn) -> Lnast_node {
    if (kind == "op_add") {
      return make_binop(Lnast_ntype::create_plus(), l, r);
    }
    if (kind == "op_sub") {
      return make_binop(Lnast_ntype::create_minus(), l, r);
    }
    if (kind == "op_tuple_concat") {
      return make_binop(Lnast_ntype::create_tuple_concat(), l, r);
    }
    if (kind == "op_shl") {
      return make_binop(Lnast_ntype::create_shl(), l, r);
    }
    if (kind == "op_sra") {
      return make_binop(Lnast_ntype::create_sra(), l, r);
    }
    if (kind == "op_bit_and") {
      return make_binop(Lnast_ntype::create_bit_and(), l, r);
    }
    if (kind == "op_bit_or") {
      return make_binop(Lnast_ntype::create_bit_or(), l, r);
    }
    if (kind == "op_bit_xor") {
      return make_binop(Lnast_ntype::create_bit_xor(), l, r);
    }
    if (kind == "op_bit_nand") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_and(), l, r));
    }
    if (kind == "op_bit_nor") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_or(), l, r));
    }
    if (kind == "op_bit_xnor") {
      return wrap_not(make_binop(Lnast_ntype::create_bit_xor(), l, r));
    }
    if (kind == "op_range_inclusive") {
      return make_range(l, r, opn);
    }
    if (kind == "op_range_exclusive") {
      auto m    = builder.add_child(Lnast_ntype::create_minus());
      auto mref = builder.mint_tmp_ref();
      lnast->add_child(m, mref);
      lnast->add_child(m, r);
      lnast->add_child(m, Lnast_node::create_const("1"));
      return make_range(l, mref, opn);
    }
    if (kind == "op_range_count") {
      auto p    = builder.add_child(Lnast_ntype::create_plus());
      auto pref = builder.mint_tmp_ref();
      lnast->add_child(p, pref);
      lnast->add_child(p, l);
      lnast->add_child(p, r);
      auto mm   = builder.add_child(Lnast_ntype::create_minus());
      auto mref = builder.mint_tmp_ref();
      lnast->add_child(mm, mref);
      lnast->add_child(mm, pref);
      lnast->add_child(mm, Lnast_node::create_const("1"));
      return make_range(l, mref, opn);
    }
    // `step` has no dedicated LNAST op; lower as a two-arg call for now.
    if (kind == "op_step") {
      return make_call("step", l, r);
    }
    std::print("prp2lnast: unhandled `binary_other_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  auto emit_times = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_mul") {
      return make_binop(Lnast_ntype::create_mult(), l, r);
    }
    if (kind == "op_div") {
      return make_binop(Lnast_ntype::create_div(), l, r);
    }
    if (kind == "op_mod") {
      return make_binop(Lnast_ntype::create_mod(), l, r);
    }
    std::print("prp2lnast: unhandled `binary_times_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  auto emit_logical = [&](std::string_view kind, const Lnast_node& l, const Lnast_node& r) -> Lnast_node {
    if (kind == "op_log_and") {
      return make_binop(Lnast_ntype::create_log_and(), l, r);
    }
    if (kind == "op_log_or") {
      return make_binop(Lnast_ntype::create_log_or(), l, r);
    }
    if (kind == "op_log_nand") {
      return wrap_not(make_binop(Lnast_ntype::create_log_and(), l, r));
    }
    if (kind == "op_log_nor") {
      return wrap_not(make_binop(Lnast_ntype::create_log_or(), l, r));
    }
    if (kind == "op_implies") {
      return make_call("implies", l, r);
    }
    if (kind == "op_not_implies") {
      return wrap_not(make_call("implies", l, r));
    }
    std::print("prp2lnast: unhandled `binary_logical_op` `{}`\n", kind);
    return builder.mint_tmp_ref();
  };

  const Tier first_tier = ops.front().tier;

  // Compare tier: Python-style chain. For each adjacent pair emit the
  // corresponding compare; then left-fold `log_and` over the results.
  if (first_tier == Tier::compare) {
    std::vector<Lnast_node> pair_results;
    pair_results.reserve(ops.size());
    for (size_t i = 0; i < ops.size(); ++i) {
      pair_results.push_back(emit_compare(ops[i].kind, operands[i], operands[i + 1]));
    }
    Lnast_node acc = pair_results.front();
    for (size_t i = 1; i < pair_results.size(); ++i) {
      acc = make_binop(Lnast_ntype::create_log_and(), acc, pair_results[i]);
    }
    return acc;
  }

  // Logical-tier mixing (`a and b or c`) was already rejected by the
  // precedence-mixing check above, so the chain here is homogeneous.

  // All other tiers: left-fold.
  Lnast_node acc = operands.front();
  for (size_t i = 0; i < ops.size(); ++i) {
    switch (ops[i].tier) {
      case Tier::times  : acc = emit_times(ops[i].kind, acc, operands[i + 1]); break;
      case Tier::other  : acc = emit_other(ops[i].kind, acc, operands[i + 1], ops[i].node); break;
      case Tier::logical: acc = emit_logical(ops[i].kind, acc, operands[i + 1]); break;
      default           : std::print("prp2lnast: unknown op kind `{}`\n", ops[i].kind); return builder.mint_tmp_ref();
    }
  }
  return acc;
}

Lnast_node Prp2lnast::unary_expr_to_node(TSNode n) {
  TSNode     op_n  = child_by_field(n, "operator");
  TSNode     arg_n = child_by_field(n, "argument");
  Lnast_node arg_ref;
  if (ts_node_is_null(arg_n)) {
    auto start = ts_node_is_null(op_n) ? ts_node_start_byte(n) : ts_node_end_byte(op_n);
    arg_ref    = constant_text_to_node(trim(text_between(start, ts_node_end_byte(n))));
  } else {
    arg_ref = expr_to_node(arg_n);
  }
  // The `operator` field is one of the aliased operator nodes:
  // op_log_not / op_bit_not / op_unary_minus / op_spread.
  std::string_view op_kind = ts_node_is_null(op_n) ? std::string_view{} : std::string_view(ts_node_type(op_n));
  if (op_kind == "op_log_not") {
    auto idx = builder.add_child(Lnast_ntype::create_log_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_kind == "op_bit_not") {
    auto idx = builder.add_child(Lnast_ntype::create_bit_not());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  if (op_kind == "op_unary_minus") {
    auto idx = builder.add_child(Lnast_ntype::create_minus());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("0"));
    lnast->add_child(idx, arg_ref);
    return ref;
  }
  // op_spread: pass-through for now.
  return arg_ref;
}

Lnast_node Prp2lnast::if_expr_to_node(TSNode n, bool need_result) {
  // if init?; cond { code } (elif init?; cond { code })* (else { code })?
  //
  // Order matters: we must evaluate every condition expression *before*
  // adding the `if` LNAST node to builder.idx_stmts. expr_to_node can emit
  // helper statements (e.g. `ne ___2 = i != 2`) into builder.idx_stmts; if the
  // `if` is already there, those helpers land as siblings *after* the
  // `if`, and the resulting LNAST has the `if` reading a tmp ref before
  // the producing ne — a downstream constprop pass that drops the
  // already-folded ne leaves the if's ref dangling. lnastfmt's
  // read-without-write check flags this. By staging cond evaluation first,
  // every helper stmt lands before the `if` and the consumer-after-producer
  // invariant holds.
  //
  // Caveat: this evaluates ALL elif conds eagerly into the surrounding
  // scope, even those that strictly should only run if the prior arm
  // missed. For pyrope comptime cond expressions this is fine (no side
  // effects); a future change could lower elif-conds inside their parent
  // arm's else branch for correctness on side-effecting conds.
  //
  // need_result == false when the if is used as a statement (its value is
  // discarded); skip the per-arm placeholder `assign result = 0` and the
  // result tmp entirely.
  Lnast_node result;
  if (need_result) {
    result = builder.mint_tmp_ref();
  }

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;
  TSNode           else_code{};
  bool             have_else = false;
  bool             init_done = false;

  auto eval_init = [&]() {
    if (init_done) {
      return;
    }
    init_done   = true;
    TSNode init = child_by_field(n, "init");
    if (!ts_node_is_null(init)) {
      uint32_t inc = ts_node_named_child_count(init);
      for (uint32_t i = 0; i < inc; i++) {
        process_statement(ts_node_named_child(init, i));
      }
    }
  };

  uint32_t nc = child_count(n);
  TSNode   pending_cond{};
  bool     have_cond = false;
  bool     in_else   = false;
  for (uint32_t i = 0; i < nc; i++) {
    const char* field_name = ts_node_field_name_for_child(n, i);
    if (!field_name) {
      continue;
    }
    std::string_view f(field_name);
    TSNode           c = child(n, i);
    if (f == "condition") {
      pending_cond = c;
      have_cond    = true;
    } else if (f == "code") {
      if (in_else) {
        else_code = c;
        have_else = true;
        in_else   = false;
      } else if (have_cond) {
        eval_init();
        Lnast_node cref = ts_node_is_null(pending_cond) ? Lnast_node::create_const("true") : expr_to_node(pending_cond);
        arms.push_back({cref, c});
        have_cond = false;
      }
    } else if (f == "else") {
      // Tree-sitter applies field("else", optseq("else", scope_statement))
      // by tagging EVERY child of the optseq with field "else" — so we see
      // two "else"-tagged children: the literal keyword and the body. The
      // keyword has node type "else" (text "else"); the body has type
      // "scope_statement". Capture the latter as the else body.
      std::string_view ct(ts_node_type(c));
      if (ct == "scope_statement") {
        else_code = c;
        have_else = true;
        in_else   = false;
      } else {
        std::string_view txt = get_text(c);
        if (txt == "else") {
          in_else = true;
        }
      }
    }
  }

  // All cond stmts (and init) have now been emitted to builder.idx_stmts. Add the
  // `if` here so it follows its producers in source order.
  auto if_idx = builder.add_child(Lnast_ntype::create_if());
  attach_loc(if_idx, n);  // span → upass/typecheck cond-not-bool can point here

  // Set of named-child node kinds that are expression-typed and can serve
  // as the value of an arm body. Mirrors `expr_stmt` in process_statement.
  static const absl::flat_hash_set<std::string_view> arm_expr_kinds = {
      "if_expression",
      "match_expression",
      "expression_item",
      "unary_expression",
      "bit_selection",
      "member_selection",
      "attribute_read",
      "dot_expression",
      "function_call_expression",
      "identifier",
      "tuple",
      "tuple_sq",
      "paren_group",
      "type_specification",
      "constant",
  };

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    // When the if is used as an expression, the arm body's value is its
    // last expression. Detect a trailing expression-typed named child and
    // emit `assign result <expr>` for it; preceding statements process as
    // normal. Without this, every arm assigns 0 and the if-expression
    // always folds to 0 instead of the body's computed value.
    bool emitted_result = false;
    if (need_result && !ts_node_is_null(code) && std::string_view(ts_node_type(code)) == "scope_statement") {
      uint32_t nnc = ts_node_named_child_count(code);
      if (nnc > 0) {
        TSNode           last = ts_node_named_child(code, nnc - 1);
        std::string_view lt(ts_node_type(last));
        if (arm_expr_kinds.contains(lt)) {
          // Process the leading statements (everything before the last
          // expression) by walking the children in source order, skipping
          // the trailing expression which we will lower as a value.
          for (uint32_t i = 0; i < ts_node_child_count(code); i++) {
            TSNode c = ts_node_child(code, i);
            if (!ts_node_is_named(c)) {
              continue;
            }
            if (ts_node_start_byte(c) == ts_node_start_byte(last) && ts_node_end_byte(c) == ts_node_end_byte(last)) {
              break;  // reached the trailing expression — handle below
            }
            process_statement(c);
          }
          Lnast_node val  = expr_to_node(last);
          auto       aidx = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, result);
          lnast->add_child(aidx, val);
          emitted_result = true;
        }
      }
    }
    if (!emitted_result) {
      if (!ts_node_is_null(code)) {
        process_scope_statement(code, body_idx);
      }
      if (need_result) {
        auto a = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
      }
    }
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (have_else) {
    emit_body(else_code);
  }

  return need_result ? result : Lnast_node::create_const("0");
}

Lnast_node Prp2lnast::match_expr_to_node(TSNode n, bool need_result) {
  // match init?; subject { (operator? expr_list | else) { code } ... }
  // Expand to a chain of unique if/else: each arm's per-pattern compare
  // emits the operator-appropriate node (`eq` for `==` / default,
  // `func_case` for `case`, negated counterparts for `!=` / `!case`).
  // Multi-pattern arms collapse via `log_or`. The `init` clause (e.g.
  // `match const t = m1; t { … }`) is processed *before* the conds so
  // any local binding it introduces is visible to the arm bodies.

  // Mint the `result` ref (when consumed) BEFORE wrapping in the match's
  // private stmts. Tmps live at function scope so they survive the wrap
  // exit, but doing this here also lets the caller reference `result`
  // outside the wrap.
  Lnast_node result_outer;
  if (need_result) {
    result_outer = builder.mint_tmp_ref();
  }

  // Previously the match wrapped its init+compare+if in a private `stmts`
  // scope so the `init` local (e.g. `match const t = m1; t { … }`'s `t`)
  // could not leak after the match exited. That extra Block scope also
  // isolated assignments to outer-scope `mut` vars from propagating after
  // the match (e.g. `hit = …` inside an arm body). The arm bodies already
  // run inside an `if` whose own `stmts` provides phi-merge for the outer
  // vars; we leave the init+compare nodes at the enclosing stmts level so
  // outer-var writes propagate correctly. `t` (or any init local) is
  // released after the match by an explicit `assign t = nil` emitted below.
  std::vector<std::string> init_locals;

  // Init: process every statement in the init list — they declare locals
  // (typical: `const t = subject`) at the enclosing stmts level. Track the
  // bound names so we can release them after the match exits.
  TSNode init = child_by_field(n, "init");
  if (!ts_node_is_null(init)) {
    uint32_t inc = ts_node_named_child_count(init);
    for (uint32_t i = 0; i < inc; i++) {
      TSNode           c = ts_node_named_child(init, i);
      std::string_view ct(ts_node_type(c));
      if (ct == "declaration_statement" || ct == "assignment") {
        TSNode lv = child_by_field(c, "lvalue");
        if (!ts_node_is_null(lv)) {
          std::string_view lvt(ts_node_type(lv));
          TSNode           id;
          if (lvt == "typed_identifier") {
            id = child_by_field(lv, "identifier");
          } else if (lvt == "identifier") {
            id = lv;
          }
          if (!ts_node_is_null(id)) {
            init_locals.emplace_back(get_text(id));
          }
        }
      }
      process_statement(c);
    }
  }

  TSNode subject   = {};
  TSNode else_code = {};
  bool   have_subj = false;
  // Find subject: the first 'condition' field child whose node type is
  // not an op-token or the 'else' keyword (tree-sitter tags every child
  // of the seq with the field name — see if_expr_to_node).
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    if (std::string_view(f) == "condition") {
      subject   = child(n, i);
      have_subj = true;
      break;
    }
  }
  if (!have_subj) {
    return Lnast_node::create_const("0");
  }
  Lnast_node subject_ref = expr_to_node(subject);

  // Same producer-before-consumer reordering as if_expr_to_node: walk every
  // arm first, emit each arm's compare/log_or compute stmts to builder.idx_stmts,
  // collect (cref, code) pairs, THEN add the if and assemble. Otherwise
  // every compare lands as a sibling AFTER the if and the if's cond refs are
  // dangling once constprop drops the helpers.
  // need_result == false when the match is used as a statement (its value is
  // discarded); skip the per-arm placeholder `assign result = 0` and the
  // result tmp entirely. The result tmp itself was minted above before the
  // wrap stmts, so the caller's reference outlives the match scope.
  Lnast_node result = result_outer;

  struct Arm {
    Lnast_node cref;
    TSNode     code;
  };
  std::vector<Arm> arms;

  bool             saw_first_condition = false;
  TSNode           pending_expr        = {};
  bool             have_pending        = false;
  bool             pending_is_else     = false;
  std::string_view pending_op          = "==";  // default per pyrope `match`
  for (uint32_t i = 0; i < child_count(n); i++) {
    const char* f = ts_node_field_name_for_child(n, i);
    if (!f) {
      continue;
    }
    std::string_view field(f);
    TSNode           c = child(n, i);
    if (field == "else_code") {
      // New grammar: the else body is its own field on match_expression.
      else_code = c;
      continue;
    }
    if (field == "condition") {
      if (!saw_first_condition) {
        saw_first_condition = true;
        continue;
      }
      std::string_view t(ts_node_type(c));
      std::string_view txt = trim(get_text(c));
      if (txt == "else") {
        // Old grammar: 'else' keyword tagged with the same "condition" field.
        pending_is_else = true;
      } else if (!ts_node_is_named(c)) {
        // Anonymous op-token preceding the arm RHS (e.g. 'case', '!=', '<').
        // Tree-sitter tags it with the same "condition" field because the
        // grammar uses field('condition', seq(optional(op), _expression)).
        // Remember it so the next arm-RHS knows which compare to emit.
        pending_op = txt;
      } else if (t == "expression_list" || t == "expression_item" || t == "constant" || t == "identifier" || t == "tuple"
                 || t == "tuple_sq" || t == "paren_group" || t == "function_call_expression" || t == "member_selection"
                 || t == "bit_selection" || t == "dot_expression" || t == "unary_expression" || t == "attribute_read"
                 || t == "type_specification" || t == "if_expression" || t == "match_expression") {
        // Any expression-typed named child is the arm RHS.
        pending_expr = c;
        have_pending = true;
      } else {
        // Unknown — treat as an op-token text.
        pending_op = txt;
      }
    } else if (field == "code") {
      if (pending_is_else) {
        else_code       = c;
        pending_is_else = false;
      } else if (have_pending) {
        const bool use_case     = (pending_op == "case" || pending_op == "!case");
        // Structural / membership arm operators get their dedicated ops —
        // `does Person { … }` must lower to func_does (a superset receiver
        // satisfies `does` but never `eq`), `in (…)` to func_in.
        const bool use_does     = (pending_op == "does" || pending_op == "!does");
        const bool use_in       = (pending_op == "in" || pending_op == "!in");
        const bool negate       = (pending_op == "!case" || pending_op == "!=" || pending_op == "!does" || pending_op == "!in");
        // Emit `<compare> tmp = subject_ref rhs` and return the tmp ref,
        // wrapping in log_not when the operator is the negated form.
        auto       emit_compare = [&](const Lnast_node& rhs) {
          auto compare_ntype = use_case   ? Lnast_ntype::create_func_case()
                               : use_does ? Lnast_ntype::create_func_does()
                               : use_in   ? Lnast_ntype::create_func_in()
                                          : Lnast_ntype::create_eq();
          auto idx           = builder.add_child(compare_ntype);
          auto ref           = builder.mint_tmp_ref();
          lnast->add_child(idx, ref);
          lnast->add_child(idx, subject_ref);
          lnast->add_child(idx, rhs);
          if (!negate) {
            return ref;
          }
          auto not_idx = builder.add_child(Lnast_ntype::create_log_not());
          auto neg_ref = builder.mint_tmp_ref();
          lnast->add_child(not_idx, neg_ref);
          lnast->add_child(not_idx, ref);
          return neg_ref;
        };

        // 1g — `does`/`case` arm patterns are in type position: a primitive
        // type token (`does u32 { … }`) is a type literal, not a variable.
        auto arm_rhs_to_node = [&](TSNode rhs) {
          return (use_does || use_case) ? does_operand_to_node(rhs) : expr_to_node(rhs);
        };
        Lnast_node       arm_cond;
        // Legacy grammar emitted an `expression_list` wrapper (multi-RHS arms);
        // the new grammar exposes the bare expression directly. Handle both.
        std::string_view pet(ts_node_type(pending_expr));
        if (pet == "expression_list") {
          uint32_t nnc = ts_node_named_child_count(pending_expr);
          if (nnc == 0) {
            arm_cond = emit_compare(constant_text_to_node(trim(get_text(pending_expr))));
          } else if (nnc == 1) {
            arm_cond = emit_compare(arm_rhs_to_node(ts_node_named_child(pending_expr, 0)));
          } else {
            auto or_idx = builder.add_child(Lnast_ntype::create_log_or());
            arm_cond    = builder.mint_tmp_ref();
            lnast->add_child(or_idx, arm_cond);
            for (uint32_t j = 0; j < nnc; j++) {
              lnast->add_child(or_idx, emit_compare(arm_rhs_to_node(ts_node_named_child(pending_expr, j))));
            }
          }
        } else {
          arm_cond = emit_compare(arm_rhs_to_node(pending_expr));
        }
        arms.push_back({arm_cond, c});
        have_pending = false;
        pending_op   = "==";  // reset for the next arm
      }
    }
  }

  // All compare/log_or compute stmts have been emitted to builder.idx_stmts.
  // Add the `if` here so it follows its producers in source order.
  auto if_idx = builder.add_child(Lnast_ntype::create_if());

  // Same expression-typed arm-body lowering as if_expr_to_node: when the
  // match's value is consumed (need_result==true), detect a trailing
  // expression-typed named child and emit `assign result <expr>` for it;
  // preceding statements process as normal. Without this, every arm
  // assigns 0 and the match-expression always folds to 0 instead of the
  // body's computed value.
  static const absl::flat_hash_set<std::string_view> arm_expr_kinds = {
      "if_expression",
      "match_expression",
      "expression_item",
      "unary_expression",
      "bit_selection",
      "member_selection",
      "attribute_read",
      "dot_expression",
      "function_call_expression",
      "identifier",
      "tuple",
      "tuple_sq",
      "paren_group",
      "type_specification",
      "constant",
  };

  auto emit_body = [&](TSNode code) {
    auto body_idx = lnast->add_child(if_idx, Lnast_ntype::create_stmts());
    builder.push_stmts(body_idx);
    bool emitted_result = false;
    if (need_result && !ts_node_is_null(code) && std::string_view(ts_node_type(code)) == "scope_statement") {
      uint32_t nnc = ts_node_named_child_count(code);
      if (nnc > 0) {
        TSNode           last = ts_node_named_child(code, nnc - 1);
        std::string_view lt(ts_node_type(last));
        if (arm_expr_kinds.contains(lt)) {
          for (uint32_t i = 0; i < ts_node_child_count(code); i++) {
            TSNode cc = ts_node_child(code, i);
            if (!ts_node_is_named(cc)) {
              continue;
            }
            if (ts_node_start_byte(cc) == ts_node_start_byte(last) && ts_node_end_byte(cc) == ts_node_end_byte(last)) {
              break;  // reached the trailing expression — handle below
            }
            process_statement(cc);
          }
          Lnast_node val  = expr_to_node(last);
          auto       aidx = builder.add_child(Lnast_ntype::create_store());
          lnast->add_child(aidx, result);
          lnast->add_child(aidx, val);
          emitted_result = true;
        }
      }
    }
    if (!emitted_result) {
      if (!ts_node_is_null(code)) {
        process_scope_statement(code, body_idx);
      }
      if (need_result) {
        auto a = builder.add_child(Lnast_ntype::create_store());
        lnast->add_child(a, result);
        lnast->add_child(a, Lnast_node::create_const("0"));
      }
    }
    builder.pop_stmts();
  };

  for (const auto& arm : arms) {
    lnast->add_child(if_idx, arm.cref);
    emit_body(arm.code);
  }
  if (else_code.tree != nullptr) {
    emit_body(else_code);
  }

  // Release any local declared by the `init` clause (e.g.
  // `match const t = subject; …` → `t`) so reads after the match observe
  // it as nil instead of the still-live bundle. Emitted as plain
  // `assign <name> = nil` at the enclosing stmts level since we no longer
  // wrap the match in a private scope.
  for (const auto& name : init_locals) {
    auto idx = builder.add_child(Lnast_ntype::create_store());
    lnast->add_child(idx, Lnast_node::create_ref(name));
    lnast->add_child(idx, Lnast_node::create_const("nil"));
  }

  return need_result ? result : Lnast_node::create_const("0");
}

Lnast_node Prp2lnast::emit_range_node(const Lnast_node& start, const Lnast_node& end) {
  // Emit a `range` LNAST node (start, end) and return its ref. Used by the
  // open-end and from-zero forms of `#[...]`; constprop's process_get_mask /
  // process_set_mask consult range_map to synthesize the mask integer (closed
  // `lo..=hi` → bits-lo..hi mask; open `lo..` → -(1<<lo)).
  auto rng_idx = builder.add_child(Lnast_ntype::create_range());
  auto rng_ref = builder.mint_tmp_ref();
  lnast->add_child(rng_idx, rng_ref);
  lnast->add_child(rng_idx, start);
  lnast->add_child(rng_idx, end);
  return rng_ref;
}

Lnast_node Prp2lnast::compute_bit_mask_ref(TSNode sel_node) {
  // The grammar's `select` admits exactly one of `index` (single expression)
  // or `range` (a selection_range form). Tuple/comma indices like `#[1,4]`
  // aren't accepted by the grammar; tree-sitter error-recovers them silently
  // and just keeps the first expression — so flag any parse error on the
  // select node as a hard compile error rather than emitting wrong code.
  if (ts_node_is_null(sel_node)) {
    report_error("bit-range-empty",
                 "syntax",
                 "empty bit selection `#[]` (expected an index or range)",
                 "write a single bit `#[3]` or a range `#[0..=3]`");
  }
  if (ts_node_has_error(sel_node)) {
    report_error(sel_node,
                 "bit-range-index",
                 "syntax",
                 std::format("invalid bit-range index `{}` (tuple/comma indices not supported in `#[...]`)", get_text(sel_node)),
                 "use a single index `#[3]` or a range `#[0..=3]`; OR them for a multi-bit mask");
  }

  auto make_const_mask = [](std::string_view text) -> std::optional<Lnast_node> {
    // Only accept actual numeric source literals here. Identifier text like
    // `i` must NOT be reinterpreted as a Pyrope constant — Dlop::from_pyrope
    // happily parses single letters as character literals (`"i"` → 105),
    // which would silently produce a bit-105 mask for `t#[i]`.
    if (text.empty()) {
      return std::nullopt;
    }
    char c0 = text.front();
    if (!(c0 >= '0' && c0 <= '9') && c0 != '-' && c0 != '+') {
      return std::nullopt;
    }
    auto v = Dlop::from_pyrope(text);
    if (v->is_invalid() || !v->is_integer()) {
      return std::nullopt;
    }
    // `#[N]` selects bit position N → single-bit mask `1 << N`. Pure Const
    // arithmetic (no to_i; works for any position width).
    return Lnast_node::create_const(Dlop::create_integer(1)->shl_op(*v)->to_pyrope());
  };

  auto make_range_mask = [&](TSNode expr_item) -> std::optional<Lnast_node> {
    if (ts_node_named_child_count(expr_item) != 3) {
      return std::nullopt;
    }
    TSNode lo = ts_node_named_child(expr_item, 0);
    TSNode op = ts_node_named_child(expr_item, 1);
    TSNode hi = ts_node_named_child(expr_item, 2);
    // Inclusive ranges only — `..=`. The aliased child of the binary_other_op
    // wrapper names the operator without text.
    if (std::string_view(ts_node_type(op)) != "binary_other_op") {
      return std::nullopt;
    }
    TSNode op_inner = ts_node_named_child(op, 0);
    if (ts_node_is_null(op_inner) || std::string_view(ts_node_type(op_inner)) != "op_range_inclusive") {
      return std::nullopt;
    }

    auto lo_v = Dlop::from_pyrope(get_text(lo));
    auto hi_v = Dlop::from_pyrope(get_text(hi));
    if (lo_v->is_invalid() || hi_v->is_invalid() || !lo_v->is_integer() || !hi_v->is_integer()) {
      return std::nullopt;
    }
    // `#[lo..=hi]` → contiguous mask `((1 << (hi-lo+1)) - 1) << lo`, all Const
    // arithmetic (no to_i; any width).
    auto one   = Dlop::create_integer(1);
    auto width = hi_v->sub_op(*lo_v)->add_op(*one);
    return Lnast_node::create_const(one->shl_op(*width)->sub_op(*one)->shl_op(*lo_v)->to_pyrope());
  };

  auto make_dynamic_mask = [&](TSNode expr) {
    auto pos = expr_to_node(expr);
    auto idx = builder.add_child(Lnast_ntype::create_shl());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    lnast->add_child(idx, Lnast_node::create_const("1"));
    lnast->add_child(idx, pos);
    return ref;
  };

  // True when an `expression_item` is a range form (`lo..=hi`, `lo..<hi`,
  // `lo..+n`) rather than a single-bit index. A range lowers (via expr_to_node)
  // to a `range` LNAST node that constprop turns into a contiguous mask; a plain
  // index must instead become the single-bit mask `1 << index` (make_dynamic_mask).
  // Without this split, `b#[lo+1]` would pass `lo+1` straight to get_mask as a
  // literal bitmask — e.g. `#[5]` → mask 0b101 selects bits {0,2}, not bit 5.
  auto expr_item_is_range = [](TSNode expr_item) -> bool {
    if (ts_node_named_child_count(expr_item) != 3) {
      return false;
    }
    TSNode op = ts_node_named_child(expr_item, 1);
    if (std::string_view(ts_node_type(op)) != "binary_other_op") {
      return false;
    }
    TSNode op_inner = ts_node_named_child(op, 0);
    if (ts_node_is_null(op_inner)) {
      return false;
    }
    auto k = std::string_view(ts_node_type(op_inner));
    return k == "op_range_inclusive" || k == "op_range_exclusive" || k == "op_range_count";
  };

  TSNode index_n = child_by_field(sel_node, "index");
  TSNode range   = child_by_field(sel_node, "range");

  if (!ts_node_is_null(range)) {
    // selection_range carries one of:
    //   open_all              — bare `..`
    //   open_from: <expr>     — `expr..`
    //   from_zero_inclusive   — `..=expr`
    //   from_zero_exclusive   — `..<expr`
    TSNode open_all  = ts_node_child_by_field_name(range, "open_all", 8);
    TSNode open_from = ts_node_child_by_field_name(range, "open_from", 9);
    TSNode fz_incl   = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
    TSNode fz_excl   = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
    if (!ts_node_is_null(open_all)) {
      return emit_range_node(Lnast_node::create_const("0"), Lnast_node::create_const("nil"));
    }
    if (!ts_node_is_null(open_from)) {
      return emit_range_node(expr_to_node(open_from), Lnast_node::create_const("nil"));
    }
    if (!ts_node_is_null(fz_incl) || !ts_node_is_null(fz_excl)) {
      bool       is_lt    = !ts_node_is_null(fz_excl);
      TSNode     expr_n   = is_lt ? fz_excl : fz_incl;
      Lnast_node end_expr = expr_to_node(expr_n);
      Lnast_node end_node = end_expr;
      if (is_lt) {
        // `..<n` is exclusive: end = n - 1 (matches the inclusive form
        // stored on the range node downstream).
        auto m    = builder.add_child(Lnast_ntype::create_minus());
        auto mref = builder.mint_tmp_ref();
        lnast->add_child(m, mref);
        lnast->add_child(m, end_expr);
        lnast->add_child(m, Lnast_node::create_const("1"));
        end_node = mref;
      }
      return emit_range_node(Lnast_node::create_const("0"), end_node);
    }
    return Lnast_node::create_const("-1");
  }

  if (!ts_node_is_null(index_n)) {
    auto et = std::string_view(ts_node_type(index_n));
    if (et == "expression_item") {
      if (auto const_mask = make_range_mask(index_n)) {
        return *const_mask;  // inclusive `lo..=hi` with const endpoints → folded mask
      }
      if (expr_item_is_range(index_n)) {
        return expr_to_node(index_n);  // dynamic/exclusive range → `range` node (range_map)
      }
      return make_dynamic_mask(index_n);  // plain single-bit index expr → `1 << index`
    }
    if (auto const_mask = make_const_mask(get_text(index_n))) {
      return *const_mask;
    }
    return make_dynamic_mask(index_n);
  }

  return Lnast_node::create_const("-1");
}

Lnast_node Prp2lnast::bit_selection_to_node(TSNode n) {
  // bit_selection: argument '#' [reduction] select
  // LNAST get_mask expects a bitmask, not the selected bit position/range.
  TSNode arg = child_by_field(n, "argument");
  if (ts_node_is_null(arg)) {
    arg = ts_node_named_child(n, 0);
  }
  Lnast_node base = expr_to_node(arg);

  // The `select` field is `multiple: true` (covers the `#` token, optional
  // reduction, and the inner select node) — `child_by_field` returns the
  // first match (the `#` token). Walk children for the actual select node.
  TSNode sel_node{};
  for (uint32_t i = 0; i < child_count(n); i++) {
    TSNode           c = child(n, i);
    std::string_view ct(ts_node_type(c));
    if (ct == "select") {
      sel_node = c;
      break;
    }
  }
  TSNode type_node = child_by_field(n, "reduction");
  TSNode ext_node  = child_by_field(n, "extension");

  Lnast_node mask_ref = compute_bit_mask_ref(sel_node);

  auto idx = builder.add_child(Lnast_ntype::create_get_mask());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  lnast->add_child(idx, mask_ref);

  if (!ts_node_is_null(type_node)) {
    std::string_view rt(ts_node_type(type_node));
    auto             red_node = Lnast_ntype::create_invalid();
    if (rt == "reduction_or") {
      red_node = Lnast_ntype::create_red_or();
    } else if (rt == "reduction_and") {
      red_node = Lnast_ntype::create_red_and();
    } else if (rt == "reduction_xor") {
      red_node = Lnast_ntype::create_red_xor();
    } else if (rt == "reduction_popcount") {
      red_node = Lnast_ntype::create_popcount();
    } else {
      return ref;
    }
    auto r_idx = builder.add_child(red_node);
    auto r_ref = builder.mint_tmp_ref();
    lnast->add_child(r_idx, r_ref);
    lnast->add_child(r_idx, ref);
    return r_ref;
  }

  // Sign/zero extension (`#sext[lo..=hi]` / `#zext[lo..=hi]`). `get_mask`
  // already produces the selected bits as an unsigned value packed LSB-first
  // into positions 0..popcount(mask)-1, which IS the zero-extended result.
  // For sext we additionally sign-extend from the top selected bit: the
  // packed slice's sign bit sits at position popcount(mask)-1, so emit a
  // `sext` whose position operand is that index (constprop folds it via
  // Dlop::sext_op, matching the graph Sext cell).
  if (!ts_node_is_null(ext_node)) {
    std::string_view et(ts_node_type(ext_node));
    if (et == "sign_extend" && mask_ref.is_const()) {
      auto mv = Dlop::from_pyrope(mask_ref.get_name());
      if (!mv->is_invalid() && mv->is_integer()) {
        int sign_bit = mv->popcount() - 1;
        if (sign_bit < 0) {
          sign_bit = 0;
        }
        auto s_idx = builder.add_child(Lnast_ntype::create_sext());
        auto s_ref = builder.mint_tmp_ref();
        lnast->add_child(s_idx, s_ref);
        lnast->add_child(s_idx, ref);
        lnast->add_child(s_idx, Lnast_node::create_const(sign_bit));
        return s_ref;
      }
    }
    // zext (and any sext whose width isn't statically known): the bare
    // get_mask already zero-extends, so return it directly.
    return ref;
  }
  return ref;
}

Lnast_node Prp2lnast::member_selection_to_node(TSNode n) {
  TSNode     arg        = child_by_field(n, "argument");
  Lnast_node base       = expr_to_node(arg);
  // Collect field nodes first — each may emit auxiliary stmts (range, minus)
  // that must precede the tuple_get so single-pass constprop can resolve the
  // field's value before consuming it. Adding the tuple_get last keeps the
  // dependency order without a post-hoc reordering pass.
  auto       emit_range = [&](const Lnast_node& start, const Lnast_node& end) {
    auto rng_idx = builder.add_child(Lnast_ntype::create_range());
    auto rng_ref = builder.mint_tmp_ref();
    lnast->add_child(rng_idx, rng_ref);
    lnast->add_child(rng_idx, start);
    lnast->add_child(rng_idx, end);
    return rng_ref;
  };
  std::vector<Lnast_node> fields;
  uint32_t                nnc = ts_node_named_child_count(n);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode sel = ts_node_named_child(n, i);

    TSNode index_n = child_by_field(sel, "index");
    if (!ts_node_is_null(index_n)) {
      fields.push_back(expr_to_node(index_n));
      continue;
    }

    TSNode range = child_by_field(sel, "range");
    if (!ts_node_is_null(range)) {
      TSNode open_all  = ts_node_child_by_field_name(range, "open_all", 8);
      TSNode open_from = ts_node_child_by_field_name(range, "open_from", 9);
      TSNode fz_incl   = ts_node_child_by_field_name(range, "from_zero_inclusive", 19);
      TSNode fz_excl   = ts_node_child_by_field_name(range, "from_zero_exclusive", 19);
      // `nil` is the open-end sentinel — round-trips through Const as a
      // string and is recognised by process_tuple_get as "slice to source's
      // last index". Using a real value here would falsely truncate.
      if (!ts_node_is_null(open_all)) {
        fields.push_back(emit_range(Lnast_node::create_const("0"), Lnast_node::create_const("nil")));
      } else if (!ts_node_is_null(open_from)) {
        fields.push_back(emit_range(expr_to_node(open_from), Lnast_node::create_const("nil")));
      } else if (!ts_node_is_null(fz_incl) || !ts_node_is_null(fz_excl)) {
        bool       is_lt    = !ts_node_is_null(fz_excl);
        TSNode     fz_n     = is_lt ? fz_excl : fz_incl;
        Lnast_node end_expr = expr_to_node(fz_n);
        Lnast_node end_node = end_expr;
        if (is_lt) {
          // `..<n` is exclusive: end = n - 1 to match the inclusive form
          // stored on the range node.
          auto m    = builder.add_child(Lnast_ntype::create_minus());
          auto mref = builder.mint_tmp_ref();
          lnast->add_child(m, mref);
          lnast->add_child(m, end_expr);
          lnast->add_child(m, Lnast_node::create_const("1"));
          end_node = mref;
        }
        fields.push_back(emit_range(Lnast_node::create_const("0"), end_node));
      }
      continue;
    }
  }
  auto idx = builder.add_child(Lnast_ntype::create_tuple_get());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  for (const auto& f : fields) {
    lnast->add_child(idx, f);
  }
  return ref;
}

Lnast_node Prp2lnast::attribute_read_to_node(TSNode n) {
  // attribute_read: argument '.' attribute_list (one or more chained reads)
  // Example: `x.[bits]` reads attribute `bits` from `x`. Lower as a sequence
  // of `attr_get` ops. Multiple chained reads (`x.[a].[b]`) chain through a
  // temporary.
  TSNode     arg  = child_by_field(n, "argument");
  Lnast_node base = expr_to_node(arg);

  uint32_t nnc = ts_node_named_child_count(n);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode al = ts_node_named_child(n, i);
    if (std::string_view(ts_node_type(al)) != "attribute_list") {
      continue;
    }
    // attribute_list children carry inner `name`/`value` fields directly
    // (the grammar wraps each item in an anonymous seq, so the field tags
    // appear on attribute_list itself). Reads only use the names; any
    // `=value` on a read is overparse and is silently dropped here.
    uint32_t total = ts_node_child_count(al);
    for (uint32_t j = 0; j < total; j++) {
      const char* fname = ts_node_field_name_for_child(al, j);
      if (!fname || std::string_view(fname) != "name") {
        continue;
      }
      TSNode name_n = ts_node_child(al, j);
      reject_common_mistakes_attr_name(name_n, get_text(name_n), false);
      auto   idx    = builder.add_child(Lnast_ntype::create_attr_get());
      auto   ref    = builder.mint_tmp_ref();
      lnast->add_child(idx, ref);
      lnast->add_child(idx, base);
      lnast->add_child(idx, Lnast_node::create_const(get_text(name_n)));
      base = ref;
    }
  }
  return base;
}

Lnast_node Prp2lnast::dot_expression_to_node(TSNode n) {
  // dot_expression: item ('.' identifier|const)+
  uint32_t nnc = ts_node_named_child_count(n);
  if (nnc == 0) {
    return Lnast_node::create_ref(trim(get_text(n)));
  }
  Lnast_node base = expr_to_node(ts_node_named_child(n, 0));
  auto       idx  = builder.add_child(Lnast_ntype::create_tuple_get());
  auto       ref  = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, base);
  for (uint32_t i = 1; i < nnc; i++) {
    TSNode c = ts_node_named_child(n, i);
    lnast->add_child(idx, Lnast_node::create_const(trim(get_text(c))));
  }
  return ref;
}

Lnast_node Prp2lnast::function_call_expr_to_node(TSNode n) {
  TSNode func = child_by_field(n, "function");
  TSNode arg  = child_by_field(n, "argument");  // tuple

  // Method-call form `receiver.method(args)`: the `function` field is a
  // `dot_expression` (receiver '.' identifier). Split it so the call lowers
  // to `fcall(method, receiver, ...args)` — otherwise the whole dotted path
  // becomes the function-name ref and the receiver disappears.
  Lnast_node func_ref;
  Lnast_node receiver_ref;
  bool       has_receiver = false;
  if (ts_node_is_null(func)) {
    func_ref = Lnast_node::create_const("nil");
  } else if (std::string_view(ts_node_type(func)) == "dot_expression" && ts_node_named_child_count(func) >= 2) {
    uint32_t fnc         = ts_node_named_child_count(func);
    TSNode   method_node = ts_node_named_child(func, fnc - 1);
    func_ref             = Lnast_node::create_ref(trim(get_text(method_node)));
    if (fnc == 2) {
      // Single-level receiver: lower it as an expression and pass as arg0.
      receiver_ref = expr_to_node(ts_node_named_child(func, 0));
    } else {
      // Multi-level receiver `a.b.c()` → receiver is the tuple_get of the
      // leading dotted path; emit that as an inline tuple_get call.
      auto tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      auto tg_ref = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tg_ref);
      lnast->add_child(tg_idx, expr_to_node(ts_node_named_child(func, 0)));
      for (uint32_t i = 1; i < fnc - 1; i++) {
        TSNode f = ts_node_named_child(func, i);
        lnast->add_child(tg_idx, Lnast_node::create_const(trim(get_text(f))));
      }
      receiver_ref = tg_ref;
    }
    has_receiver = true;
  } else {
    func_ref = Lnast_node::create_ref(trim(get_text(func)));
  }

  // Task 1m — `import("unit")` is a comptime builtin with its own canonical
  // lowering (validated string-literal argument; const-form callee).
  if (!has_receiver && func_ref.is_ref() && func_ref.get_name() == "import") {
    auto ref = builder.mint_tmp_ref();
    lower_import_call(n, arg, ref);
    return ref;
  }

  auto call_args = collect_call_args(arg);
  if (has_receiver) {
    Call_arg receiver_arg;
    receiver_arg.value   = receiver_ref;
    receiver_arg.is_ufcs = true;  // task 1k — UFCS receiver marker (no self ⇒ compile error)
    call_args.insert(call_args.begin(), receiver_arg);
  }

  auto idx = builder.add_child(Lnast_ntype::create_func_call());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  lnast->add_child(idx, func_ref);
  add_call_args_to_fcall(idx, call_args);
  attach_loc(idx, n);  // call-site span → upass argument-naming diagnostics point here
  return ref;
}

Lnast_node Prp2lnast::tuple_to_node(TSNode n, bool /*is_square*/) {
  uint32_t nnc = ts_node_named_child_count(n);

  // Tuple scope (04-variables.md): "Tuple field initializers follow program
  // order and can read earlier tuple fields by name". Fields are not
  // statement-level declarations, so expose each field name to
  // identifier_to_node through an in-flight frame as soon as its value has
  // been lowered. RAII so report_error unwinds cleanly.
  struct Inflight_scope_guard {
    std::vector<std::vector<std::string>>& scopes;
    explicit Inflight_scope_guard(std::vector<std::vector<std::string>>& s) : scopes(s) { scopes.emplace_back(); }
    ~Inflight_scope_guard() { scopes.pop_back(); }
  };
  Inflight_scope_guard tuple_scope(inflight_name_scopes_);

  // Pre-compute sub-expressions so their LNAST statements emit BEFORE the
  // tuple_add that references them — constprop runs in textual order.
  struct Item {
    bool        is_assign = false;
    bool        is_spread = false;  // `...expr` — emit value as its own concat chunk
    std::string assign_key;
    Lnast_node  value;
    // Phase 3: per-field attribute decorations on the lvalue
    // (e.g. `b::[poison=99]=2`). Lowered after the tuple_add as a
    // `tuple_get` + `attr_set` pair so the override is observable on the
    // resulting tuple field.
    TSNode      attr_list_node{};
    bool        has_attr_list{false};
    // Phase 8 typesystem: per-field type annotation on the lvalue
    // (e.g. `(a:u4=3, b:u4=5)`). Lowered after the tuple_add as a
    // `tuple_get` + `type_spec` pair so `t.a.[bits]` resolves.
    TSNode      type_cast_node{};
    bool        has_type_cast{false};
    // Field declared with an explicit `mut` marker (`const t = (mut a:u4=1)`).
    // Lowered after the tuple_add as a `declare(tg_tmp, prim_type_none, mut)`
    // so the attributes pass records Decl_kind::mut_kind on the field path —
    // writes through a mut field of a const tuple are legal, not a rebind.
    bool        is_mut{false};
  };
  std::vector<Item> items;
  items.reserve(nnc);
  // Track named-field keys so a bundle literal that repeats a field name is
  // rejected: "each tuple field must be unique" (docs/docs/pyrope/03-bundle.md
  // "Concatenate fields"). Concatenation must be a separate post-declaration
  // statement (`y.ff ++= 2`), never a second field in the literal.
  absl::flat_hash_set<std::string> seen_field_keys;
  for (uint32_t i = 0; i < nnc; i++) {
    TSNode           c = ts_node_named_child(n, i);
    std::string_view t(ts_node_type(c));
    if (t == "comment") {
      // Comments are tree-sitter `extras`, so they surface as named children
      // even inside a tuple. Skip them — otherwise the generic `else` branch
      // below would lower a comment into a spurious positional const field.
      continue;
    }
    if (t == "unary_expression") {
      // Spread (`...inner`) is the only positional unary in a tuple item that
      // needs to flatten — every other unary just becomes a regular value.
      // The grammar aliases the spread token to `op_spread`, so dispatch by
      // the operator field's node kind.
      TSNode op_n = child_by_field(c, "operator");
      if (!ts_node_is_null(op_n) && std::string_view(ts_node_type(op_n)) == "op_spread") {
        TSNode arg_n = child_by_field(c, "argument");
        if (!ts_node_is_null(arg_n)) {
          Item it;
          it.is_spread = true;
          it.value     = expr_to_node(arg_n);
          items.push_back(std::move(it));
          continue;
        }
      }
    }
    if (t == "assignment") {
      Item it;
      it.is_assign = true;
      // Per-field storage marker: `mut a:u4=1` carries a `decl` field with a
      // mut_decl storage child. Captured so the post-tuple_add decoration
      // loop can record the field's mutability (see Item::is_mut).
      if (TSNode dn = child_by_field(c, "decl"); !ts_node_is_null(dn)) {
        TSNode storage = child_by_field(dn, "storage");
        if (!ts_node_is_null(storage) && std::string_view(ts_node_type(storage)) == "mut_decl") {
          it.is_mut = true;
        }
      }
      // A bundle-literal field may only be introduced with plain `=`. A
      // compound operator (`++=`, `+=`, …) here is a compile error: declare
      // the field with `=`, then mutate it with `name OP= …` as a separate
      // statement (the variable must be `mut`). The grammar makes the
      // `operator` field a required `assignment_operator` whose single named
      // child names the aliased op kind (`assign`, `assign_tuple_concat`, …).
      {
        TSNode op_node = child_by_field(c, "operator");
        if (!ts_node_is_null(op_node)) {
          TSNode           op_inner = ts_node_named_child(op_node, 0);
          std::string_view op_k     = ts_node_is_null(op_inner) ? std::string_view{} : std::string_view(ts_node_type(op_inner));
          if (!op_k.empty() && op_k != "assign") {
            report_error(c,
                         "bundle-compound-assign",
                         "syntax",
                         std::format("compound assignment `{}` is not allowed inside a bundle literal", trim(get_text(op_node))),
                         "set the field with `=` here, then concatenate/update it with a separate `name OP= …` statement");
          }
        }
      }
      TSNode                      lv = child_by_field(c, "lvalue");
      TSNode                      rv = child_by_field(c, "rvalue");
      // The lvalue may carry a type_cast (`(a:u4=1, …)` parses lv as a
      // `typed_identifier`). Strip it: the ref text must be a bare
      // identifier; otherwise lnastfmt rejects the LNAST.
      std::string_view            lvt2(ts_node_type(lv));
      // Phase 3 — capture any per-field attribute_list (`b::[poison=99]=2`)
      // wherever the grammar attaches it so we can lower it as a post-tuple
      // attr_set. The attribute_list may live under a `type_specification`
      // wrapper or directly under the `typed_identifier`; walk children of
      // the lvalue to find it. Doing so before the lvt2 dispatch keeps the
      // logic shared between `typed_identifier` and `type_specification`.
      // Recursively walk the lvalue subtree (and the assignment node itself,
      // since the grammar may attach `::[...]` between lvalue and `=`) to
      // find an attribute_list.
      std::function<void(TSNode)> capture_attr_list_under = [&](TSNode parent_node) {
        if (it.has_attr_list) {
          return;
        }
        uint32_t total = ts_node_child_count(parent_node);
        for (uint32_t k = 0; k < total; k++) {
          TSNode           kc  = ts_node_child(parent_node, k);
          const char*      kfn = ts_node_field_name_for_child(parent_node, k);
          std::string_view kt(ts_node_type(kc));
          if (kt == "attribute_list") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            return;
          }
          // New grammar: the write-side `::[…]` attribute carrier is a
          // `tuple_sq` tagged with the `attribute` field on a `type_cast`.
          if ((kt == "tuple_sq" || kt == "attribute_sq") && kfn && std::string_view(kfn) == "attribute") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            return;
          }
          capture_attr_list_under(kc);
          if (it.has_attr_list) {
            return;
          }
        }
      };
      capture_attr_list_under(lv);
      // Some grammar shapes attach the `::[...]` attribute_list to the
      // assignment node directly (between lvalue and rvalue); also scan
      // the assignment's own children but exclude rvalue/operator subtrees.
      if (!it.has_attr_list) {
        uint32_t total = ts_node_child_count(c);
        for (uint32_t k = 0; k < total; k++) {
          TSNode           kc  = ts_node_child(c, k);
          const char*      kfn = ts_node_field_name_for_child(c, k);
          std::string_view kt(ts_node_type(kc));
          if (kt == "attribute_list") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            break;
          }
          if ((kt == "tuple_sq" || kt == "attribute_sq") && kfn && std::string_view(kfn) == "attribute") {
            it.attr_list_node = kc;
            it.has_attr_list  = true;
            break;
          }
        }
      }
      if (lvt2 == "type_specification") {
        // `b::[poison=99]` parses as a type_specification with the bare
        // identifier in the `argument` field.
        TSNode arg = child_by_field(lv, "argument");
        if (!ts_node_is_null(arg)) {
          std::string_view at(ts_node_type(arg));
          if (at == "typed_identifier") {
            TSNode id     = child_by_field(arg, "identifier");
            it.assign_key = ts_node_is_null(id) ? trim(get_text(arg)) : trim(get_text(id));
          } else {
            it.assign_key = trim(get_text(arg));
          }
        }
      } else if (lvt2 == "typed_identifier") {
        TSNode id = child_by_field(lv, "identifier");
        if (!ts_node_is_null(id)) {
          it.assign_key = trim(get_text(id));
        }
        // Phase 8: capture the per-field type cast (`:u4`, `:s5`, …) so the
        // post-tuple_add loop can emit a per-field type_spec.
        TSNode tc = child_by_field(lv, "type");
        if (!ts_node_is_null(tc)) {
          it.type_cast_node = tc;
          it.has_type_cast  = true;
        }
      } else {
        // `_complex_identifier` with optional sibling `type` field on the
        // assignment node. Use the lvalue text directly; for plain
        // identifiers it's already clean.
        it.assign_key = trim(get_text(lv));
      }
      if (!it.assign_key.empty()) {
        // A named field that repeats an earlier one in the same bundle literal
        // is a duplicate — reject it (each tuple field must be unique). The
        // span points at this (the second) field so `locate_error_here` lands
        // on the offending line.
        if (!seen_field_keys.insert(it.assign_key).second) {
          report_error(c,
                       "duplicate-tuple-field",
                       "name",
                       std::format("duplicate field `{}` in bundle literal (each field name must be unique)", it.assign_key),
                       "rename or remove the duplicate field");
        }
      } else {
        // Anonymous slot (e.g. `(mut :u13 = 5)`) — synthesize a tmp name so
        // we don't emit an empty `ref` (lnastfmt would reject it).
        it.assign_key = builder.create_lnast_tmp();
      }
      if (!ts_node_is_null(rv)) {
        it.value = expr_to_node(rv);
      } else {
        TSNode op    = child_by_field(c, "operator");
        auto   start = ts_node_is_null(op) ? ts_node_end_byte(lv) : ts_node_end_byte(op);
        it.value     = constant_text_to_node(trim(text_between(start, ts_node_end_byte(c))));
      }
      // The field is readable by the LATER fields' initializers (tuple scope)
      // — push after lowering its own value so `a = a + 1` self-reads still
      // resolve against the enclosing scope.
      inflight_name_scopes_.back().push_back(it.assign_key);
      items.push_back(std::move(it));
    } else if (t == "var_or_let_or_reg") {
      // New `_tuple_item` choice: `decl value` (e.g. `(mut 3, const 5)`).
      // `_tuple_item` is hidden so its children show as siblings on the
      // tuple. Pair this kind keyword with the next named sibling, which is
      // either a `typed_identifier` (lvalue declaration form) or an
      // expression (positional value with mutability override). Either way
      // the field is positional — record the value only.
      if (i + 1 < nnc) {
        TSNode           next = ts_node_named_child(n, i + 1);
        std::string_view nextt(ts_node_type(next));
        Item             it;
        if (nextt == "typed_identifier") {
          // Bare declaration like `mut b` — emit the identifier as the
          // tuple slot's positional value (initial value is undefined).
          TSNode id = child_by_field(next, "identifier");
          if (!ts_node_is_null(id)) {
            it.value = identifier_to_node(id, true);
          } else {
            it.value = builder.mint_tmp_ref();
          }
        } else {
          it.value = expr_to_node(next);
        }
        items.push_back(std::move(it));
        ++i;  // consumed the value sibling
      }
    } else if (t == "typed_identifier") {
      // Bare typed_identifier inside a tuple (no preceding decl keyword).
      // Treat as a positional ref slot.
      Item   it;
      TSNode id = child_by_field(c, "identifier");
      it.value  = ts_node_is_null(id) ? builder.mint_tmp_ref() : identifier_to_node(id, true);
      items.push_back(std::move(it));
    } else if (t == "lambda" && !ts_node_is_null(child_by_field(c, "name"))) {
      // In-tuple method definition (`comb call(ref self, a) -> (r) { … }`).
      // Hoist the lambda to a top-level func_def under a file-unique name and
      // bind it as a named field holding the function ref — the exact shape
      // of the decorator pattern (`call = docall`), so constprop records the
      // qualified function name and the runner's method dispatch resolves it
      // through the receiver's bundle.
      TSNode      name_node = child_by_field(c, "name");
      std::string mname(trim(get_text(name_node)));
      std::string uniq = std::format("{}__t{}", mname, ++hoisted_lambda_count_);
      process_lambda_statement_named(c, uniq);
      Item it;
      it.is_assign  = true;
      it.assign_key = mname;
      it.value      = Lnast_node::create_ref(uniq);
      if (!seen_field_keys.insert(it.assign_key).second) {
        report_error(c,
                     "duplicate-tuple-field",
                     "name",
                     std::format("duplicate field `{}` in bundle literal (each field name must be unique)", it.assign_key),
                     "rename or remove the duplicate method");
      }
      // Method fields are readable by later field initializers (tuple scope).
      inflight_name_scopes_.back().push_back(it.assign_key);
      items.push_back(std::move(it));
    } else {
      if (t == "type_specification") {
        // Type-tuple field intro (`type Complex = (v1:string, …)`): the
        // field name DECLARES the field — it is not a read of `v1`, and it
        // is readable by the later fields. Register it before lowering so
        // identifier_to_node resolves it in-flight.
        TSNode arg = child_by_field(c, "argument");
        if (!ts_node_is_null(arg)) {
          TSNode id = std::string_view(ts_node_type(arg)) == "typed_identifier" ? child_by_field(arg, "identifier") : arg;
          if (!ts_node_is_null(id) && std::string_view(ts_node_type(id)) == "identifier") {
            inflight_name_scopes_.back().emplace_back(trim(get_text(id)));
          }
        }
      }
      Item it;
      it.value = expr_to_node(c);
      items.push_back(std::move(it));
    }
  }

  // Spread (`...inner`) items expand inline at the tuple's outer level; we
  // emit a tuple_concat of (chunks of non-spread items as their own
  // tuple_add tmps) interleaved with each spread's bundle ref. The simple
  // no-spread case stays as a single tuple_add.
  bool has_spread = false;
  for (const auto& it : items) {
    if (it.is_spread) {
      has_spread = true;
      break;
    }
  }

  auto emit_chunk_tuple_add = [&](const std::vector<Item>& chunk) -> Lnast_node {
    auto chunk_idx = builder.add_child(Lnast_ntype::create_tuple_add());
    auto chunk_ref = builder.mint_tmp_ref();
    lnast->add_child(chunk_idx, chunk_ref);
    for (const auto& it : chunk) {
      if (it.is_assign) {
        auto aidx = lnast->add_child(chunk_idx, Lnast_ntype::create_store());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(chunk_idx, it.value);
      }
    }
    return chunk_ref;
  };

  if (!has_spread) {
    auto idx = builder.add_child(Lnast_ntype::create_tuple_add());
    auto ref = builder.mint_tmp_ref();
    lnast->add_child(idx, ref);
    if (nnc == 0) {
      // Tree-sitter-pyrope hides `_simple_number` (and other `_constant`) tokens,
      // so literals like `(10)` or `(-100)` parse as a tuple with zero named
      // children. Recover the literal from the text between `(` and `)`.
      auto inner = trim(text_between(ts_node_start_byte(n) + 1, ts_node_end_byte(n) - 1));
      if (!inner.empty()) {
        lnast->add_child(idx, constant_text_to_node(inner));
      }
      return ref;
    }
    for (auto& it : items) {
      if (it.is_assign) {
        auto aidx = lnast->add_child(idx, Lnast_ntype::create_store());
        lnast->add_child(aidx, Lnast_node::create_ref(it.assign_key));
        lnast->add_child(aidx, it.value);
      } else {
        lnast->add_child(idx, it.value);
      }
    }
    // Per-field attribute overrides (e.g. `b::[poison=99]=2`) and per-
    // field type annotations (e.g. `a:u4=3`). Emit a tuple_get to
    // introduce a tmp ref bound to the field, then attach decorations
    // against that tmp. The attribute pass tracks tuple_get aliases and
    // migrates the attrs to the underlying path so `t.b.[poison]` /
    // `t.a.[bits]` reads find the override.
    //
    // Per-field types: `a:u4` is sugar for `a:int(max=15,min=0)`, so emit the
    // canonical `prim_type_int(max,min)` via a `type_spec` on the field's
    // tuple_get tmp (Task 1t — was a bare `ubits`/`sbits` attr_set). Downstream
    // passes only ever see `prim_type_int`; `bits`/`sign` derive from the range.
    // (`type_spec` is in `parent_writes_pos0` so lnastfmt's unwritten-tmp check
    // accepts the tmp once the producing `tuple_get` is DCE'd.)
    for (const auto& it : items) {
      if ((!it.has_attr_list && !it.has_type_cast && !it.is_mut) || !it.is_assign) {
        continue;
      }
      auto tg_idx = builder.add_child(Lnast_ntype::create_tuple_get());
      auto tg_ref = builder.mint_tmp_ref();
      lnast->add_child(tg_idx, tg_ref);
      lnast->add_child(tg_idx, ref);
      lnast->add_child(tg_idx, Lnast_node::create_const(it.assign_key));
      if (it.is_mut) {
        // Record the field's `mut` marker on the tuple_get tmp (alias-chased
        // by attributes' lookup_type_info onto `t.field`): a write through a
        // mut field of a `const` tuple is legal, not a const rebind.
        auto d_idx = builder.add_child(Lnast_ntype::create_declare());
        lnast->add_child(d_idx, tg_ref);
        lnast->add_child(d_idx, Lnast_ntype::create_prim_type_none());
        lnast->add_child(d_idx, Lnast_node::create_const("mut"));
      }
      if (it.has_type_cast) {
        TSNode ty = child_by_field(it.type_cast_node, "type");
        if (!ts_node_is_null(ty)) {
          std::string_view tt(ts_node_type(ty));
          if (tt == "uint_type" || tt == "sint_type" || tt == "bool_type" || tt == "string_type") {
            // Task 1t — `a:u4` is sugar for `a:int(max=15,min=0)`. Emit the
            // canonical prim_type node via a `type_spec` on the field's
            // tuple_get tmp (was a bare `ubits`/`sbits` attr_set). emit_type_expr
            // maps uint/sint→prim_type_int (bits/sign derive from the range),
            // bool→prim_type_bool, string→prim_type_string — so a typed field's
            // kind is recorded for the 1g per-field `does`/`equals`/`case` check
            // (previously only integer field types survived).
            auto ts_idx = builder.add_child(Lnast_ntype::create_type_spec());
            lnast->add_child(ts_idx, tg_ref);
            emit_type_expr(ts_idx, ty);
          } else if (tt == "expression_type" || tt == "dot_expression_type" || tt == "function_call_type") {
            // Named-type field (`inn:inner_t`): record its typename so a later
            // `o.inn.[typename]` resolves — mirrors the top-level `:Type` case in
            // emit_type_spec. The attribute pass migrates this tuple_get-tmp attr
            // onto the underlying field path (same as the ubits/sbits case).
            auto raw = trim(get_text(ty));
            if (!raw.empty()) {
              auto as_idx = builder.add_child(Lnast_ntype::create_attr_set());
              lnast->add_child(as_idx, tg_ref);
              lnast->add_child(as_idx, Lnast_node::create_const("typename"));
              std::string quoted;
              quoted.reserve(raw.size() + 2);
              quoted.push_back('\'');
              quoted.append(raw);
              quoted.push_back('\'');
              lnast->add_child(as_idx, Lnast_node::create_const(quoted));
            }
          }
        }
      }
      if (it.has_attr_list) {
        emit_attribute_list(tg_ref, it.attr_list_node);
      }
    }
    return ref;
  }

  // Spread path: build a list of bundle refs to concat. Each contiguous run
  // of non-spread items becomes a tuple_add tmp; each spread contributes its
  // pre-evaluated inner ref directly. Concat is then a single tuple_concat
  // over those refs.
  std::vector<Lnast_node> chunks;
  std::vector<Item>       pending;
  for (auto& it : items) {
    if (it.is_spread) {
      if (!pending.empty()) {
        chunks.push_back(emit_chunk_tuple_add(pending));
        pending.clear();
      }
      chunks.push_back(it.value);
    } else {
      pending.push_back(std::move(it));
    }
  }
  if (!pending.empty()) {
    chunks.push_back(emit_chunk_tuple_add(pending));
  }

  auto idx = builder.add_child(Lnast_ntype::create_tuple_concat());
  auto ref = builder.mint_tmp_ref();
  lnast->add_child(idx, ref);
  for (const auto& c : chunks) {
    lnast->add_child(idx, c);
  }
  return ref;
}

// ---------------- Factory hook ----------------
