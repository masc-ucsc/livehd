
#pragma once

#include <memory>
#include <vector>

// clang-format off
#include "slang/ast/ASTVisitor.h"

#include "pass.hpp"
#include "lnast_builder.hpp"
// clang-format on

class Slang_tree {
public:
  Slang_tree();

  void process_root(const slang::ast::RootSymbol& root);

  std::vector<std::shared_ptr<Lnast>> pick_lnast();

protected:
  Lnast_builder lnast_builder;

  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> parsed_lnasts;

  enum class Net_attr { Input, Output, Register, Local };

  bool has_lnast(std::string_view name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  bool process_top_instance(const slang::ast::InstanceSymbol& symbol);
  bool process(const slang::ast::AssignmentExpression& expr);

  void process_lhs(const slang::ast::Expression& lhs, const std::string& rhs_var, bool last_value);

  std::string process_expression(const slang::ast::Expression& expr, bool last_value);
};
