//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdio>

#include "fmt/format.h"
#include "gtest/gtest.h"

#include "ast.hpp"

class Test_scanner : public Elab_scanner {
protected:
  enum test_rules : Rule_id {
    test_rule_invalid = 0,
    test_rule,
    test_rule_top,
    test_rule_identifier,
    test_rule_add_expression,
    test_rule_statement,
  };

  std::unique_ptr<Ast_parser> ast;

public:
  void elaborate() {
    ast = std::make_unique<Ast_parser>(get_memblock(), test_rule);

    ast->down();
    ast->add(test_rule_identifier, scan_token());
    ast->up(test_rule_identifier);

    ast_handler();

    ast = nullptr;
  }

  void ast_handler() {
    for (const auto &it : ast->depth_preorder(ast->get_root())) {
      auto        node       = ast->get_data(it);
      auto        rule_value = node.rule_id;
      std::string rule_name;
      switch (rule_value) {
        case test_rule: rule_name.assign("test_rule"); break;
        case test_rule_invalid: rule_name.assign("test_rule_invalid"); break;
        case test_rule_top: rule_name.assign("test_rule_top"); break;
        case test_rule_identifier: rule_name.assign("test_rule_identifier"); break;
        case test_rule_add_expression: rule_name.assign("test_rule_add_expression"); break;
        case test_rule_statement: rule_name.assign("test_rule_statement"); break;
      }
      auto token_text = scan_text(node.token_entry);
      fmt::print("Rule name: {}, Token text: {}\n", rule_name, token_text);
    }
  }
};

int main(int argc, char **argv) {
  char statement[] = "a+b=c\n";

  Test_scanner scanner;
  scanner.parse_inline(statement);
}
