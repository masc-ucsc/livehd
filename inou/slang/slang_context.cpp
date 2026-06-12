//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Shared Slang_context plumbing: root walk, naming, constant evaluation,
// provenance and diagnostics. The per-concern lowering code lives in
// slang_structure/stmt/expr/lvalue/types.cpp (todo/ 2s subtask A).

#include "slang_context.hpp"

#include <cctype>
#include <utility>

#include "diag.hpp"
#include "iassert.hpp"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang_location.hpp"
#include "str_tools.hpp"

void Slang_context::process_root(const slang::ast::RootSymbol& root) {
  for (auto inst : root.topInstances) {
    lower_module(*inst);
  }
}

std::vector<std::shared_ptr<Lnast>> Slang_context::pick_lnast() {
  std::vector<std::shared_ptr<Lnast>> v = std::move(ordered_lnasts_);
  ordered_lnasts_.clear();
  lowered_.clear();
  module_names_.clear();
  module_names_used_.clear();
  return v;
}

// ── naming ────────────────────────────────────────────────────────────────────

// Escaped Verilog identifiers (`\a[0] `) carry non-identifier characters;
// the LNAST ref-text contract wants those backtick-quoted (the form
// prp2lnast emits). Two characters cannot ride even INSIDE the quotes:
// whitespace (the dump tokenizer splits on it) and '.' (the builder's
// bundle-path split treats it as a field separator) - map both to '_'
// BEFORE uniquing so distinct raws that sanitize alike still get suffixes.
static std::string sanitize_name(std::string name) {
  for (auto& c : name) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == '.') {
      c = '_';
    }
  }
  return name;
}

static std::string quote_if_needed(std::string name) {
  bool plain = !name.empty();
  for (const char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
      plain = false;
      break;
    }
  }
  if (plain && std::isdigit(static_cast<unsigned char>(name.front()))) {
    plain = false;
  }
  return plain ? name : absl::StrCat("`", name, "`");
}

std::string Slang_context::lname_of(const slang::ast::Symbol& sym) {
  auto it = sym_lname_.find(&sym);
  if (it != sym_lname_.end()) {
    return it->second;
  }

  std::string base = sanitize_name(absl::StrCat(genblk_prefix_, sym.name));
  if (base.empty()) {
    base = "_anon";
  }
  std::string name = base;
  int         n    = 0;
  while (used_names_.contains(name) || Lnast::is_tmp(name)) {
    name = absl::StrCat(base, "_s", ++n);
  }
  used_names_.insert(name);
  name = quote_if_needed(std::move(name));
  sym_lname_.emplace(&sym, name);
  return name;
}

std::string Slang_context::fresh_local(std::string_view stem) {
  std::string name = absl::StrCat("_", stem, "_", ++local_cnt_);
  while (used_names_.contains(name)) {
    name = absl::StrCat("_", stem, "_", ++local_cnt_);
  }
  used_names_.insert(name);
  return name;
}

std::string Slang_context::to_int_value(const std::string& v) {
  if (!is_bool_value(v)) {
    return v;
  }
  if (v == "true") {
    return "1";
  }
  if (v == "false") {
    return "0";
  }
  // bool -> 0/1 through a branch-merged local (tolg: one mux).
  auto l = fresh_local("b2i");
  builder_.create_declare_stmts(l, "mut", "", "");
  auto if_nid = builder_.create_if_stmt(false);
  builder_.add_if_cond(if_nid, v);
  auto then_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(then_stmts);
  builder_.create_assign_stmts(l, "1");
  builder_.pop_stmts();
  auto else_stmts = builder_.add_if_stmts(if_nid);
  builder_.push_stmts(else_stmts);
  builder_.create_assign_stmts(l, "0");
  builder_.pop_stmts();
  return l;
}

// ── constant evaluation ──────────────────────────────────────────────────────

