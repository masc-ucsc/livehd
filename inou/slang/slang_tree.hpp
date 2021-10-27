
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

  static void setup();
  static void process_root(const slang::RootSymbol& root);

  static std::vector<std::shared_ptr<Lnast>> pick_lnast();

protected:
  Lnast_create lnast_create_obj;

  static inline absl::flat_hash_map<mmap_lib::str, std::shared_ptr<Lnast>> parsed_lnasts;

  enum class Net_attr { Input, Output, Register, Local };

  static bool has_lnast(mmap_lib::str name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  bool          process_top_instance(const slang::InstanceSymbol& symbol);
  bool          process(const slang::AssignmentExpression& expr);
  mmap_lib::str process_expression(const slang::Expression& expr);
  mmap_lib::str process_mask_and(const slang::UnaryExpression& uexpr);
  mmap_lib::str process_mask_popcount(const slang::UnaryExpression& uexpr);
  mmap_lib::str process_mask_xor(const slang::UnaryExpression& uexpr);
};
