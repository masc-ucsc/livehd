//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdio>
#include <format>
#include <iostream>

#include "gtest/gtest.h"
#include "prp.hpp"

class Prp_test : public ::testing::Test {
public:
  class Prp_test_class : public Prp {
  protected:
    void elaborate() {
      patch_pass(pyrope_keyword);

      ast = std::make_unique<Ast_parser>(get_memblock(), Prp_rule);
      std::list<std::tuple<Rule_id, Token_entry>> loc_list;
      gen_ws_map();

      int      failed  = 0;
      uint64_t sub_cnt = 0;

      if (!CHECK_RULE(&Prp_test_class::rule_start)) {
        failed = 1;
      }

      if (failed) {
        std::cout << "\nParsing FAILED!\n";
        return;
      } else {
        std::cout << "\nParsing SUCCESSFUL!\n";
      }

      // build the ast
      ast_builder(loc_list);
      ast_handler();
      ast = nullptr;
    }
    void ast_handler() {
      for (const auto &it : ast->depth_preorder()) {
        auto node       = ast->get_data(it);
        auto token_text = scan_text(node.token_entry);
        if (token_text != "") {
          tree_traversal_tokens.push_back(token_text);
        }
        tree_traversal_rules.emplace_back(rule_id_to_string(node.rule_id));
        std::print("Rule name: {}, Token text: {}, Tree level: {}\n",
                   tree_traversal_rules.back(),
                   scan_text(node.token_entry),
                   it.level);
      }
    }

  public:
    std::vector<std::string_view> tree_traversal_tokens;
    std::vector<std::string>      tree_traversal_rules;
  };
};

TEST_F(Prp_test, assignment_expression1) {
  Prp_test_class scanner;

  std::vector<std::string_view> tree_traversal_check_tokens;
  std::vector<std::string>      tree_traversal_check_rules;

  tree_traversal_check_tokens.push_back("\%out");
  tree_traversal_check_tokens.push_back("as");
  tree_traversal_check_tokens.push_back("(");
  tree_traversal_check_tokens.push_back("__bits");
  tree_traversal_check_tokens.push_back("=");
  tree_traversal_check_tokens.push_back("8");
  tree_traversal_check_tokens.push_back(")");

  tree_traversal_check_rules.push_back("Program");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Tuple notation");

  scanner.parse_inline("\%out as (__bits=8)\n");
  EXPECT_EQ(tree_traversal_check_tokens, scanner.tree_traversal_tokens);
  EXPECT_EQ(tree_traversal_check_rules, scanner.tree_traversal_rules);
}

TEST_F(Prp_test, assignment_expression2) {
  Prp_test_class scanner;

  std::vector<std::string_view> tree_traversal_check_tokens;
  std::vector<std::string>      tree_traversal_check_rules;

  tree_traversal_check_tokens.push_back("\%out");
  tree_traversal_check_tokens.push_back("as");
  tree_traversal_check_tokens.push_back("(");
  tree_traversal_check_tokens.push_back("__bits");
  tree_traversal_check_tokens.push_back("=");
  tree_traversal_check_tokens.push_back("10");
  tree_traversal_check_tokens.push_back(",");
  tree_traversal_check_tokens.push_back("__bits");
  tree_traversal_check_tokens.push_back("as");
  tree_traversal_check_tokens.push_back("10");
  tree_traversal_check_tokens.push_back(")");

  tree_traversal_check_rules.push_back("Program");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Tuple notation");

  scanner.parse_inline("\%out as (__bits=10,__bits as 10)\n");
  EXPECT_EQ(tree_traversal_check_tokens, scanner.tree_traversal_tokens);
  EXPECT_EQ(tree_traversal_check_rules, scanner.tree_traversal_rules);
}

TEST_F(Prp_test, if_statement1) {
  Prp_test_class scanner;

  std::vector<std::string_view> tree_traversal_check_tokens;
  std::vector<std::string>      tree_traversal_check_rules;

  tree_traversal_check_tokens.push_back("if");
  tree_traversal_check_tokens.push_back("x");
  tree_traversal_check_tokens.push_back(">");
  tree_traversal_check_tokens.push_back("5");
  tree_traversal_check_tokens.push_back("{");
  tree_traversal_check_tokens.push_back("b");
  tree_traversal_check_tokens.push_back("=");
  tree_traversal_check_tokens.push_back("10");
  tree_traversal_check_tokens.push_back("c");
  tree_traversal_check_tokens.push_back("=");
  tree_traversal_check_tokens.push_back("10");
  tree_traversal_check_tokens.push_back("+");
  tree_traversal_check_tokens.push_back("x");
  tree_traversal_check_tokens.push_back("}");

  tree_traversal_check_rules.push_back("Program");
  tree_traversal_check_rules.push_back("If statement");
  tree_traversal_check_rules.push_back("If statement");
  tree_traversal_check_rules.push_back("Relational expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Relational expression");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Empty scope colon");
  tree_traversal_check_rules.push_back("Block body");
  tree_traversal_check_rules.push_back("Code blocks");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Additive expression");
  tree_traversal_check_rules.push_back("Numerical constant");
  tree_traversal_check_rules.push_back("Additive expression");
  tree_traversal_check_rules.push_back("Reference");
  tree_traversal_check_rules.push_back("Block body");

  scanner.parse_inline("if (x>5){\nb = 10\n c = 10 + x\n}\n");
  EXPECT_EQ(tree_traversal_check_tokens, scanner.tree_traversal_tokens);
  EXPECT_EQ(tree_traversal_check_rules, scanner.tree_traversal_rules);
}
