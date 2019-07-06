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
#include "lnast_parser.hpp"

using tuple     = std::tuple<std::string, std::string>;          // <type, name>
using tuple_ssa = std::tuple<std::string, std::string, uint16_t>;// <type, name, sbs>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;
  std::vector<std::vector<tuple>> ast_preorder_golden;

public:
  Tree<tuple>  ast_gld;
  Lnast_parser lnast_parser;

  void SetUp() override {
    setup_lnast_golden();
    setup_lnast_testee();

    setup_lnast_ssa_golden();
    setup_lnast_ssa_testee();
  }

void setup_lnast_ssa_golden(){
  ;//SH:Todo
}

void setup_lnast_ssa_testee(){
  ;//SH:Todo
}


/*
 *
CFG text input for lnast parser
END
K1   K2    0  0    14    :    ___a    __bits  0d1
K2   K3    0  0    14    as   $a      ___a
K3   K4    0  15   29    :    ___b    __bits  0d1
K4   K5    0  15   29    as   $b      ___b
K5   K6    0  30   44    :    ___c    __bits  0d1
K6   K7    0  30   44    as   %s      ___c
K7   K8    0  45   57    &    ___d    $a      $b
K8   K9    0  45   57    =    %s      ___d
K9   K14   0  59   96    ::{  ___e    null    K11   $a    $b  %o  null
K11  K12   1  59   96    ^    ___f    $a      $b
K12  null  1  59   96    =    %o      ___f
K14  K15   0  59   96    =    fun1    \___e
K15  K16   0  98   121   :    ___h    a       0d3
K16  K17   0  98   121   :    ___i    b       0d4
K17  K18   0  98   121   .()  ___g    fun1    ___h  ___i
K18  K19   0  98   121   =    result  ___g

K19  K20   0  123  129   .()  ___j    $a
K20  K21   0  123  129   =    x       ___j
K21  K22   0  130  265   >    ___k    $a      0d1

K22  K47   0  130  265   if   ___k    K24     K44
K24  K25   0  130  265   .()  ___l    $e
K25  K26   0  130  265   =    x       ___l
K26  K27   0  130  265   >    ___m    $a      0d2
K27  K41   0  130  265   if   ___m    K29     K32
K29  K30   0  130  265   .()  ___n    $b
K30  null  0  130  265   =    x       ___n
K32  K33   0  130  265   +    ___p    $a      0d1
K33  K34   0  130  265   >    ___o    ___p    0d3
K34  null  0  130  265   if   ___o    K36     K39
K36  K37   0  130  265   .()  ___q    $c
K37  null  0  130  265   =    x       ___q
K39  K40   0  130  265   .()  ___r    $d
K40  null  0  130  265   =    x       ___r
K41  K42   0  130  265   .()  ___s    $e
K42  null  0  130  265   =    y       ___s
K44  K45   0  130  265   .()  ___t    $f
K45  null  0  130  265   =    x       ___t
K47  K48   0  267  279   +    ___u    x       $a
K48  K49   0  267  279   =    %o1     ___u
K49  K50   0  280  292   +    ___v    y       $a
K50  K51   0  280  292   =    %o2     ___v

 *
 */

  void setup_lnast_golden(){
    //root and statement
    ast_gld.set_root(std::make_tuple(ntype_dbg(Lnast_ntype_top), ""));
    auto top_sts = ast_gld.add_child(Tree_index(0,0), std::make_tuple(ntype_dbg(Lnast_ntype_statement), "K1"));

    //auto K1      = ast_gld.add_child(top_sts,   std::make_tuple(Lnast_ntype_label,     "___a"  , 0));
    //auto K1_op1  = ast_gld.add_child(K1,        std::make_tuple(Lnast_ntype_attr_bits, "__bits", 0));
    //auto K2_op2  = ast_gld.add_child(K1_op1,    std::make_tuple(Lnast_ntype_const,     "0d1"   , 0));

    //auto K2    = ast_gld.add_child(top_sts,     std::make_tuple(Lnast_ntype_as,     "$a"  , 0));
    //auto c122  = ast_gld.add_child(c12,  std::make_tuple(Lnast_ntype_ref,    "___a", 0));
    //(void) c122;

    //auto c13   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_label,     "___b"  , 0));
    //auto c132  = ast_gld.add_child(c13,  std::make_tuple(Lnast_ntype_attr_bits, "__bits", 0));
    //auto c1321 = ast_gld.add_child(c132, std::make_tuple(Lnast_ntype_const,     "0d1"   , 0));
    //(void) c1321;

    //auto c14   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_as,     "$b"  , 0));
    //auto c142  = ast_gld.add_child(c14,  std::make_tuple(Lnast_ntype_ref,    "___b", 0));
    //(void) c142;

    //auto c15   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_label,     "___c"  , 0));
    //auto c152  = ast_gld.add_child(c15,  std::make_tuple(Lnast_ntype_attr_bits, "__bits", 0));
    //auto c1521 = ast_gld.add_child(c152, std::make_tuple(Lnast_ntype_const,     "0d1"   , 0));
    //(void) c1521;

    //auto c16   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_as,         "%s"  , 0));
    //auto c162  = ast_gld.add_child(c16,  std::make_tuple(Lnast_ntype_ref,        "___c", 0));
    //(void) c162;

    //auto c17   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_and,       "___d", 0));
    //auto c172  = ast_gld.add_child(c17,  std::make_tuple(Lnast_ntype_input,     "$a"  , 0));
    //auto c173  = ast_gld.add_child(c17,  std::make_tuple(Lnast_ntype_input,     "$b"  , 0));
    //(void) c172;
    //(void) c173;

    //auto c18   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_pure_assign,   "%s"  , 0));
    //auto c182  = ast_gld.add_child(c18,  std::make_tuple(Lnast_ntype_ref,           "___d", 0));
    //(void) c182;

    //auto c19    = ast_gld.add_child(c1,     std::make_tuple(Lnast_ntype_sub,        "",1));
    //auto c191   = ast_gld.add_child(c19,    std::make_tuple(Lnast_ntype_statement,  "",1));
    //auto c1911  = ast_gld.add_child(c191,   std::make_tuple(Lnast_ntype_xor,        "___f",1));
    //auto c19112 = ast_gld.add_child(c1911,  std::make_tuple(Lnast_ntype_input,      "$a",1));
    //auto c19113 = ast_gld.add_child(c1911,  std::make_tuple(Lnast_ntype_input,      "$b",1));
    //(void) c19112;
    //(void) c19113;


    //auto c1912   = ast_gld.add_child(c191,   std::make_tuple(Lnast_ntype_pure_assign,    "%o",   1));
    //auto c19122  = ast_gld.add_child(c1912,  std::make_tuple(Lnast_ntype_ref,            "___f", 1));
    //(void) c19122;


    //auto c1913   = ast_gld.add_child(c191,   std::make_tuple(Lnast_ntype_func_def,     "fun1",1));
    //auto c19132  = ast_gld.add_child(c1913,  std::make_tuple(Lnast_ntype_input ,       "$a",  1));
    //auto c19133  = ast_gld.add_child(c1913,  std::make_tuple(Lnast_ntype_input ,       "$b",  1));
    //auto c19134  = ast_gld.add_child(c1913,  std::make_tuple(Lnast_ntype_output,       "%o",  1));
    //(void) c19132;
    //(void) c19133;
    //(void) c19134;

    //auto c1a  = ast_gld.add_child(c1,  std::make_tuple(Lnast_ntype_label, "___h",   0));
    //auto c1a2 = ast_gld.add_child(c1a, std::make_tuple(Lnast_ntype_ref,   "a",      0));
    //auto c1a3 = ast_gld.add_child(c1a, std::make_tuple(Lnast_ntype_const, "0d3",    0));
    //(void) c1a2;
    //(void) c1a3;


    //auto c1b  = ast_gld.add_child(c1,  std::make_tuple(Lnast_ntype_label, "___i", 0));
    //auto c1b2 = ast_gld.add_child(c1b, std::make_tuple(Lnast_ntype_ref,   "b",    0));
    //auto c1b3 = ast_gld.add_child(c1b, std::make_tuple(Lnast_ntype_const, "0d4",  0));
    //(void) c1b2;
    //(void) c1b3;

    //auto c1c  = ast_gld.add_child(c1,  std::make_tuple(Lnast_ntype_func_call, "___g",   0));
    //auto c1c2 = ast_gld.add_child(c1c, std::make_tuple(Lnast_ntype_ref,       "fun1",   0));
    //auto c1c3 = ast_gld.add_child(c1c, std::make_tuple(Lnast_ntype_ref,       "___h",   0));
    //auto c1c4 = ast_gld.add_child(c1c, std::make_tuple(Lnast_ntype_ref,       "___i",   0));
    //(void) c1c2;
    //(void) c1c3;
    //(void) c1c4;


    //auto c1d   = ast_gld.add_child(c1,   std::make_tuple(Lnast_ntype_pure_assign, "result",  0));
    //auto c1d2  = ast_gld.add_child(c1d,  std::make_tuple(Lnast_ntype_ref,         "___g",    0));
    //(void) c1d2;

    setup_ast_sorted_golden();
    setup_ast_preorder_golden();
  }


  void setup_ast_sorted_golden(){
    ast_gld.each_breadth_first_fast([this] (const Tree_index &parent, const Tree_index &self, const tuple &node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();

      std::string type  = std::get<0>(node_data);
      std::string name  = std::get<1>(node_data);

      std::string ptype  = std::get<0>(ast_gld.get_data(parent));
      std::string pname  = std::get<1>(ast_gld.get_data(parent));

      fmt::print("nname:{}, ntype:{}\n",   name,  type);
      fmt::print("pname:{}, ptype:{}\n\n", pname, ptype);

      //auto tuple_data = std::make_tuple(name, type);
      ast_sorted_golden[self.level].emplace_back(std::make_tuple(name, type));
      EXPECT_EQ(ast_gld.get_parent(self), parent);
    });

    for(auto &a:ast_sorted_golden) {
        std::sort(a.begin(), a.end());
    }
  };

  void setup_ast_preorder_golden(){
    for (const auto &it:ast_gld.depth_preorder(ast_gld.get_root())) {
      while (static_cast<size_t>(it.level)>=ast_preorder_golden.size())
        ast_preorder_golden.emplace_back();

      auto node_data = ast_gld.get_data(it);
      std::string type  = std::get<0>(node_data);
      std::string name  = std::get<1>(node_data);

      auto tuple_data = std::make_tuple(name, type);

      ast_preorder_golden[it.level].emplace_back(tuple_data);
    }
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

  void setup_lnast_testee(){
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

    std::string name(node_data.token.get_text(memblock));
    std::string type  = ntype_dbg(node_data.type);

    std::string pname(lnast->get_data(parent).token.get_text(memblock));
    std::string ptype  = ntype_dbg(lnast->get_data(parent).type);

    fmt::print("nname:{}, ntype:{}\n", name, type);
    fmt::print("pname:{}, ptype:{}\n\n", pname, ptype);

    auto tuple_data = std::make_tuple(name, type);
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
    std::string name(node_data.token.get_text(memblock)); //str_view to string
    std::string type  = ntype_dbg(node_data.type);
    auto tuple_data = std::make_tuple(name, type);

    while (static_cast<size_t>(it.level)>=ast_preorder_testee.size())
      ast_preorder_testee.emplace_back();

    ast_preorder_testee[it.level].emplace_back(tuple_data);
  }

  check_preorder_against_ast(ast_preorder_testee);
}

