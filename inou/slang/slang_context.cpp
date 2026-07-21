//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Shared Slang_context plumbing: root walk, naming, constant evaluation,
// provenance and diagnostics. The per-concern lowering code lives in
// slang_structure/stmt/expr/lvalue/types.cpp (todo/ 2s subtask A).

#include "slang_context.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <set>
#include <utility>

#include "diag.hpp"
#include "iassert.hpp"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_location.hpp"
#include "str_tools.hpp"

void Slang_context::process_root(const slang::ast::RootSymbol& root) {
  for (auto inst : root.topInstances) {
    lower_module(*inst);
  }
  emit_package_units();  // one namespace .prp per referenced package (pub comptime consts)
}

// Emit one Pyrope NAMESPACE unit per package whose parameters this compile
// referenced by name (see package_param_ref). Each is a file of
// `pub comptime const PARAM[:type] = <defining expr | value>` — the export side
// of the `pkg.PARAM` provenance refs; the importing module unit carries the
// matching `const pkg = import("pkg")` (get_imported_packages → prp_writer).
//
// Fidelity riders (M2): a param's DEFINING EXPRESSION is preserved as readable
// text (`TXFMA_B19 - TXFMA_B24`) when render_const_expr can prove it value-
// faithful; the params an expression references join the emitted set (closure);
// consts print in SOURCE order; an explicit SV width becomes a `:uN`/`:sN`
// type. pub_values_ always carries the FOLDED literals (the import machinery
// consumes them), so recompile semantics never depend on the expression text.
void Slang_context::emit_package_units() {
  // ── closure: pull in params referenced by kept defining expressions ─────────
  struct Pinfo {
    std::string              value;      // folded literal (pyrope text)
    std::string              expr;       // defining expression text ("" → print the value)
    std::string              type;       // "u5"-style type text ("" → none)
    std::vector<std::string> same_refs;  // same-package names the expr reads
  };
  std::map<std::string, std::map<std::string, Pinfo>> needed;
  std::map<std::string, std::set<std::string>>        unit_imports;  // pkg → cross-pkg imports
  std::deque<std::pair<std::string, std::string>>     work;
  for (const auto& [pkg_name, params] : referenced_pkg_params_) {
    for (const auto& [param_name, value] : params) {
      needed[pkg_name][param_name].value = value;
      work.emplace_back(pkg_name, param_name);
    }
  }
  // Would importing `to` from `from` create an import cycle among the units?
  auto creates_cycle = [&](const std::string& from, const std::string& to) {
    std::vector<std::string> stack{to};
    std::set<std::string>    seen;
    while (!stack.empty()) {
      auto cur = stack.back();
      stack.pop_back();
      if (cur == from) {
        return true;
      }
      if (!seen.insert(cur).second) {
        continue;
      }
      if (auto it = unit_imports.find(cur); it != unit_imports.end()) {
        stack.insert(stack.end(), it->second.begin(), it->second.end());
      }
    }
    return false;
  };
  // lower_module scopes eval_ctx_ per body and restores it to empty before we
  // run, but render_const_expr's literal/conversion checks need try_eval — give
  // the closure a package-scoped context (restored at the end).
  auto saved_eval = std::move(eval_ctx_);
  while (!work.empty()) {
    auto [pkg_name, param_name] = work.front();
    work.pop_front();
    auto sit = referenced_pkg_syms_.find(pkg_name);
    if (sit == referenced_pkg_syms_.end()) {
      continue;
    }
    const auto* pkg  = sit->second;
    const auto* msym = pkg->find(param_name);
    if (msym == nullptr) {
      continue;
    }
    eval_ctx_.emplace(pkg->asSymbol(), slang::ast::EvalFlags::CacheResults);
    auto& pi = needed[pkg_name][param_name];
    if (pi.value.empty()) {  // closure-added: compute the folded literal
      const auto* cv = package_const_value(*msym);
      if (cv == nullptr || !cv->isInteger()) {
        needed[pkg_name].erase(param_name);
        continue;
      }
      pi.value = const_text(cv->integer());
    }
    // type: an explicit SV width (anything but the bare-int default) prints
    if (msym->kind == slang::ast::SymbolKind::Parameter || msym->kind == slang::ast::SymbolKind::EnumValue) {
      const auto& t = msym->as<slang::ast::ValueSymbol>().getType();
      if (t.isIntegral()) {
        auto ti = tinfo(t);
        if (!(ti.bits == 32 && ti.is_signed)) {  // skip the plain-`int` default
          pi.type = absl::StrCat(ti.is_signed ? "s" : "u", ti.bits);
        }
      }
    }
    // defining expression (Parameter only; an enum member's value is its face)
    if (msym->kind != slang::ast::SymbolKind::Parameter) {
      continue;
    }
    const auto* init = msym->as<slang::ast::ValueSymbol>().getInitializer();
    if (init == nullptr) {
      continue;
    }
    {  // a bare (possibly negated / converted) literal: the folded value IS the source form
      const auto* lit = init;
      while (lit->kind == slang::ast::ExpressionKind::Conversion) {
        lit = &lit->as<slang::ast::ConversionExpression>().operand();
      }
      if (lit->kind == slang::ast::ExpressionKind::UnaryOp) {
        const auto& u = lit->as<slang::ast::UnaryExpression>();
        if (u.op == slang::ast::UnaryOperator::Minus || u.op == slang::ast::UnaryOperator::Plus) {
          lit = &u.operand();
        }
      }
      while (lit->kind == slang::ast::ExpressionKind::Conversion) {
        lit = &lit->as<slang::ast::ConversionExpression>().operand();
      }
      if (lit->kind == slang::ast::ExpressionKind::IntegerLiteral) {
        continue;
      }
    }
    std::set<std::string>                                                 imps;
    std::vector<std::pair<const slang::ast::PackageSymbol*, std::string>> refs;
    auto                                                                  r = render_const_expr(*init, pkg, imps, refs);
    if (!r) {
      continue;
    }
    const auto* cv  = package_const_value(*msym);
    auto v64 = (cv != nullptr && cv->isInteger()) ? cv->integer().as<int64_t>() : std::optional<int64_t>{};
    if (!v64 || *v64 != r->second) {
      continue;  // render not value-faithful — keep the literal
    }
    bool imports_ok = true;
    for (const auto& ip : imps) {
      if (creates_cycle(pkg_name, ip)) {
        imports_ok = false;  // cross-package cycle — keep the literal
        break;
      }
    }
    if (!imports_ok) {
      continue;
    }
    pi.expr = r->first;
    for (const auto& ip : imps) {
      unit_imports[pkg_name].insert(ip);
    }
    for (const auto& [rpkg, rname] : refs) {
      std::string rpkg_name(rpkg->name);
      if (rpkg == pkg) {
        pi.same_refs.push_back(rname);
      }
      if (needed[rpkg_name].count(rname) == 0) {
        referenced_pkg_syms_.emplace(rpkg_name, rpkg);
        needed[rpkg_name][rname];  // value filled when its work item runs
        work.emplace_back(rpkg_name, rname);
      }
    }
  }
  eval_ctx_ = std::move(saved_eval);
  // ── emission: one namespace unit per package, consts in SOURCE order ────────
  for (const auto& [pn, aliases] : referenced_pkg_types_) {
    needed[pn];  // a package referenced ONLY via port-dim aliases still emits a unit
  }
  for (const auto& [pkg_name, params] : needed) {
    if (params.empty() && referenced_pkg_types_.count(pkg_name) == 0u) {
      continue;
    }
    auto saved_builder = std::move(builder_);
    builder_           = Lnast_builder();
    builder_.lnast     = std::make_shared<Lnast>(pkg_name);  // no lambda_kind → a namespace unit
    builder_.lnast->set_package_unit();
    builder_.lnast->set_pending_srcid(hhds::SourceId_invalid);
    auto root          = builder_.lnast->set_root(Lnast_ntype::create_top());
    builder_.idx_stmts = builder_.lnast->add_child(root, Lnast_ntype::create_stmts());
    if (auto uit = unit_imports.find(pkg_name); uit != unit_imports.end()) {
      for (const auto& ip : uit->second) {
        builder_.lnast->add_imported_package(ip);
      }
    }
    std::vector<std::pair<std::string, std::string>> pub_vals;
    absl::flat_hash_map<std::string, std::string>    expr_m;
    absl::flat_hash_map<std::string, std::string>    type_m;
    std::set<std::string>                            emitted;
    auto emit_one = [&](const std::string& param_name, const Pinfo& pi) {
      // `const` binds its value from a SEPARATE store, not the declare init
      // (only `reg` folds the init into the declare — prp2lnast). A declare-init
      // const does not bind as a known comptime value, so harvest_pub_values
      // rejects it.
      builder_.create_declare_stmts(param_name, "const comptime", "", "");
      builder_.create_assign_stmts(param_name, pi.value);
      builder_.lnast->add_pub(param_name, "value");
      pub_vals.emplace_back(param_name, pi.value);
      // a defining expr may only read names DECLARED ABOVE it in this file
      const bool refs_ok = std::all_of(pi.same_refs.begin(), pi.same_refs.end(),
                                       [&](const std::string& n) { return emitted.count(n) != 0; });
      if (!pi.expr.empty() && refs_ok) {
        expr_m.emplace(param_name, pi.expr);
      }
      if (!pi.type.empty()) {
        type_m.emplace(param_name, pi.type);
      }
      emitted.insert(param_name);
    };
    // source order: walk the package's members; the scope exposes enum members
    // transparently, so anything unmatched falls back to a name-ordered tail.
    if (auto sit = referenced_pkg_syms_.find(pkg_name); sit != referenced_pkg_syms_.end()) {
      for (const auto& member : sit->second->members()) {
        std::string nm(member.name);
        if (auto pit = params.find(nm); pit != params.end() && emitted.count(nm) == 0 && !pit->second.value.empty()) {
          emit_one(nm, pit->second);
        }
      }
    }
    for (const auto& [param_name, pi] : params) {
      if (emitted.count(param_name) == 0 && !pi.value.empty()) {
        emit_one(param_name, pi);
      }
    }
    // Port-dim scalar type aliases (`pub type VPU_FCMD_SZ_T = u7`): pub kind
    // "type", carrying NO value. The range is emitted structurally, as the
    // declare's `prim_type_int(max, 0)` — the same shape the Pyrope front-end
    // produces — so the import machinery reads it off the tree via
    // Lnast::pub_type_face. The `uN` print text rides package_const_types_.
    if (auto tit = referenced_pkg_types_.find(pkg_name); tit != referenced_pkg_types_.end()) {
      for (const auto& [alias, face_text] : tit->second) {
        builder_.create_declare_stmts(alias, "type", face_text.first, "0");
        builder_.lnast->add_pub(alias, "type");
        type_m.emplace(alias, face_text.second);
      }
    }
    // Stamp pub_values_ NOW: the prp_writer's package-unit path reads only
    // get_pub_values(), and the constprop harvest that normally fills it does
    // not run with compile.upass.constprop=0 (its skip guard tolerates a
    // pre-stamped unit, so constprop=1 is unaffected).
    builder_.lnast->set_pub_values(std::move(pub_vals));
    builder_.lnast->set_package_const_exprs(std::move(expr_m));
    builder_.lnast->set_package_const_types(std::move(type_m));
    ordered_lnasts_.push_back(builder_.lnast);
    builder_ = std::move(saved_builder);
  }
  referenced_pkg_params_.clear();
  referenced_pkg_syms_.clear();
  referenced_pkg_types_.clear();
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
  // A user symbol whose sanitized name sits in LiveHD's reserved tmp namespace
  // (Lnast::is_tmp — a leading `%`, e.g. from an escaped Verilog id `\%x`) would
  // make every "<base>_sN" uniquing candidate also a temp, so the loop below
  // would spin forever. Push such a name out of the tmp namespace once, before
  // uniquing. (A `___`-leading name is now an ordinary identifier, not a temp.)
  if (Lnast::is_tmp(base)) {
    base.insert(0, "usr");  // "%x" -> "usr%x"
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
