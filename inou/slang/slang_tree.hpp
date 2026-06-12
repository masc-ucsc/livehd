
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// clang-format off
#include "slang/ast/ASTVisitor.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

#include "pass.hpp"
#include "lnast_builder.hpp"
// clang-format on

class Slang_tree {
public:
  Slang_tree();

  // The driver hands the compilation's SourceManager before process_root so the
  // importer can mint provenance (subtask D) and located diagnostics (subtask C).
  void set_source_manager(const slang::SourceManager* sm) { sm_ = sm; }

  void process_root(const slang::ast::RootSymbol& root);

  std::vector<std::shared_ptr<Lnast>> pick_lnast();

protected:
  const slang::SourceManager* sm_ = nullptr;

  Lnast_builder lnast_builder;

  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> parsed_lnasts;

  enum class Net_attr { Input, Output, Register, Local };

  bool has_lnast(std::string_view name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  bool process_top_instance(const slang::ast::InstanceSymbol& symbol);
  bool process(const slang::ast::AssignmentExpression& expr);

  void process_lhs(const slang::ast::Expression& lhs, const std::string& rhs_var, bool last_value);

  std::string process_expression(const slang::ast::Expression& expr, bool last_value);

  // ── provenance + diagnostics (subtasks C & D), all through one seam ────────
  // Mint a SourceId for `range` into the current lnast's locator.
  hhds::SourceId mint_loc(slang::SourceRange range);
  hhds::SourceId mint_loc(slang::SourceLocation loc) { return mint_loc(slang::SourceRange(loc, loc)); }
  // Set the lnast's pending SourceId so add_child stamps def-bearing nodes.
  void set_pending_loc(slang::SourceRange range) {
    if (lnast_builder.lnast) {
      lnast_builder.lnast->set_pending_srcid(mint_loc(range));
    }
  }
  void set_pending_loc(slang::SourceLocation loc) { set_pending_loc(slang::SourceRange(loc, loc)); }
  void clear_pending_loc() {
    if (lnast_builder.lnast) {
      lnast_builder.lnast->set_pending_srcid(hhds::SourceId_invalid);
    }
  }
  // Emit a located `category=unsupported` error for an SV construct the direct
  // reader does not yet lower, instead of asserting / printing FIXME.
  void emit_unsupported(slang::SourceRange range, std::string_view code, std::string message, std::string_view hint = {});
  void emit_unsupported(slang::SourceLocation loc, std::string_view code, std::string message, std::string_view hint = {}) {
    emit_unsupported(slang::SourceRange(loc, loc), code, std::move(message), hint);
  }
};
