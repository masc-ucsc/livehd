

#include <vector>
#include <iostream>
#include <functional>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "mmap_tree.hpp"

class Elab_test : public ::testing::Test {

  std::vector<std::vector<std::string>> ast_sorted_verification;

public:
  mmap_lib::tree<std::string> ast;

  void SetUp() override {

    ast.set_root("root");

    auto c11 = ast.add_child(mmap_lib::Tree_index(0,0), "child1.0");
    I(c11.level == 1 && c11.pos == 0);
    auto c12 = ast.add_child(mmap_lib::Tree_index(0,0), "child1.2");
    I(c12.level == 1 && c12.pos == 1);

    auto c111 = ast.add_child(c11, "child1.0.0");
    I(c111.level == 2 && c11.pos == 0);
    auto c112 = ast.add_child(c11, "child1.0.1");
    I(c112.level == 2 && c112.pos == 1);
    auto c1111 = ast.add_child(c111, "child1.0.0.0");
    I(c1111.level == 3 && c1111.pos == 0);

    mmap_lib::Tree_index s;
    s = ast.insert_next_sibling(c1111, "child1.0.0.6");
    I(ast.get_depth_preorder_next(c1111)==s);
    s = ast.insert_next_sibling(c1111, "child1.0.0.5");
    I(ast.get_depth_preorder_next(c1111)==s);
    s = ast.insert_next_sibling(c1111, "child1.0.0.4");
    I(ast.get_depth_preorder_next(c1111)==s);
    s = ast.insert_next_sibling(c1111, "child1.0.0.3");
    I(ast.get_depth_preorder_next(c1111)==s);
    s = ast.insert_next_sibling(c1111, "child1.0.0.2");
    I(ast.get_depth_preorder_next(c1111)==s);
    s = ast.insert_next_sibling(c1111, "child1.0.0.1");
    I(ast.get_depth_preorder_next(c1111)==s);

    auto c121 = ast.add_child(c12, "child1.2.0");
    I(c121.level == 2 && c121.pos == 4);
    auto c122 = ast.add_child(c12, "child1.2.1");
    I(c122.level == 2 && c122.pos == 5);
    auto c123 = ast.add_child(c12, "child1.2.2");
    I(c123.level == 2 && c123.pos == 6);
    auto c113 = ast.add_child(c11, "child1.0.3");
    ast.add_child(c113, "child1.0.3.0");
    ast.add_child(c113, "child1.0.3.1");
    ast.add_child(c113, "child1.0.3.2");

    s = ast.add_child(c112, "child1.0.1.0");
    s= ast.add_child(c112, "child1.0.1.1");
    ast.insert_next_sibling(s, "child1.0.1.6");
    s = ast.insert_next_sibling(s, "child1.0.1.2");
    ast.add_child(s, "child1.0.1.2.0");
    ast.add_child(s, "child1.0.1.2.1");
    s = ast.insert_next_sibling(s, "child1.0.1.3");
    s = ast.insert_next_sibling(s, "child1.0.1.4");
    s = ast.insert_next_sibling(s, "child1.0.1.5");
    s = ast.add_child(c112, "child1.0.1.7");
    ast.insert_next_sibling(s, "child1.0.1.16");
    s = ast.insert_next_sibling(s, "child1.0.1.8");
    ast.insert_next_sibling(s, "child1.0.1.15");
    ast.insert_next_sibling(s, "child1.0.1.14");
    s = ast.insert_next_sibling(s, "child1.0.1.9");
    ast.insert_next_sibling(s, "child1.0.1.13");
    ast.insert_next_sibling(s, "child1.0.1.12");
    ast.insert_next_sibling(s, "child1.0.1.11");
    s = ast.insert_next_sibling(s, "child1.0.1.10");

    auto c114 = ast.insert_next_sibling(c113, "child1.0.4");
    ast.add_child(c113, "child1.0.3.3");
    ast.add_child(c113, "child1.0.3.4");
    ast.add_child(c113, "child1.0.3.5");
    auto c115 = ast.append_sibling(c113, "child1.0.5");
    auto c116 = ast.append_sibling(c115, "child1.0.6");
    auto c13  = ast.append_sibling(c12, "child1.3");

    s = ast.insert_next_sibling(c112, "child1.0.2");

    s = ast.insert_next_sibling(c11, "child1.1");

    ast.each_bottom_up_fast([this](const mmap_lib::Tree_index &self, std::string str) {
      while (static_cast<size_t>(self.level)>=ast_sorted_verification.size())
        ast_sorted_verification.emplace_back();
      ast_sorted_verification[self.level].emplace_back(str);
    });

    for(auto &a:ast_sorted_verification) {
      std::sort(a.begin(), a.end());
    }
  }

  void check_against_ast(std::vector<std::vector<std::string>> &ast2_sorted_verification) {
    for(auto &a:ast2_sorted_verification) {
      std::sort(a.begin(), a.end());
    }
    EXPECT_EQ(ast_sorted_verification,ast2_sorted_verification);
  }
};

