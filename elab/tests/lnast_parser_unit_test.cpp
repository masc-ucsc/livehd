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
 * CFG text input for lnast parser
K1   K2    0  0   14   :    ___a    __bits  0d1
K2   K3    0  0   14   as   $a      ___a
K3   K4    0  15  29   :    ___b    __bits  0d1
K4   K5    0  15  29   as   $b      ___b
K5   K6    0  30  44   :    ___c    __bits  0d1
K6   K7    0  30  44   as   %s      ___c
K7   K8    0  45  57   &    ___d    $a      $b
K8   K9    0  45  57   =    %s      ___d
K11  K12   1  59  96   +    ___f    $a      $b
K12  null  1  59  96   =    %o      ___f
K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
K14  K15   0  59  96   =    fun1    \___e
K15  K16   0  98  121  :    ___h    a       0d3
K16  K17   0  98  121  :    ___i    b       0d4
K17  K18   0  98  121  .()  ___g    fun1    ___h  ___i
K18  K19   0  98  121  =    result  ___g
END
*/

using tuple = std::tuple<std::string, std::string , uint8_t>;// <node_name, node_type, scope>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;
  std::vector<std::vector<tuple>> ast_preorder_golden;

public:
  Tree<tuple>  ast_gld;
  Lnast_parser lnast_parser;

  void SetUp() override {
    //root and statement
    ast_gld.set_root(std::make_tuple("", ntype_dbg(Lnast_ntype_top), 0));
    auto c1    = ast_gld.add_child(Tree_index(0,0), std::make_tuple("K1", ntype_dbg(Lnast_ntype_statement), 0));

    auto c11   = ast_gld.add_child(c1,   std::make_tuple("K1",     ntype_dbg(Lnast_ntype_label), 0));
    auto c111  = ast_gld.add_child(c11,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c112  = ast_gld.add_child(c11,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1121 = ast_gld.add_child(c112, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c111; // for turn off un-used warning
    (void) c1121;

    auto c12   = ast_gld.add_child(c1,   std::make_tuple("K2",     ntype_dbg(Lnast_ntype_as), 0));
    auto c121  = ast_gld.add_child(c12,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
    auto c122  = ast_gld.add_child(c12,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c121;
    (void) c122;

    auto c13   = ast_gld.add_child(c1,   std::make_tuple("K3",     ntype_dbg(Lnast_ntype_label), 0));
    auto c131  = ast_gld.add_child(c13,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c132  = ast_gld.add_child(c13,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1321 = ast_gld.add_child(c132, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c131; // for turn off un-used warning
    (void) c1321;

    auto c14   = ast_gld.add_child(c1,   std::make_tuple("K4",     ntype_dbg(Lnast_ntype_as), 0));
    auto c141  = ast_gld.add_child(c14,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
    auto c142  = ast_gld.add_child(c14,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c141;
    (void) c142;

    auto c15   = ast_gld.add_child(c1,   std::make_tuple("K5",     ntype_dbg(Lnast_ntype_label), 0));
    auto c151  = ast_gld.add_child(c15,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c152  = ast_gld.add_child(c15,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
    auto c1521 = ast_gld.add_child(c152, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c151; // for turn off un-used warning
    (void) c1521;

    auto c16   = ast_gld.add_child(c1,   std::make_tuple("K6",     ntype_dbg(Lnast_ntype_as), 0));
    auto c161  = ast_gld.add_child(c16,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
    auto c162  = ast_gld.add_child(c16,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c161;
    (void) c162;

    auto c17   = ast_gld.add_child(c1,   std::make_tuple("K7",     ntype_dbg(Lnast_ntype_and), 0));
    auto c171  = ast_gld.add_child(c17,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
    auto c172  = ast_gld.add_child(c17,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
    auto c173  = ast_gld.add_child(c17,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
    (void) c171;
    (void) c172;
    (void) c173;

    auto c18   = ast_gld.add_child(c1,   std::make_tuple("K8",     ntype_dbg(Lnast_ntype_pure_assign), 0));
    auto c181  = ast_gld.add_child(c18,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
    auto c182  = ast_gld.add_child(c18,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
    (void) c181;
    (void) c182;

    auto c19    = ast_gld.add_child(c1,     std::make_tuple("K11",  ntype_dbg(Lnast_ntype_sub),1));
    auto c191   = ast_gld.add_child(c19,    std::make_tuple("K11",  ntype_dbg(Lnast_ntype_statement),1));
    auto c1911  = ast_gld.add_child(c191,   std::make_tuple("K11",  ntype_dbg(Lnast_ntype_plus),1));
    auto c19111 = ast_gld.add_child(c1911,  std::make_tuple("___f", ntype_dbg(Lnast_ntype_ref),1));
    auto c19112 = ast_gld.add_child(c1911,  std::make_tuple("$a",   ntype_dbg(Lnast_ntype_input),1));
    auto c19113 = ast_gld.add_child(c1911,  std::make_tuple("$b",   ntype_dbg(Lnast_ntype_input),1));
    (void) c19111;
    (void) c19112;
    (void) c19113;


    auto c1912   = ast_gld.add_child(c191,   std::make_tuple("K12",    ntype_dbg(Lnast_ntype_pure_assign), 1));
    auto c19121  = ast_gld.add_child(c1912,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
    auto c19122  = ast_gld.add_child(c1912,  std::make_tuple("___f",   ntype_dbg(Lnast_ntype_ref), 1));
    (void) c19121;
    (void) c19122;


    auto c1913   = ast_gld.add_child(c191,   std::make_tuple("K9",     ntype_dbg(Lnast_ntype_func_def), 1));
    auto c19131  = ast_gld.add_child(c1913,  std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref), 1));
    auto c19132  = ast_gld.add_child(c1913,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 1));
    auto c19133  = ast_gld.add_child(c1913,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 1));
    auto c19134  = ast_gld.add_child(c1913,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
    (void) c19131;
    (void) c19132;
    (void) c19133;
    (void) c19134;

    auto c1a  = ast_gld.add_child(c1,  std::make_tuple("K15",    ntype_dbg(Lnast_ntype_label), 0));
    auto c1a1 = ast_gld.add_child(c1a, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1a2 = ast_gld.add_child(c1a, std::make_tuple("a",      ntype_dbg(Lnast_ntype_ref),   0));
    auto c1a3 = ast_gld.add_child(c1a, std::make_tuple("0d3",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c1a1;
    (void) c1a2;
    (void) c1a3;


    auto c1b  = ast_gld.add_child(c1,  std::make_tuple("K16",    ntype_dbg(Lnast_ntype_label), 0));
    auto c1b1 = ast_gld.add_child(c1b, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1b2 = ast_gld.add_child(c1b, std::make_tuple("b",      ntype_dbg(Lnast_ntype_ref),   0));
    auto c1b3 = ast_gld.add_child(c1b, std::make_tuple("0d4",    ntype_dbg(Lnast_ntype_const), 0));
    (void) c1b1;
    (void) c1b2;
    (void) c1b3;

    auto c1c  = ast_gld.add_child(c1, std::make_tuple("K17",     ntype_dbg(Lnast_ntype_func_call), 0));
    auto c1c1 = ast_gld.add_child(c1c, std::make_tuple("___g",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c2 = ast_gld.add_child(c1c, std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c3 = ast_gld.add_child(c1c, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
    auto c1c4 = ast_gld.add_child(c1c, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
    (void) c1c1;
    (void) c1c2;
    (void) c1c3;
    (void) c1c4;


    auto c1d   = ast_gld.add_child(c1,   std::make_tuple("K18",     ntype_dbg(Lnast_ntype_pure_assign), 0));
    auto c1d1  = ast_gld.add_child(c1d,  std::make_tuple("result",  ntype_dbg(Lnast_ntype_ref), 0));
    auto c1d2  = ast_gld.add_child(c1d,  std::make_tuple("___g",    ntype_dbg(Lnast_ntype_ref), 0));
    (void) c1d1;
    (void) c1d2;

    ast_gld.each_breadth_first_fast([this](const Tree_index &parent, const Tree_index &self, tuple tuple_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();
      ast_sorted_golden[self.level].emplace_back(tuple_data);
      EXPECT_EQ(ast_gld.get_parent(self), parent);
    });


    for(auto &a:ast_sorted_golden) {
        std::sort(a.begin(), a.end());
    }

    for (const auto &it:ast_gld.depth_preorder(ast_gld.get_root())) {
        while (static_cast<size_t>(it.level)>=ast_preorder_golden.size())
            ast_preorder_golden.emplace_back();
        ast_preorder_golden[it.level].emplace_back(ast_gld.get_data(it));
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

    auto lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership
    std::vector<std::vector<tuple>> ast_sorted_testee;
    std::string_view memblock = setup_memblock();

    lnast->each_breadth_first_fast([this, &ast_sorted_testee, &memblock, &lnast] (const Tree_index &parent,
                                                                                  const Tree_index &self,
                                                                                  const Lnast_node &node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_testee.size())
          ast_sorted_testee.emplace_back();

      std::string node_name(node_data.node_token.get_text(memblock));
      std::string node_type  = ntype_dbg(node_data.node_type);
      auto        node_scope = node_data.scope;

      std::string pname(lnast->get_data(parent).node_token.get_text(memblock));
      std::string ptype  = ntype_dbg(lnast->get_data(parent).node_type);
      auto        pscope = lnast->get_data(parent).scope;

      fmt::print("nname:{}, ntype:{}, nscope:{}\n", node_name, node_type, node_scope);
      fmt::print("pname:{}, ptype:{}, pscope:{}\n\n", pname, ptype, pscope);

      tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);
      ast_sorted_testee[self.level].emplace_back(tuple_data);
      EXPECT_EQ(lnast-> get_parent(self), parent);
    });

    check_sorted_against_ast(ast_sorted_testee);
}


TEST_F(Lnast_test,Traverse_preorder_traverse_check_on_lnast){

    auto lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership
    std::vector<std::vector<tuple>> ast_preorder_testee;
    std::string_view memblock = setup_memblock();

    for (const auto &it: lnast->depth_preorder(lnast->get_root()) ) {

        const auto& node_data = lnast->get_data(it);
        std::string node_name(node_data.node_token.get_text(memblock)); //str_view to string
        std::string node_type  = ntype_dbg(node_data.node_type);
        auto        node_scope = node_data.scope;
        tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);

        while (static_cast<size_t>(it.level)>=ast_preorder_testee.size())
            ast_preorder_testee.emplace_back();

        ast_preorder_testee[it.level].emplace_back(tuple_data);
    }

    check_preorder_against_ast(ast_preorder_testee);
}

