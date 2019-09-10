#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"

#include "prp.hpp"
#include "fmt/format.h"

class Prp_test: public ::testing::Test{
public:

  class Prp_test_class : public Prp {
  protected:
    void elaborate(){
      patch_pass(pyrope_keyword);
      ast = std::make_unique<Ast_parser>(get_buffer(), Prp_rule);
      ast->down();
      std::cerr << "Starting to parse\n";
      while(!scan_is_end()){
        dump_token();
        eat_comments();
        if(scan_is_end()) return;
        if(!rule_top()){
          std::cerr << "Something went wrong.\n";
          return;
        }
      }
      std::cerr << "Finished Parsing\n";
      ast->up(Prp_rule_code_blocks);

      ast_handler();

      ast = nullptr;
    }
    void ast_handler(){
      std::string rule_name;
      for(const auto &it:ast->depth_preorder(ast->get_root())) {
        auto node = ast->get_data(it);
        switch(node.rule_id){
          case Prp_invalid:
            rule_name.assign("Invalid");
            break;
          case Prp_rule:
            rule_name.assign("Program");
            break;
          case Prp_rule_code_blocks:
            rule_name.assign("Top level");
            break;
          case Prp_rule_code_block_int:
            rule_name.assign("Code block");
            break;
          case Prp_rule_assignment_expression:
            rule_name.assign("Assignment expression");
            break;
          case Prp_rule_logical_expression:
            rule_name.assign("Logical expression");
            break;
          case Prp_rule_relational_expression:
            rule_name.assign("Relational expression");
            break;
          case Prp_rule_additive_expression:
            rule_name.assign("Additive expression");
            break;
          case Prp_rule_bitwise_expression:
            rule_name.assign("Bitwise expression");
            break;
          case Prp_rule_multiplicative_expression:
            rule_name.assign("Multiplicative expression");
            break;
          case Prp_rule_unary_expression:
            rule_name.assign("Unary expressiion");
            break;
          case Prp_rule_factor:
            rule_name.assign("Factor");
            break;
          case Prp_rule_tuple_by_notation:
            rule_name.assign("Tuple by notation");
            break;
          case Prp_rule_tuple_notation_no_bracket:
            rule_name.assign("Tuple notation non bracket");
            break;
          case Prp_rule_tuple_notation:
            rule_name.assign("Tuple notation");
            break;
          case Prp_rule_tuple_notation_with_object:
            rule_name.assign("Tuple notation with object");
            break;
          case Prp_rule_range_notation:
            rule_name.assign("Range notation");
            break;
          case Prp_rule_bit_selection_bracket:
            rule_name.assign("Bit selection bracket");
            break;
          case Prp_rule_bit_selection_notation:
            rule_name.assign("Bit selection notation");
            break;
          case Prp_rule_tuple_array_bracket:
            rule_name.assign("Tuple array bracket");
            break;
          case Prp_rule_tuple_array_notation:
            rule_name.assign("Tuple array notation");
            break;
          case Prp_rule_lhs_expression:
            rule_name.assign("LHS expression");
            break;
          case Prp_rule_lhs_var_name:
            rule_name.assign("LHS variable name");
            break;
          case Prp_rule_rhs_expression_property:
            rule_name.assign("RHS expression property");
            break;
          case Prp_rule_rhs_expression:
            rule_name.assign("RHS expression");
            break;
          case Prp_rule_identifier:
            rule_name.assign("Identifier");
            break;
          case Prp_rule_constant:
            rule_name.assign("Constant");
            break;
          case Prp_rule_assignment_operator:
            rule_name.assign("Assignment operator");
            break;
          case Prp_rule_tuple_dot_notation:
            rule_name.assign("Tuple dot notation");
            break;
          case Prp_rule_tuple_dot_dot:
            rule_name.assign("Tuple dot dot");
            break;
        }
        auto token_text = scan_text(node.token_entry);
        tree_traversal_tokens.push_back(token_text);
        tree_traversal_rules.push_back(rule_name);
        std::cerr << "Rule name: "<< rule_name << ", Token text: " << token_text << ", Tree level: " << it.level << std::endl;
      }
    }
  public:
    std::vector<std::string_view> tree_traversal_tokens;
    std::vector<std::string> tree_traversal_rules;
  };
};

