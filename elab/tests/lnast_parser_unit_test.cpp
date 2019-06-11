//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>  //for getcwd()
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "gtest/gtest.h"
#include "fmt/format.h"
#include "lnast.hpp"
/*
K1   K2   0  0   16  :   ___b  __bits  0d1
K2   K3   0  0   16  ()  ___a  ___b
K3   K4   0  0   16  as  $a    ___a
END
*/
using tuple = std::tuple<std::string, std::string , uint8_t>;// <node_name, node_type, scope>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;
  std::vector<std::vector<tuple>> ast_preorder_golden;

public:
  Tree<tuple>  ast;
  Lnast_parser lnast_parser;

  void SetUp() override {
    //root and statement
    ast.set_root(std::make_tuple("", ntype_dbg(Lnast_ntype_top), 0));
    auto c1    = ast.add_child(Tree_index(0,0), std::make_tuple("K1", ntype_dbg(Lnast_ntype_statement), 0));

    auto c11   = ast.add_child(c1,   std::make_tuple("K1",     ntype_dbg(Lnast_ntype_lable), 0));
    auto c111  = ast.add_child(c11,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c112  = ast.add_child(c11,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1121 = ast.add_child(c112, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c111; // for turn off un-used warning
    (void) c1121;

    auto c12   = ast.add_child(c1,   std::make_tuple("K2",     ntype_dbg(Lnast_ntype_as), 0));
    auto c121  = ast.add_child(c12,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
    auto c122  = ast.add_child(c12,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c121;
    (void) c122;

    auto c13   = ast.add_child(c1,   std::make_tuple("K3",     ntype_dbg(Lnast_ntype_lable), 0));
    auto c131  = ast.add_child(c13,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c132  = ast.add_child(c13,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1321 = ast.add_child(c132, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c131; // for turn off un-used warning
    (void) c1321;

    auto c14   = ast.add_child(c1,   std::make_tuple("K4",     ntype_dbg(Lnast_ntype_as), 0));
    auto c141  = ast.add_child(c14,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
    auto c142  = ast.add_child(c14,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c141;
    (void) c142;

    auto c15   = ast.add_child(c1,   std::make_tuple("K5",     ntype_dbg(Lnast_ntype_lable), 0));
    auto c151  = ast.add_child(c15,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c152  = ast.add_child(c15,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1521 = ast.add_child(c152, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c151; // for turn off un-used warning
    (void) c1521;

    auto c16   = ast.add_child(c1,   std::make_tuple("K6",     ntype_dbg(Lnast_ntype_as), 0));
    auto c161  = ast.add_child(c16,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
    auto c162  = ast.add_child(c16,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c161;
    (void) c162;

    auto c17   = ast.add_child(c1,   std::make_tuple("K7",     ntype_dbg(Lnast_ntype_and), 0));
    auto c171  = ast.add_child(c17,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c172  = ast.add_child(c17,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
    auto c173  = ast.add_child(c17,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
    (void) c171;
    (void) c172;
    (void) c173;

    auto c18   = ast.add_child(c1,   std::make_tuple("K8",     ntype_dbg(Lnast_ntype_pure_assign), 0));
    auto c181  = ast.add_child(c18,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
    auto c182  = ast.add_child(c18,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c181;
    (void) c182;

    auto c19    = ast.add_child(c1,     std::make_tuple("K11",  ntype_dbg(Lnast_ntype_sub),1));
    auto c191   = ast.add_child(c19,    std::make_tuple("K11",  ntype_dbg(Lnast_ntype_statement),1));
    auto c1911  = ast.add_child(c191,   std::make_tuple("K11",  ntype_dbg(Lnast_ntype_plus),1));
    auto c19111 = ast.add_child(c1911,  std::make_tuple("___f", ntype_dbg(Lnast_ntype_ref),1));
    auto c19112 = ast.add_child(c1911,  std::make_tuple("$a",   ntype_dbg(Lnast_ntype_input),1));
    auto c19113 = ast.add_child(c1911,  std::make_tuple("$b",   ntype_dbg(Lnast_ntype_input),1));
    (void) c19111;
    (void) c19112;
    (void) c19113;


    auto c1912   = ast.add_child(c191,   std::make_tuple("K12",    ntype_dbg(Lnast_ntype_pure_assign), 1));
    auto c19121  = ast.add_child(c1912,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
    auto c19122  = ast.add_child(c1912,  std::make_tuple("___f",   ntype_dbg(Lnast_ntype_ref), 1));
    (void) c19121;
    (void) c19122;


    auto c1913   = ast.add_child(c191,   std::make_tuple("K9",     ntype_dbg(Lnast_ntype_func_def), 1));
    auto c19131  = ast.add_child(c1913,  std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref), 1));
    auto c19132  = ast.add_child(c1913,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 1));
    auto c19133  = ast.add_child(c1913,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 1));
    auto c19134  = ast.add_child(c1913,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
    (void) c19131;
    (void) c19132;
    (void) c19133;
    (void) c19134;

    auto c1a  = ast.add_child(c1,  std::make_tuple("K15",    ntype_dbg(Lnast_ntype_lable), 0));
    auto c1a1 = ast.add_child(c1a, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1a2 = ast.add_child(c1a, std::make_tuple("a",      ntype_dbg(Lnast_ntype_ref),   0));
    auto c1a3 = ast.add_child(c1a, std::make_tuple("0d3",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c1a1;
    (void) c1a2;
    (void) c1a3;


    auto c1b  = ast.add_child(c1,  std::make_tuple("K16",    ntype_dbg(Lnast_ntype_lable), 0));
    auto c1b1 = ast.add_child(c1b, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1b2 = ast.add_child(c1b, std::make_tuple("b",      ntype_dbg(Lnast_ntype_ref),   0));
    auto c1b3 = ast.add_child(c1b, std::make_tuple("0d4",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c1b1;
    (void) c1b2;
    (void) c1b3;

    auto c1c  = ast.add_child(c1, std::make_tuple("K17",     ntype_dbg(Lnast_ntype_func_call), 0));
    auto c1c1 = ast.add_child(c1c, std::make_tuple("___g",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c2 = ast.add_child(c1c, std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c3 = ast.add_child(c1c, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c4 = ast.add_child(c1c, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
    (void) c1c1;
    (void) c1c2;
    (void) c1c3;
    (void) c1c4;


    auto c1d   = ast.add_child(c1,   std::make_tuple("K18",     ntype_dbg(Lnast_ntype_pure_assign), 0));
    auto c1d1  = ast.add_child(c1d,  std::make_tuple("result",  ntype_dbg(Lnast_ntype_ref), 0));
    auto c1d2  = ast.add_child(c1d,  std::make_tuple("___g",    ntype_dbg(Lnast_ntype_ref), 0));
    (void) c1d1;
    (void) c1d2;

    ast.each_breadth_first_fast([this](const Tree_index &parent, const Tree_index &self, tuple node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();
      ast_sorted_golden[self.level].emplace_back(node_data);
      EXPECT_EQ(ast.get_parent(self), parent);
    });


    for(auto &a:ast_sorted_golden) {
        std::sort(a.begin(), a.end());
    }

    for (const auto &it:ast.depth_preorder(ast.get_root())) {
        while (static_cast<size_t>(it.level)>=ast_preorder_golden.size())
            ast_preorder_golden.emplace_back();
        ast_preorder_golden[it.level].emplace_back(ast.get_data(it));
    }

    setup_testee();
  }

  void check_sorted_against_ast(std::vector<std::vector<tuple>> &ast_sorted_testee) {
      for(auto &a:ast_sorted_testee) {
          std::sort(a.begin(), a.end());
      }
      EXPECT_EQ(ast_sorted_golden, ast_sorted_testee);
  }

  void check_preorder_against_ast(std::vector<std::vector<tuple>> &ast_preorder_testee) {
      EXPECT_EQ(ast_preorder_golden, ast_preorder_testee);
  }

  std::string get_current_working_dir(){
      std::string cwd("\0", FILENAME_MAX + 1);
      return getcwd(&cwd[0],cwd.capacity());
  }

  std::string_view setup_memblock(){
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
      fprintf(stderr, "Content of memblock: \n%s\n", memblock);
      if(memblock == MAP_FAILED) {
          fprintf(stderr, "error, mmap failed\n");
          exit(-3);
      }
      return memblock;
  }

  void setup_testee(){
    std::string_view memblock = setup_memblock();
    lnast_parser.parse("lnast", memblock);
  }
};

TEST_F(Lnast_test, Traverse_breadth_first_check_on_ast) {

    std::vector<std::vector<tuple>> ast_sorted_testee;
    std::string_view memblock = setup_memblock();

    lnast_parser.get_ast()->each_breadth_first_fast([this, &ast_sorted_testee, &memblock](const Tree_index &parent, const Tree_index &self, const Lnast_node &node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_testee.size())
          ast_sorted_testee.emplace_back();

      std::string node_name(node_data.node_token.get_text(memblock));
      std::string node_type  = ntype_dbg(node_data.node_type);
      auto        node_scope = node_data.scope;
      std::string pname(lnast_parser.get_ast()->get_data(parent).node_token.get_text(memblock));
      std::string ptype  = ntype_dbg(lnast_parser.get_ast()->get_data(parent).node_type);
      auto        pscope = lnast_parser.get_ast()->get_data(parent).scope;
      fmt::print("nname:{}, ntype:{}, nscope:{}\n", node_name, node_type, node_scope);
      fmt::print("pname:{}, ptype:{}, pscope:{}\n\n", pname, ptype, pscope);

      tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);
      ast_sorted_testee[self.level].emplace_back(tuple_data);
      EXPECT_EQ(lnast_parser.get_ast()-> get_parent(self), parent);
    });

    check_sorted_against_ast(ast_sorted_testee);
}


TEST_F(Lnast_test,Traverse_preorder_traverse_check_on_lnast){

    std::vector<std::vector<tuple>> ast_preorder_testee;
    std::string_view memblock = setup_memblock();

    for (const auto &it: lnast_parser.get_ast()->depth_preorder(lnast_parser.get_ast()->get_root()) ) {
        std::string node_name(lnast_parser.get_ast()->get_data(it).node_token.get_text(memblock));
        std::string node_type  = ntype_dbg(lnast_parser.get_ast()->get_data(it).node_type);
        auto        node_scope = lnast_parser.get_ast()->get_data(it).scope;
        tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);

        while (static_cast<size_t>(it.level)>=ast_preorder_testee.size())
            ast_preorder_testee.emplace_back();

        ast_preorder_testee[it.level].emplace_back(tuple_data);
    }

    check_preorder_against_ast(ast_preorder_testee);
}

