
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

  void elaborate() {
    patch_pass(); // Fix token_id_num or allow custom

    ast = std::make_unique<Ast_parser>(get_memblock(), test_rule);

    while(!scan_is_end()) {
      EXPECT_TRUE(scan_is_token(Token_id_alnum));
      auto cmd = scan_text();
      scan_next();

      if (cmd == "up") {
        EXPECT_TRUE(scan_is_token(Token_id_num));
        std::string val{scan_text()};
        scan_next();

        auto rid_int = std::atoi(val.c_str());
        Rule_id rid = static_cast<Rule_id>(rid_int);

        ast->up(rid);
      }else if (cmd == "down") {

        ast->down();
      }else if (cmd == "add") {
        EXPECT_TRUE(scan_is_token(Token_id_num));
        std::string val{scan_text()};
        scan_next();

        auto rid_int = std::atoi(val.c_str());
        Rule_id rid = static_cast<Rule_id>(rid_int);

        ast->add(rid, scan_token());
      }else{
        EXPECT_TRUE(false); // What cmd??
      }
    }
  }
};

TEST_F(Ast_test_setup, ast_trivial) {

  char statement[] = "down add 13 add 17 up 18\n";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, 13);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,1)).rule_id, 17);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivial2) {

  char statement[] = " down down add 1 add 2 up 3 up 4 ";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, 4);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, 1);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,1)).rule_id, 2);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivialc) {

  char statement[] = " down add 3 down add 6 up 7 up 8";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, 6);

  ast = nullptr;
}

TEST_F(Ast_test_setup, pseudo_eprp) {

  char statement[] = " down add 3 down add 6 down up 7 add 8 up 9";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, 6);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,1)).rule_id, 8);

  ast = nullptr;
}


TEST_F(Ast_test_setup, pseudo_eprp3) {

  char statement[] = " down add 3 down add 6 down down down down up 66 up 33 add 88 up 77 up 7 add 8 up 9";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(1,0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,0)).rule_id, 6);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(2,1)).rule_id, 8);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(3,0)).rule_id, 7);
  EXPECT_EQ(ast->get_data(mmap_lib::Tree_index(4,0)).rule_id, 88);

  ast = nullptr;
}
