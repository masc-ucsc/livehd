

#include <vector>
#include <iostream>
#include <functional>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "mmap_tree.hpp"
using namespace std::chrono;

class Elab_test : public ::testing::Test {

  std::vector<std::vector<std::string>> ast_sorted_verification;

public:
  mmap_lib::tree<std::string> ast;
  mmap_lib::tree<std::string> tree;

  void SetUp() override {

    // Smaller tree
    tree.set_root("root");
    auto n10 = tree.add_child(mmap_lib::Tree_index(0,0), "n1.0");
    I(n10.level == 1 && n10.pos == 0);
    auto n11 = tree.add_child(mmap_lib::Tree_index(0,0), "n1.1");
    I(n11.level == 1 && n11.pos == 1);
    auto n20 = tree.add_child(mmap_lib::Tree_index(1,0), "n2.0");
    I(n20.level == 2 && n20.pos == 0);
    auto n21 = tree.add_child(mmap_lib::Tree_index(1,0), "n2.1");
    I(n21.level == 2 && n21.pos == 1);
    auto n24 = tree.add_child(mmap_lib::Tree_index(1,1), "n2.4");
    I(n24.level == 2 && n24.pos == 4);
    auto n25 = tree.add_child(mmap_lib::Tree_index(1,1), "n2.5");
    I(n25.level == 2 && n25.pos == 5);

    // Larger tree
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

/*
TEST_F(Elab_test, Traverse_breadth_first_check_on_ast) {
  // auto start = std::chrono::steady_clock::now();

  std::vector<std::vector<std::string>> ast2_sorted_verification;

  ast.each_bottom_up_fast([this,&ast2_sorted_verification](const mmap_lib::Tree_index &self, const std::string &str) {
      while (static_cast<size_t>(self.level)>=ast2_sorted_verification.size())
        ast2_sorted_verification.emplace_back();
      ast2_sorted_verification[self.level].emplace_back(str);
      auto parent = ast.get_parent(self);
      std::cout << str << " parent:" << ast.get_data(parent) << "\n";
  });

  // auto end = std::chrono::steady_clock::now();

  check_against_ast(ast2_sorted_verification);

  // std::chrono::duration<double> elapsed = end-start;
  // std::cout << "elapsed " << elapsed.count() << "s\n";
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
*/

// Testing delete_subtree and delete_leaf on the two constructed trees
TEST_F(Elab_test, Delete_check) {
  std::vector<std::string> ast_preorder_traversal;
  std::vector<std::string> ast_preorder_traversal2;

  for(const auto &it:ast.depth_preorder(ast.get_root())) {
    ast_preorder_traversal.push_back(ast.get_data(it));
  }
  auto size_before = ast_preorder_traversal.size();

  ast.dump_data();
  // tree.dump_data();
  std::cout << "--------------------------------------------------------------------------\n";
  ast.delete_subtree(mmap_lib::Tree_index(2,0));
  ast.dump_data();
  // tree.delete_subtree(mmap_lib::Tree_index(2,5));
  // tree.dump_data();

  for(const auto &it:ast.depth_preorder(ast.get_root())) {
    ast_preorder_traversal2.push_back(ast.get_data(it));
  }
  auto size_after = ast_preorder_traversal2.size();

  // size_before should be greater than size_after as we removed a subtree from it
  EXPECT_GT(size_before, size_after);

  // Adding a leaf and then deleting it
  // ast.dump_data();
  // ast.add_child(mmap_lib::Tree_index(0,0), "child1.0");
  // ast.delete_leaf(mmap_lib::Tree_index(1,1));
  // ast.dump_data();

  // Test delete_leaf on root
  // auto root = ast.get_root();
  // ast.delete_subtree(root);

  // Delete some leaves
  // ast.delete_leaf(mmap_lib::Tree_index(4, 0));
  // ast.delete_leaf(mmap_lib::Tree_index(4, 1));
  // ast.dump_data();

  // Check for leaves in preorder
  // for(const auto &it:ast.depth_preorder(ast.get_root())) {
  //   // if(ast.is_leaf(it))
  //   {
  //     std::cout << ast.get_data(it) << " \tlevel: " << it.level << " \tpos " << it.pos;
  //     if(ast.is_leaf(it))
  //       std::cout << "\tleaf";
  //     std::cout << std::endl;
  //   }
  // }

  // Testing out postorder with leaf check
  // for(const auto &it:ast.depth_postorder(ast.get_root())) {
  //   // if(ast.is_leaf(it))
  //   {
  //     std::cout << ast.get_data(it) << " \tlevel: " << it.level << " \tpos " << it.pos;
  //     if(ast.is_leaf(it))
  //       std::cout << "\tleaf";
  //     std::cout << std::endl;
  //   }
  // }
}
