
#pragma once

// clang-format off
#include "slang/symbols/ASTVisitor.h"

#include <vector>

#include "pass.hpp"
#include "lnast_create.hpp"
// clang-format on

class Slang_tree {
public:
  Slang_tree();

  void process_root(const slang::RootSymbol& root);

  std::vector<std::shared_ptr<Lnast>> pick_lnast();

protected:
  Lnast_create lnast_create_obj;

  absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> parsed_lnasts;

  enum class Net_attr { Input, Output, Register, Local };

  bool has_lnast(std::string_view name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  bool        process_top_instance(const slang::InstanceSymbol& symbol);
  bool        process(const slang::AssignmentExpression& expr);

  void        process_lhs(const slang::Expression &lhs, const std::string &rhs_var, bool last_value);

  std::string process_expression(const slang::Expression& expr, bool last_value);
  std::string process_mask_and(const slang::UnaryExpression& uexpr, bool last_value);
  std::string process_mask_popcount(const slang::UnaryExpression& uexpr, bool last_value);
  std::string process_mask_xor(const slang::UnaryExpression& uexpr, bool last_value);
};
