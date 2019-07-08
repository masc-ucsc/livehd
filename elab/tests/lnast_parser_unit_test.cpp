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

    //setup_lnast_ssa_golden();
    //setup_lnast_ssa_testee();
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
    ast_gld.set_root(std::make_tuple(ntype_dbg(Lnast_ntype_top), "")); //knum = K1
    auto top_sts = ast_gld.add_child(Tree_index(0,0), std::make_tuple(ntype_dbg(Lnast_ntype_statements), "")); //knum = K1

    auto K1      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_label),         "___a"  ));
    auto K1_op1  = ast_gld.add_child(K1,        std::make_tuple(ntype_dbg(Lnast_ntype_attr_bits),     "__bits"));
    auto K1_op2  = ast_gld.add_child(K1_op1,    std::make_tuple(ntype_dbg(Lnast_ntype_const),         "0d1"   ));
    (void) K1_op2;

    auto K2      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_as),            "$a"));
    auto K2_op1  = ast_gld.add_child(K2,        std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___a"));
    (void) K2_op1;

    auto K3      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_label),         "___b"  ));
    auto K3_op1  = ast_gld.add_child(K3,        std::make_tuple(ntype_dbg(Lnast_ntype_attr_bits),     "__bits"));
    auto K3_op2  = ast_gld.add_child(K3_op1,    std::make_tuple(ntype_dbg(Lnast_ntype_const),         "0d1"   ));
    (void) K3_op2;

    auto K4      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_as),            "$b"));
    auto K4_op1  = ast_gld.add_child(K4,        std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___b"));
    (void) K4_op1;

    auto K5      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_label),         "___c"  ));
    auto K5_op1  = ast_gld.add_child(K5,        std::make_tuple(ntype_dbg(Lnast_ntype_attr_bits),     "__bits"));
    auto K5_op2  = ast_gld.add_child(K5_op1,    std::make_tuple(ntype_dbg(Lnast_ntype_const),         "0d1"   ));
    (void) K5_op2;

    auto K6      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_as),            "%s"  ));
    auto K6_op1  = ast_gld.add_child(K6,        std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___c"));
    (void) K6_op1;

    auto K7      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_and),           "___d"));
    auto K7_op1  = ast_gld.add_child(K7,        std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$a"  ));
    auto K7_op2  = ast_gld.add_child(K7,        std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$b"  ));
    (void) K7_op1;
    (void) K7_op2;

    auto K8      = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign),   "%s"  ));
    auto K8_op1  = ast_gld.add_child(K8,        std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___d"));
    (void) K8_op1;

    auto func_def     = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_func_def),      "fun1")); //knum = K9
    auto func_def_sts = ast_gld.add_child(func_def, std::make_tuple(ntype_dbg(Lnast_ntype_statements),    ""));     //knum = K9
    auto K9_op1       = ast_gld.add_child(func_def, std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$a"));
    auto K9_op2       = ast_gld.add_child(func_def, std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$b"));
    auto K9_op3       = ast_gld.add_child(func_def, std::make_tuple(ntype_dbg(Lnast_ntype_output),        "%o"));
    (void) K9_op1;
    (void) K9_op2;
    (void) K9_op3;

    auto K11      = ast_gld.add_child(func_def_sts, std::make_tuple(ntype_dbg(Lnast_ntype_xor),           "___f"));
    auto K11_op1  = ast_gld.add_child(K11,          std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$a"  ));
    auto K11_op2  = ast_gld.add_child(K11,          std::make_tuple(ntype_dbg(Lnast_ntype_input),         "$b"  ));
    (void) K11_op1;
    (void) K11_op2;

    auto K12      = ast_gld.add_child(func_def_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign),   "%o"  ));
    auto K12_op1  = ast_gld.add_child(K12,           std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___f"));
    (void) K12_op1;

    auto K15      = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_label),         "___h"));
    auto K15_op1  = ast_gld.add_child(K15,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "a"   ));
    auto K15_op2  = ast_gld.add_child(K15,      std::make_tuple(ntype_dbg(Lnast_ntype_const),         "0d3" ));
    (void) K15_op1;
    (void) K15_op2;

    auto K16      = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_label),         "___i"));
    auto K16_op1  = ast_gld.add_child(K16,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "b"   ));
    auto K16_op2  = ast_gld.add_child(K16,      std::make_tuple(ntype_dbg(Lnast_ntype_const),         "0d4" ));
    (void) K16_op1;
    (void) K16_op2;

    auto K17      = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_func_call),     "___g"));
    auto K17_op1  = ast_gld.add_child(K17,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "fun1"));
    auto K17_op2  = ast_gld.add_child(K17,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___h"));
    auto K17_op3  = ast_gld.add_child(K17,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___i"));
    (void) K17_op1;
    (void) K17_op2;
    (void) K17_op3;

    auto K18      = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign),   "result"));
    auto K18_op1  = ast_gld.add_child(K18,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),           "___g"));
    (void) K18_op1;

    auto K19      = ast_gld.add_child(top_sts, std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___j"));
    auto K19_op1  = ast_gld.add_child(K19,     std::make_tuple(ntype_dbg(Lnast_ntype_input),       "$a"));
    (void) K19_op1;

    auto K20      = ast_gld.add_child(top_sts, std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K20_op1  = ast_gld.add_child(K20,     std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___j"));
    (void) K20_op1;

    auto K21      = ast_gld.add_child(top_sts, std::make_tuple(ntype_dbg(Lnast_ntype_gt),          "___k"));
    auto K21_op1  = ast_gld.add_child(K21,     std::make_tuple(ntype_dbg(Lnast_ntype_input),       "$a"));
    auto K21_op2  = ast_gld.add_child(K21,     std::make_tuple(ntype_dbg(Lnast_ntype_const),       "0d1"));
    (void) K21_op1;
    (void) K21_op2;


    auto K22         = ast_gld.add_child(top_sts,  std::make_tuple(ntype_dbg(Lnast_ntype_if),          ""));
    auto K22_c0sts   = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_cstatements), ""));
    auto K22_c0      = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___k"));
    auto K24sts      = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_statements),  ""));
    auto K22_c1sts   = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_cstatements), ""));
    auto K22_c1      = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "true"));
    auto K44sts      = ast_gld.add_child(K22,      std::make_tuple(ntype_dbg(Lnast_ntype_statements),  ""));
    (void) K22_c0sts ;
    (void) K22_c0    ;
    (void) K24sts;
    (void) K22_c1sts ;
    (void) K22_c1    ;
    (void) K44sts;

    auto K24         = ast_gld.add_child(K24sts, std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___l"));
    auto K24_op1     = ast_gld.add_child(K24,    std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$e"));
    (void) K24_op1;

    auto K25         = ast_gld.add_child(K24sts, std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K25_op1     = ast_gld.add_child(K25,    std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___l"));
    (void) K25_op1;

    auto K26         = ast_gld.add_child(K24sts, std::make_tuple(ntype_dbg(Lnast_ntype_gt),          "___m"));
    auto K26_op1     = ast_gld.add_child(K26,    std::make_tuple(ntype_dbg(Lnast_ntype_input),       "$a"));
    auto K26_op2     = ast_gld.add_child(K26,    std::make_tuple(ntype_dbg(Lnast_ntype_const),       "0d2"));
    (void) K26_op1;
    (void) K26_op2;

    auto K27         = ast_gld.add_child(K24sts,  std::make_tuple(ntype_dbg(Lnast_ntype_if),          ""));
    auto K27_c0sts   = ast_gld.add_child(K27,     std::make_tuple(ntype_dbg(Lnast_ntype_cstatements), ""));
    auto K27_c0      = ast_gld.add_child(K27,     std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___m"));
    auto K29sts      = ast_gld.add_child(K27,     std::make_tuple(ntype_dbg(Lnast_ntype_statements),  ""));
    auto K27_c1sts   = ast_gld.add_child(K27,     std::make_tuple(ntype_dbg(Lnast_ntype_cstatements),  "")); //K32sts
    (void) K27_c0sts ;
    (void) K27_c0    ;
    (void) K29sts;
    (void) K27_c1sts;

    auto K33         = ast_gld.add_child(K27_c1sts, std::make_tuple(ntype_dbg(Lnast_ntype_gt),       "___o"));
    auto K33_op1     = ast_gld.add_child(K33,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),      "___p"));
    auto K33_op2     = ast_gld.add_child(K33,       std::make_tuple(ntype_dbg(Lnast_ntype_const),    "0d3"));
    (void) K33_op1;
    (void) K33_op2;

    auto K27_c1      = ast_gld.add_child(K27,    std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___o"));
    auto K36sts      = ast_gld.add_child(K27,    std::make_tuple(ntype_dbg(Lnast_ntype_statements),  ""));
    auto K27_c2sts   = ast_gld.add_child(K27,    std::make_tuple(ntype_dbg(Lnast_ntype_cstatements), ""));
    auto K27_c2      = ast_gld.add_child(K27,    std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "true"));
    auto K39sts      = ast_gld.add_child(K27,    std::make_tuple(ntype_dbg(Lnast_ntype_statements),  ""));
    (void) K27_c1     ;
    (void) K36sts     ;
    (void) K27_c2sts  ;
    (void) K27_c2     ;
    (void) K39sts     ;

    auto K29         = ast_gld.add_child(K29sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___n"));
    auto K29_op1     = ast_gld.add_child(K29,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$b"));
    (void) K29_op1;

    auto K30         = ast_gld.add_child(K29sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K30_op1     = ast_gld.add_child(K30,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___n"));
    (void) K30_op1;

    auto K32         = ast_gld.add_child(K27_c1sts, std::make_tuple(ntype_dbg(Lnast_ntype_plus),        "___p"));
    auto K32_op1     = ast_gld.add_child(K32,       std::make_tuple(ntype_dbg(Lnast_ntype_input),       "$a"));
    auto K32_op2     = ast_gld.add_child(K32,       std::make_tuple(ntype_dbg(Lnast_ntype_const),       "0d1"));
    (void) K32_op1;
    (void) K32_op2;

    auto K36         = ast_gld.add_child(K36sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___q"));
    auto K36_op1     = ast_gld.add_child(K36,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$c"));
    (void) K36_op1;

    auto K37         = ast_gld.add_child(K36sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K37_op1     = ast_gld.add_child(K37,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___q"));
    (void) K37_op1;

    auto K39         = ast_gld.add_child(K39sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___r"));
    auto K39_op1     = ast_gld.add_child(K39,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$d"));
    (void) K39_op1;

    auto K40         = ast_gld.add_child(K39sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K40_op1     = ast_gld.add_child(K40,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___r"));
    (void) K40_op1;

    auto K41         = ast_gld.add_child(K24sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___s"));
    auto K41_op1     = ast_gld.add_child(K41,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$e"));
    (void) K41_op1;

    auto K42         = ast_gld.add_child(K24sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "y"));
    auto K42_op1     = ast_gld.add_child(K42,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___s"));
    (void) K42_op1;

    auto K44         = ast_gld.add_child(K44sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "___t"));
    auto K44_op1     = ast_gld.add_child(K44,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "$f"));
    (void) K44_op1;

    auto K45         = ast_gld.add_child(K24sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "x"));
    auto K45_op1     = ast_gld.add_child(K45,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___t"));
    (void) K45_op1;

    auto K47         = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_plus),        "___u"));
    auto K47_op1     = ast_gld.add_child(K47,       std::make_tuple(ntype_dbg(Lnast_ntype_input),       "x"));
    auto K47_op2     = ast_gld.add_child(K47,       std::make_tuple(ntype_dbg(Lnast_ntype_const),       "$a"));
    (void) K47_op1;
    (void) K47_op2;

    auto K48         = ast_gld.add_child(K24sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "%o1"));
    auto K48_op1     = ast_gld.add_child(K48,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___u"));
    (void) K48_op1;

    auto K49         = ast_gld.add_child(top_sts,   std::make_tuple(ntype_dbg(Lnast_ntype_plus),        "___v"));
    auto K49_op1     = ast_gld.add_child(K49,       std::make_tuple(ntype_dbg(Lnast_ntype_input),       "y"));
    auto K49_op2     = ast_gld.add_child(K49,       std::make_tuple(ntype_dbg(Lnast_ntype_const),       "$a"));
    (void) K49_op1;
    (void) K49_op2;

    auto K50         = ast_gld.add_child(K24sts,    std::make_tuple(ntype_dbg(Lnast_ntype_pure_assign), "%o2"));
    auto K50_op1     = ast_gld.add_child(K50,       std::make_tuple(ntype_dbg(Lnast_ntype_ref),         "___v"));
    (void) K50_op1;

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
};//end class

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

