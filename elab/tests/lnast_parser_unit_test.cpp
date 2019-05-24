//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>  //for getcwd()
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "fmt/format.h"
#include "lnast.hpp"
/*
K1   K2   0  0   16  :   ___b  __bits  0d1
K2   K3   0  0   16  ()  ___a  ___b
K3   K4   0  0   16  as  $a    ___a
END
*/
using tuple = std::tuple<std::string, uint8_t , uint8_t>;// <node_name, node_type, scope>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;

public:
  Tree<tuple>  ast;
  Lnast_parser lnast_parser;

  void SetUp() override {
    //root and statement
    ast.set_root(std::make_tuple("K1", Lnast_ntype_top, 0));
    auto c11    = ast.add_child(Tree_index(0,0), std::make_tuple("K1", Lnast_ntype_statement, 0));

    auto c111   = ast.add_child(c11,   std::make_tuple("K1",     Lnast_ntype_lable, 0));
    auto c1111  = ast.add_child(c111,  std::make_tuple("___b",   Lnast_ntype_ref, 0));
    auto c1112  = ast.add_child(c111,  std::make_tuple("__bits", Lnast_ntype_attr_bits, 0));
    auto c11121 = ast.add_child(c1112, std::make_tuple("0d1",    Lnast_ntype_const, 0));
    (void) c1111; // for turn off un-used warning
    (void) c11121;

    auto c112   = ast.add_child(c11,   std::make_tuple("K2",     Lnast_ntype_tuple, 0));
    auto c1121  = ast.add_child(c112,  std::make_tuple("___a",   Lnast_ntype_ref, 0));
    auto c1122  = ast.add_child(c112,  std::make_tuple("___b",   Lnast_ntype_ref, 0));
    (void) c1121;
    (void) c1122;


    auto c113   = ast.add_child(c11,   std::make_tuple("K3",     Lnast_ntype_as, 0));
    auto c1131  = ast.add_child(c113,  std::make_tuple("$a",     Lnast_ntype_ref, 0));
    auto c1132  = ast.add_child(c113,  std::make_tuple("___a",   Lnast_ntype_ref, 0));
    (void) c1131;
    (void) c1132;

    ast.each_breadth_first_fast([this](const Tree_index &parent, const Tree_index &self, tuple tuple_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();
      ast_sorted_golden[self.level].emplace_back(tuple_data);
      EXPECT_EQ(ast.get_parent(self), parent);
    });

    for(auto &a:ast_sorted_golden) {
        std::sort(a.begin(), a.end());
    }

    setup_testee();
  }

  void check_against_ast(std::vector<std::vector<tuple>> &ast_sorted_testee) {
      for(auto &a:ast_sorted_testee) {
          std::sort(a.begin(), a.end());
      }
      EXPECT_EQ(ast_sorted_golden, ast_sorted_testee);
  }

  std::string get_current_working_dir(){
      std::string cwd("\0", FILENAME_MAX + 1);
      return getcwd(&cwd[0],cwd.capacity());
  }

  void setup_testee(){
      std::string tmp_str = get_current_working_dir();
      std::string file_path = tmp_str + "/inou/cfg/tests/ast_test.cfg";
      int fd = open(file_path.c_str(), O_RDONLY);
      if(fd < 0) {
          fprintf(stderr, "error, could not open %s\n", file_path.c_str());
          exit(-3);
      }

      struct stat sb;
      fstat(fd, &sb);
      printf("Size: %lu\n", (uint64_t)sb.st_size);

      char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
      if(memblock == MAP_FAILED) {
          fprintf(stderr, "error, mmap failed\n");
          exit(-3);
      }

      lnast_parser.parse(file_path, memblock, sb.st_size);
  }
};

TEST_F(Lnast_test, Traverse_breadth_first_check_on_ast) {

    std::vector<std::vector<tuple>> ast_sorted_testee;

    lnast_parser.get_ast()->each_breadth_first_fast([this, &ast_sorted_testee](const Tree_index &parent, const Tree_index &self, const Lnast_node &node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_testee.size())
          ast_sorted_testee.emplace_back();

      std::string node_name(lnast_parser.scan_text(node_data.node_name));
      auto        node_type  = node_data.node_type;
      auto        node_scope = node_data.scope;
      fmt::print("nname:{}, ntype:{}, nscope:{}\n", node_name, node_type, node_scope);

      tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);
      ast_sorted_testee[self.level].emplace_back(tuple_data);
      EXPECT_EQ(lnast_parser.get_ast()-> get_parent(self), parent);
    });

    check_against_ast(ast_sorted_testee);
}

//Todo: need to test pre-order print screen
