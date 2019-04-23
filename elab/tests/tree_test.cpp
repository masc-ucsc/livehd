

#include <vector>
#include <iostream>
#include <functional>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "tree.hpp"

class Elab_test : public ::testing::Test {

  std::vector<std::vector<std::string>> ast_sorted_verification;

public:
  Tree<std::string> ast;

  void SetUp() override {

    ast.set_root("root");

    auto c11 = ast.add_child(Tree_index(0,0), "child1.1");
    auto c12 = ast.add_child(Tree_index(0,0), "child1.2");

    auto c111 = ast.add_child(c11, "child1.1.1");
    auto c112 = ast.add_child(c11, "child1.1.2");
    (void)c112;
    auto c1111 = ast.add_child(c111, "child1.1.1.1");
    (void)c1111;
    auto c121 = ast.add_child(c12, "child1.2.1");
    (void)c121;
    auto c122 = ast.add_child(c12, "child1.2.2");
    (void)c122;
    auto c123 = ast.add_child(c12, "child1.2.3");
    (void)c123;
    auto c113 = ast.add_child(c11, "child1.1.3");
    (void)c113;
    auto c114 = ast.add_sibling(c113, "child1.1.4");
    (void)c114;
    auto c13  = ast.add_sibling(c12, "child1.3");
    (void)c13;

    ast.each_breadth_first_fast([this](const Tree_index &parent, const Tree_index &self, std::string str) {
      while (static_cast<size_t>(self.level)>=ast_sorted_verification.size())
        ast_sorted_verification.emplace_back();
      ast_sorted_verification[self.level].emplace_back(str);
      EXPECT_EQ(ast.get_parent(self), parent);
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

  ast.each_breadth_first_fast([this,&ast2_sorted_verification](const Tree_index &parent, const Tree_index &self, const std::string &str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      EXPECT_EQ(ast.get_parent(self), parent);
      std::cout << str << " parent:" << ast.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
}

TEST_F(Elab_test, Traverse_bottom_first_check_on_ast) {

  std::vector<std::vector<std::string>> ast2_sorted_verification;

  ast.each_bottom_first_fast([this,&ast2_sorted_verification](const Tree_index &parent, const Tree_index &self, const std::string &str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      EXPECT_EQ(ast.get_parent(self), parent);
      std::cout << str << " parent:" << ast.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
}

TEST_F(Elab_test, Create_with_lazy_check) {

  Tree<std::string> ast2;

  ast2.set_root("bad root root");
  ast2.set_root("worse root root");

  ast2.add_lazy_child(3, "child1.1.1.1");

  ast2.set_root("root");

  ast2.add_lazy_child(2, "child1.1.1");
  ast2.add_lazy_child(2, "child1.1.2");
  ast2.add_lazy_child(2, "child1.1.3");
  ast2.add_lazy_child(2, "child1.1.4");
  ast2.add_lazy_child(1, "child1.1");

  ast2.add_lazy_child(2, "child1.2.1");
  ast2.add_lazy_child(2, "child1.2.2");
  ast2.add_lazy_child(2, "child1.2.3");
  ast2.add_lazy_child(1, "child1.2");

  ast2.add_lazy_child(1, "child1.3");

  std::cout << "-----------LAZY\n";
  std::vector<std::vector<std::string>> ast2_sorted_verification;

  ast2.each_bottom_first_fast([&ast2,&ast2_sorted_verification](const Tree_index &parent, const Tree_index &self, std::string str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      EXPECT_EQ(ast2.get_parent(self), parent);
      std::cout << str << " parent:" << ast2.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
  ast2_sorted_verification.clear();

  ast2.each_breadth_first_fast([&ast2,&ast2_sorted_verification](const Tree_index &parent, const Tree_index &self, std::string str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      EXPECT_EQ(ast2.get_parent(self), parent);
      std::cout << str << " parent:" << ast2.get_data(parent) << "\n";
  });

  check_against_ast(ast2_sorted_verification);
}

TEST_F(Elab_test, Preorder_traversal_check) {
  std::vector<std::string> ast_preorder_traversal_check;
  std::vector<std::string> ast_preorder_traversal;

  ast_preorder_traversal_check.push_back("root");
  ast_preorder_traversal_check.push_back("child1.1");
  ast_preorder_traversal_check.push_back("child1.1.1");
  ast_preorder_traversal_check.push_back("child1.1.1.1");
  ast_preorder_traversal_check.push_back("child1.1.2");
  ast_preorder_traversal_check.push_back("child1.1.3");
  ast_preorder_traversal_check.push_back("child1.1.4");
  ast_preorder_traversal_check.push_back("child1.2");
  ast_preorder_traversal_check.push_back("child1.2.1");
  ast_preorder_traversal_check.push_back("child1.2.2");
  ast_preorder_traversal_check.push_back("child1.2.3");
  ast_preorder_traversal_check.push_back("child1.3");

  Tree_index node = ast.get_root();

  for(const auto &it:ast.depth_preorder(ast.get_root())) {
    ast_preorder_traversal.push_back(ast.get_data(it));
  }

  for(auto it = begin(ast_preorder_traversal); it != end(ast_preorder_traversal); ++it){
    fmt::print("{}\n", *it);
  }

  EXPECT_EQ(ast_preorder_traversal, ast_preorder_traversal_check);
}