/*TEST_F(Prp_test, assignment_expression0) {
  std::vector<std::string_view> tree_traversal_check_tokens;
  std::vector<std::string> tree_traversal_check_rules;
  
  tree_traversal_check_tokens.push_back(""); // program
  tree_traversal_check_tokens.push_back(""); // top level
  tree_traversal_check_tokens.push_back(""); // assignment expression
  tree_traversal_check_tokens.push_back("\%out");
  tree_traversal_check_tokens.push_back("=");
  tree_traversal_check_tokens.push_back(""); // logical expression
  tree_traversal_check_tokens.push_back(""); // relational expression
  tree_traversal_check_tokens.push_back(""); // additive expression
  tree_traversal_check_tokens.push_back("b");
  tree_traversal_check_tokens.push_back("+");
  tree_traversal_check_tokens.push_back("c");
  
  tree_traversal_check_rules.push_back("Program");
  tree_traversal_check_rules.push_back("Top level");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Identifier");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Logical expression");
  tree_traversal_check_rules.push_back("Relational expression");
  tree_traversal_check_rules.push_back("Additive expression");
  tree_traversal_check_rules.push_back("Identifier");
  tree_traversal_check_rules.push_back("Additive expression");
  tree_traversal_check_rules.push_back("Identifier");
  
  std::string_view parse_txt("\%out=b+c\n");
  
  scanner.parse("assignment_expression0", parse_txt.data(), 1);
  EXPECT_EQ(tree_traversal_check_tokens, scanner.tree_traversal_tokens);
  EXPECT_EQ(tree_traversal_check_rules, scanner.tree_traversal_rules);
}*/

TEST_F(Prp_test, assignment_expression1){
  Prp_test_class scanner;
  
  std::vector<std::string_view> tree_traversal_check_tokens;
  std::vector<std::string> tree_traversal_check_rules;
  
  tree_traversal_check_tokens.push_back(""); // program
  tree_traversal_check_tokens.push_back(""); // top level
  tree_traversal_check_tokens.push_back(""); // assignment expression
  tree_traversal_check_tokens.push_back("\%out");
  tree_traversal_check_tokens.push_back("as");
  tree_traversal_check_tokens.push_back(""); // logical expression
  tree_traversal_check_tokens.push_back(""); // relational expression
  tree_traversal_check_tokens.push_back(""); // additive expression
  tree_traversal_check_tokens.push_back(""); // bitwise expression
  tree_traversal_check_tokens.push_back(""); // tuple_notation
  tree_traversal_check_tokens.push_back("(");
  tree_traversal_check_tokens.push_back(""); // rhs_expression_property
  tree_traversal_check_rules.push_back("__bits:");
  tree_traversal_check_rules.push_back("8");
  tree_traversal_check_rules.push_back(")");
  
  tree_traversal_check_rules.push_back("Program");
  tree_traversal_check_rules.push_back("Top level");
  tree_traversal_check_rules.push_back("Assignment expression");
  tree_traversal_check_rules.push_back("Identifier");
  tree_traversal_check_rules.push_back("Assignment operator");
  tree_traversal_check_rules.push_back("Logical expression");
  tree_traversal_check_rules.push_back("Relational expression");
  tree_traversal_check_rules.push_back("Additive expression");
  tree_traversal_check_rules.push_back("Bitwise expression");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("Tuple notation");
  tree_traversal_check_rules.push_back("RHS expression property");
  tree_traversal_check_rules.push_back("Identifier");
  tree_traversal_check_rules.push_back("Constant");
  tree_traversal_check_rules.push_back("Tuple notation");
  
  std::string_view parse_txt("\%out as (__bits:8)\n");
  
  Elab_scanner::Token_list tlist;
  scanner.parse("assignment_expression1", parse_txt.data(), tlist);
  EXPECT_EQ(tree_traversal_check_tokens, scanner.tree_traversal_tokens);
  EXPECT_EQ(tree_traversal_check_rules, scanner.tree_traversal_rules);
}
