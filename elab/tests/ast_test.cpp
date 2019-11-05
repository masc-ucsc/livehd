
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "ast.hpp"
#include "fmt/format.h"

class Ast_test_setup : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
  }
};

std::unique_ptr<Ast_parser> ast;

class Test_scanner: public Elab_scanner{
public:
  enum test_rules: Rule_id
  {
    test_rule_invalid = 0,
    test_rule,
    test_rule_top,
    test_rule_identifier,
    test_rule_add,
    test_rule_statement,
  };


  void elaborate(){
    ast = std::make_unique<Ast_parser>(get_buffer(), test_rule);

    EXPECT_TRUE(scan_is_token(Token_id_alnum));
    std::string test = scan_text();

    if (test == "a") {
      ast->down();
      ast->add(test_rule_identifier, scan_token());
      ast->add(test_rule_top, scan_token());
      ast->up(test_rule_statement);
    }else if (test == "b") {
      ast->down();
      ast->down();
      ast->add(test_rule_identifier, scan_token());
      ast->add(test_rule_top, scan_token());
      ast->up(test_rule_statement);
      ast->up(test_rule_add);
    }else if (test == "c") {
      ast->down();
      ast->add(test_rule_identifier, scan_token());
      ast->down();
      ast->add(test_rule_top, scan_token());
      ast->up(test_rule_statement);
      ast->up(test_rule_add);
    }else{
      EXPECT_TRUE(false); // test should match
    }

  }
};

TEST_F(Ast_test_setup, ast_trivial) {

  char statement[] = "a+b=c\n";

  Test_scanner scanner;

  Elab_scanner::Token_list tlist;
  scanner.parse("test", statement, tlist);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, Test_scanner::test_rules::test_rule_identifier);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,1)).rule_id, Test_scanner::test_rules::test_rule_top);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivial2) {

  char statement[] = "b\n";

  Test_scanner scanner;

  Elab_scanner::Token_list tlist;
  scanner.parse("test", statement, tlist);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, Test_scanner::test_rules::test_rule_add);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, Test_scanner::test_rules::test_rule_identifier);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,1)).rule_id, Test_scanner::test_rules::test_rule_top);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivialc) {

  char statement[] = "c\n";

  Test_scanner scanner;

  Elab_scanner::Token_list tlist;
  scanner.parse("test", statement, tlist);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, Test_scanner::test_rules::test_rule_identifier);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, Test_scanner::test_rules::test_rule_top);

  ast = nullptr;
}
