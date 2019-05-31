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
using tuple = std::tuple<std::string, uint8_t , uint8_t>;// <node_name, node_type, scope>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;

public:
  Tree<tuple>  ast;
  Lnast_parser lnast_parser;

  void SetUp() override {
    //root and statement
    ast.set_root(std::make_tuple("", Lnast_ntype_top, 0));
    auto c1    = ast.add_child(Tree_index(0,0), std::make_tuple("K1", Lnast_ntype_statement, 0));

    auto c11   = ast.add_child(c1,   std::make_tuple("K1",     Lnast_ntype_lable, 0));
    auto c111  = ast.add_child(c11,  std::make_tuple("___a",   Lnast_ntype_ref, 0));
    auto c112  = ast.add_child(c11,  std::make_tuple("__bits", Lnast_ntype_attr_bits, 0));
    auto c1121 = ast.add_child(c112, std::make_tuple("0d1",    Lnast_ntype_const, 0));
    (void) c111; // for turn off un-used warning
    (void) c1121;

    auto c12   = ast.add_child(c1,   std::make_tuple("K2",     Lnast_ntype_as, 0));
    auto c121  = ast.add_child(c12,  std::make_tuple("$a",     Lnast_ntype_input, 0));
    auto c122  = ast.add_child(c12,  std::make_tuple("___a",   Lnast_ntype_ref, 0));
    (void) c121;
    (void) c122;

    auto c13   = ast.add_child(c1,   std::make_tuple("K3",     Lnast_ntype_lable, 0));
    auto c131  = ast.add_child(c13,  std::make_tuple("___b",   Lnast_ntype_ref, 0));
    auto c132  = ast.add_child(c13,  std::make_tuple("__bits", Lnast_ntype_attr_bits, 0));
    auto c1321 = ast.add_child(c132, std::make_tuple("0d1",    Lnast_ntype_const, 0));
    (void) c131; // for turn off un-used warning
    (void) c1321;

    auto c14   = ast.add_child(c1,   std::make_tuple("K4",     Lnast_ntype_as, 0));
    auto c141  = ast.add_child(c14,  std::make_tuple("$b",     Lnast_ntype_input, 0));
    auto c142  = ast.add_child(c14,  std::make_tuple("___b",   Lnast_ntype_ref, 0));
    (void) c141;
    (void) c142;

    auto c15   = ast.add_child(c1,   std::make_tuple("K5",     Lnast_ntype_lable, 0));
    auto c151  = ast.add_child(c15,  std::make_tuple("___c",   Lnast_ntype_ref, 0));
    auto c152  = ast.add_child(c15,  std::make_tuple("__bits", Lnast_ntype_attr_bits, 0));
    auto c1521 = ast.add_child(c152, std::make_tuple("0d1",    Lnast_ntype_const, 0));
    (void) c151; // for turn off un-used warning
    (void) c1521;

    auto c16   = ast.add_child(c1,   std::make_tuple("K6",     Lnast_ntype_as, 0));
    auto c161  = ast.add_child(c16,  std::make_tuple("%s",     Lnast_ntype_output, 0));
    auto c162  = ast.add_child(c16,  std::make_tuple("___c",   Lnast_ntype_ref, 0));
    (void) c161;
    (void) c162;

    auto c17   = ast.add_child(c1,   std::make_tuple("K7",     Lnast_ntype_and, 0));
    auto c171  = ast.add_child(c17,  std::make_tuple("___d",   Lnast_ntype_ref, 0));
    auto c172  = ast.add_child(c17,  std::make_tuple("$a",     Lnast_ntype_input, 0));
    auto c173  = ast.add_child(c17,  std::make_tuple("$b",     Lnast_ntype_input, 0));
    (void) c171;
    (void) c172;
    (void) c173;

    auto c18   = ast.add_child(c1,   std::make_tuple("K8",     Lnast_ntype_pure_assign, 0));
    auto c181  = ast.add_child(c18,  std::make_tuple("%s",     Lnast_ntype_output, 0));
    auto c182  = ast.add_child(c18,  std::make_tuple("___d",   Lnast_ntype_ref, 0));
    (void) c181;
    (void) c182;

    auto c19    = ast.add_child(c1,     std::make_tuple("K11",  Lnast_ntype_sub,1));
    auto c191   = ast.add_child(c19,    std::make_tuple("K11",  Lnast_ntype_statement,1));
    auto c1911  = ast.add_child(c191,   std::make_tuple("K11",  Lnast_ntype_plus,1));
    auto c19111 = ast.add_child(c1911,  std::make_tuple("___f", Lnast_ntype_ref,1));
    auto c19112 = ast.add_child(c1911,  std::make_tuple("$a",   Lnast_ntype_input,1));
    auto c19113 = ast.add_child(c1911,  std::make_tuple("$b",   Lnast_ntype_input,1));
    (void) c19111;
    (void) c19112;
    (void) c19113;


    auto c1912   = ast.add_child(c191,   std::make_tuple("K12",    Lnast_ntype_pure_assign, 1));
    auto c19121  = ast.add_child(c1912,  std::make_tuple("%o",     Lnast_ntype_output, 1));
    auto c19122  = ast.add_child(c1912,  std::make_tuple("___f",   Lnast_ntype_ref, 1));
    (void) c19121;
    (void) c19122;


    auto c1913   = ast.add_child(c191,   std::make_tuple("K9",     Lnast_ntype_func_def, 1));
    auto c19131  = ast.add_child(c1913,  std::make_tuple("___e",   Lnast_ntype_ref, 1));
    auto c19132  = ast.add_child(c1913,  std::make_tuple("K11",    Lnast_ntype_ref, 1));
    auto c19133  = ast.add_child(c1913,  std::make_tuple("$a",     Lnast_ntype_input, 1));
    auto c19134  = ast.add_child(c1913,  std::make_tuple("$b",     Lnast_ntype_input, 1));
    auto c19135  = ast.add_child(c1913,  std::make_tuple("%o",     Lnast_ntype_output, 1));
    (void) c19131;
    (void) c19132;
    (void) c19133;
    (void) c19134;
    (void) c19135;

    auto c1a  = ast.add_child(c1,  std::make_tuple("K15",    Lnast_ntype_lable, 0));
    auto c1a1 = ast.add_child(c1a, std::make_tuple("___h",   Lnast_ntype_ref,   0));
    auto c1a2 = ast.add_child(c1a, std::make_tuple("a",      Lnast_ntype_ref,   0));
    auto c1a3 = ast.add_child(c1a, std::make_tuple("0d3",    Lnast_ntype_const, 0));
    (void) c1a1;
    (void) c1a2;
    (void) c1a3;


    auto c1b  = ast.add_child(c1,  std::make_tuple("K16",    Lnast_ntype_lable, 0));
    auto c1b1 = ast.add_child(c1b, std::make_tuple("___i",   Lnast_ntype_ref,   0));
    auto c1b2 = ast.add_child(c1b, std::make_tuple("b",      Lnast_ntype_ref,   0));
    auto c1b3 = ast.add_child(c1b, std::make_tuple("0d4",    Lnast_ntype_const, 0));
    (void) c1b1;
    (void) c1b2;
    (void) c1b3;

    auto c1c  = ast.add_child(c1, std::make_tuple("K17",     Lnast_ntype_func_call, 0));
    auto c1c1 = ast.add_child(c1c, std::make_tuple("___g",   Lnast_ntype_ref,   0));
    auto c1c2 = ast.add_child(c1c, std::make_tuple("fun1",   Lnast_ntype_ref,   0));
    auto c1c3 = ast.add_child(c1c, std::make_tuple("___h",   Lnast_ntype_ref,   0));
    auto c1c4 = ast.add_child(c1c, std::make_tuple("___i",   Lnast_ntype_ref,   0));
    (void) c1c1;
    (void) c1c2;
    (void) c1c3;
    (void) c1c4;


    auto c1d   = ast.add_child(c1,   std::make_tuple("K18",     Lnast_ntype_pure_assign, 0));
    auto c1d1  = ast.add_child(c1d,  std::make_tuple("result",  Lnast_ntype_ref, 0));
    auto c1d2  = ast.add_child(c1d,  std::make_tuple("___g",    Lnast_ntype_ref, 0));
    (void) c1d1;
    (void) c1d2;

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
      auto        node_type  = node_data.node_type;
      auto        node_scope = node_data.scope;
      fmt::print("nname:{}, ntype:{}, nscope:{}\n", node_name, node_type, node_scope);

      tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);
      ast_sorted_testee[self.level].emplace_back(tuple_data);
      EXPECT_EQ(lnast_parser.get_ast()-> get_parent(self), parent);
    });

    check_against_ast(ast_sorted_testee);
}

//todo: need to test node by node when pre-order traverse