std::optional<slang::ConstantValue> Slang_context::try_eval(const slang::ast::Expression& expr) {
  if (!eval_ctx_) {
    return std::nullopt;
  }
  auto cv = expr.eval(*eval_ctx_);
  if (cv.bad()) {
    return std::nullopt;
  }
  return cv;
}

std::optional<int64_t> Slang_context::try_eval_int(const slang::ast::Expression& expr) {
  auto cv = try_eval(expr);
  if (!cv || !cv->isInteger()) {
    return std::nullopt;
  }
  return cv->integer().as<int64_t>();
}

std::string Slang_context::const_text(const slang::SVInt& svint) {
  if (svint.hasUnknown()) {
    // x/z bits ride as `?` in an explicitly-signed binary literal (bare 0b is
    // invalid pyrope; hlop enforces 0ub/0sb).
    auto buffer = svint.toString(slang::LiteralBase::Binary, false);
    std::string body(buffer.data(), buffer.size());
    for (auto& c : body) {
      if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') {
        c = '?';
      }
    }
    if (svint.isSigned() && svint.isNegative()) {
      // A negative pattern with unknowns cannot ride a plain 0sb literal
      // (pyrope literals are magnitude-form); print sign-extended unsigned.
      return absl::StrCat("0ub", body);
    }
    return absl::StrCat(svint.isSigned() ? "0sb" : "0ub", body);
  }

  if (svint.isNegative() || svint.getMinRepresentedBits() < 8) {  // small numbers in decimal (easier to read)
    auto buffer = svint.toString(slang::LiteralBase::Decimal, false);
    return std::string(buffer.data(), buffer.size());
  }

  auto buffer = svint.toString(slang::LiteralBase::Hex, false);
  return absl::StrCat("0x", std::string_view(buffer.data(), buffer.size()));
}

// ── provenance + diagnostics ─────────────────────────────────────────────────

hhds::SourceId Slang_context::mint_loc(slang::SourceRange range) {
  if (sm_ == nullptr || builder_.lnast == nullptr) {
    return hhds::SourceId_invalid;
  }
  return livehd::slang_loc::mint(builder_.lnast->source_locator(), *sm_, range);
}

void Slang_context::set_pending_loc(slang::SourceRange range) {
  if (builder_.lnast) {
    builder_.lnast->set_pending_srcid(mint_loc(range));
  }
}

void Slang_context::clear_pending_loc() {
  if (builder_.lnast) {
    builder_.lnast->set_pending_srcid(hhds::SourceId_invalid);
  }
}

void Slang_context::emit_unsupported(slang::SourceRange range, std::string_view code, std::string message,
                                     std::string_view hint) {
  module_failed_ = true;
  livehd::diag::Span span;
  if (sm_ != nullptr) {
    span = livehd::slang_loc::span_of(*sm_, range);
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string(code),
                                                     .category = "unsupported",
                                                     .pass     = "inou.slang",
                                                     .message  = std::move(message),
                                                     .span     = std::move(span),
                                                     .hint     = std::string(hint)});
}

void Slang_context::emit_error(slang::SourceRange range, std::string_view code, std::string_view category,
                               std::string message, std::string_view hint) {
  module_failed_ = true;
  livehd::diag::Span span;
  if (sm_ != nullptr) {
    span = livehd::slang_loc::span_of(*sm_, range);
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                     .code     = std::string(code),
                                                     .category = std::string(category),
                                                     .pass     = "inou.slang",
                                                     .message  = std::move(message),
                                                     .span     = std::move(span),
                                                     .hint     = std::string(hint)});
}

void Slang_context::emit_warning(slang::SourceRange range, std::string_view code, std::string_view category,
                                 std::string message, std::string_view hint) {
  livehd::diag::Span span;
  if (sm_ != nullptr) {
    span = livehd::slang_loc::span_of(*sm_, range);
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::warning,
                                                     .code     = std::string(code),
                                                     .category = std::string(category),
                                                     .pass     = "inou.slang",
                                                     .message  = std::move(message),
                                                     .span     = std::move(span),
                                                     .hint     = std::string(hint)});
}
