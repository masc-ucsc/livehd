
#include "ast.hpp"

#include <format>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "str_tools.hpp"

namespace {
// Local diagnostic helper (replacement for the deleted core/tree_compat.hpp
// shim). `level_of` walks parents — fine for this test's pre-order scan.
inline int32_t level_of(const hhds::Tree::Node_class& nid) {
  int32_t d = 0;
  auto    p = nid.parent();
  while (p.is_valid()) {
    ++d;
    p = p.parent();
  }
  return d;
}
}  // namespace

class Ast_test_setup : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {}
};

std::unique_ptr<Ast_parser> ast;

// Mirrors the legacy lhtree (level, pos) indexing: returns the `pos`-th node
// (0-based) at depth `level` in left-to-right pre-order.
static hhds::Tree::Node_class node_at(const Ast_parser& a, int level, int pos) {
  int idx = 0;
  for (const auto& nid : a.depth_preorder()) {
    if (level_of(nid) == level) {
      if (idx == pos) {
        return nid;
      }
      ++idx;
    }
  }
  return {};
}

class Test_scanner : public Elab_scanner {
public:
  enum test_rules : Rule_id {
    test_rule_invalid = 0,
    test_rule,
    test_rule_top,
    test_rule_identifier,
    test_rule_add,
    test_rule_statement,
  };

  void elaborate() {
    patch_pass();  // Fix token_id_num or allow custom

    ast = std::make_unique<Ast_parser>(get_memblock(), test_rule);

    while (!scan_is_end()) {
      EXPECT_TRUE(scan_is_token(Token_id_alnum));
      auto cmd = scan_text();
      scan_next();

      if (cmd == "up") {
        EXPECT_TRUE(scan_is_token(Token_id_num));
        auto val = scan_text();
        scan_next();

        EXPECT_TRUE(str_tools::is_i(val));
        auto    rid_int = str_tools::to_i(val);
        Rule_id rid     = static_cast<Rule_id>(rid_int);

        ast->up(rid);
      } else if (cmd == "down") {
        ast->down();
      } else if (cmd == "add") {
        EXPECT_TRUE(scan_is_token(Token_id_num));
        auto val = scan_text();  // const auto & better but KEPT on purpose to test copy too
        scan_next();

        auto    rid_int = str_tools::to_i(val);
        Rule_id rid     = static_cast<Rule_id>(rid_int);

        ast->add(rid, scan_token_entry());
      } else {
        EXPECT_TRUE(false);  // What cmd??
      }
    }
  }
};

TEST_F(Ast_test_setup, ast_trivial) {
  char statement[] = "down add 13 add 17 up 18\n";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 0)).rule_id, 13);
  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 1)).rule_id, 17);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivial2) {
  char statement[] = " down down add 1 add 2 up 3 up 4 ";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 0)).rule_id, 4);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 0)).rule_id, 1);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 1)).rule_id, 2);

  ast = nullptr;
}

TEST_F(Ast_test_setup, ast_trivialc) {
  char statement[] = " down add 3 down add 6 up 7 up 8";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 0)).rule_id, 6);

  ast = nullptr;
}

TEST_F(Ast_test_setup, pseudo_eprp) {
  char statement[] = " down add 3 down add 6 down up 7 add 8 up 9";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 0)).rule_id, 6);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 1)).rule_id, 8);

  ast = nullptr;
}

TEST_F(Ast_test_setup, pseudo_eprp3) {
  char statement[] = " down add 3 down add 6 down down down down up 66 up 33 add 88 up 77 up 7 add 8 up 9";

  Test_scanner scanner;

  scanner.parse_inline(statement);

  EXPECT_EQ(ast->get_data(node_at(*ast, 1, 0)).rule_id, 3);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 0)).rule_id, 6);
  EXPECT_EQ(ast->get_data(node_at(*ast, 2, 1)).rule_id, 8);
  EXPECT_EQ(ast->get_data(node_at(*ast, 3, 0)).rule_id, 7);
  EXPECT_EQ(ast->get_data(node_at(*ast, 4, 0)).rule_id, 88);

  ast = nullptr;
}