TEST_F(Elab_test, Traverse_breadth_first_check_on_ast) {

  std::vector<std::vector<std::string>> ast2_sorted_verification;

  ast.each_bottom_up_fast([this,&ast2_sorted_verification](const mmap_lib::Tree_index &self, const std::string &str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      auto parent = ast.get_parent(self);
      std::cout << str << " parent:" << ast.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
}

TEST_F(Elab_test, Traverse_bottom_first_check_on_ast) {

  std::vector<std::vector<std::string>> ast2_sorted_verification;

  ast.each_bottom_up_fast([this,&ast2_sorted_verification](const mmap_lib::Tree_index &self, const std::string &str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      auto parent = ast.get_parent(self);
      std::cout << str << " parent:" << ast.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
}

TEST_F(Elab_test, Preorder_traversal_check) {
  std::vector<std::string> ast_preorder_traversal_golden;
  std::vector<std::string> ast_preorder_traversal;

  ast_preorder_traversal_golden.push_back("root");
  ast_preorder_traversal_golden.push_back("child1.0");
  ast_preorder_traversal_golden.push_back("child1.0.0");
  ast_preorder_traversal_golden.push_back("child1.0.0.0");
  ast_preorder_traversal_golden.push_back("child1.0.0.1");
  ast_preorder_traversal_golden.push_back("child1.0.0.2");
  ast_preorder_traversal_golden.push_back("child1.0.0.3");
  ast_preorder_traversal_golden.push_back("child1.0.0.4");
  ast_preorder_traversal_golden.push_back("child1.0.0.5");
  ast_preorder_traversal_golden.push_back("child1.0.0.6");
  ast_preorder_traversal_golden.push_back("child1.0.1");
  ast_preorder_traversal_golden.push_back("child1.0.1.0");
  ast_preorder_traversal_golden.push_back("child1.0.1.1");
  ast_preorder_traversal_golden.push_back("child1.0.1.2");
  ast_preorder_traversal_golden.push_back("child1.0.1.2.0");
  ast_preorder_traversal_golden.push_back("child1.0.1.2.1");
  ast_preorder_traversal_golden.push_back("child1.0.1.3");
  ast_preorder_traversal_golden.push_back("child1.0.1.4");
  ast_preorder_traversal_golden.push_back("child1.0.1.5");
  ast_preorder_traversal_golden.push_back("child1.0.1.6");
  ast_preorder_traversal_golden.push_back("child1.0.1.7");
  ast_preorder_traversal_golden.push_back("child1.0.1.8");
  ast_preorder_traversal_golden.push_back("child1.0.1.9");
  ast_preorder_traversal_golden.push_back("child1.0.1.10");
  ast_preorder_traversal_golden.push_back("child1.0.1.11");
  ast_preorder_traversal_golden.push_back("child1.0.1.12");
  ast_preorder_traversal_golden.push_back("child1.0.1.13");
  ast_preorder_traversal_golden.push_back("child1.0.1.14");
  ast_preorder_traversal_golden.push_back("child1.0.1.15");
  ast_preorder_traversal_golden.push_back("child1.0.1.16");
  ast_preorder_traversal_golden.push_back("child1.0.2");
  ast_preorder_traversal_golden.push_back("child1.0.3");
  ast_preorder_traversal_golden.push_back("child1.0.3.0");
  ast_preorder_traversal_golden.push_back("child1.0.3.1");
  ast_preorder_traversal_golden.push_back("child1.0.3.2");
  ast_preorder_traversal_golden.push_back("child1.0.3.3");
  ast_preorder_traversal_golden.push_back("child1.0.3.4");
  ast_preorder_traversal_golden.push_back("child1.0.3.5");
  ast_preorder_traversal_golden.push_back("child1.0.4");
  ast_preorder_traversal_golden.push_back("child1.0.5");
  ast_preorder_traversal_golden.push_back("child1.0.6");
  ast_preorder_traversal_golden.push_back("child1.1");
  ast_preorder_traversal_golden.push_back("child1.2");
  ast_preorder_traversal_golden.push_back("child1.2.0");
  ast_preorder_traversal_golden.push_back("child1.2.1");
  ast_preorder_traversal_golden.push_back("child1.2.2");
  ast_preorder_traversal_golden.push_back("child1.3");

  for(const auto &it:ast.depth_preorder(ast.get_root())) {
    ast_preorder_traversal.push_back(ast.get_data(it));
  }

  EXPECT_EQ(ast_preorder_traversal_golden.size(), ast_preorder_traversal.size());

  for(auto i=0u;i<ast_preorder_traversal.size();++i) {
    fmt::print("ref:{} gld:{}\n", ast_preorder_traversal[i], ast_preorder_traversal_golden[i]);
    EXPECT_EQ(ast_preorder_traversal[i], ast_preorder_traversal_golden[i]);
  }

}
