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
  mmap_lib::tree<tuple>  ast_gld;
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
1    0   99  SEQ0
2    1   99  99    99   :     ___a    __bits  0d1
3    1   99  99    99   as    $a      ___a
4    1   99  99    99   :     ___b    __bits  0d1
5    1   99  99    99   as    $b      ___b
6    1   99  99    99   :     ___c    __bits  0d1
7    1   99  99    99   as    %s      ___c
8    1   99  99    99   &     ___d    $a      $b
9    1   99  99    99   =     %s      ___d
10   1   99  99    99   ::{   ___e    null    $a    $b    %o
12   10  99  SEQ1
13   12  99  99    99   ^     ___f    $a      $b
14   12  99  99    99   =     %o      ___f
15   1   99  99    99   =     fun1    \___e
16   1   99  99    99   :     ___h    a       0d3
17   1   99  99    99   :     ___i    b       0d4
18   1   99  99    99   .()   ___g    fun1    ___h  ___i
19   1   99  99    99   =     result  ___g
20   1   99  99    99   .()   ___j    $a
21   1   99  99    99   =     x       ___j
23   1   99  99    99   if    ___k
24   23  99  SEQ2
25   24  99  99    99   >     ___k    $a      0d1
26   23  99  SEQ3
27   26  99  99    99   .()   ___l    $e
28   26  99  99    99   =     x       ___l
29   26  99  99    99   if    ___m
30   29  99  SEQ4
31   30  99  99    99   >     ___m    $a      0d2
32   29  99  SEQ5
33   32  99  99    99   .()   ___n    $b
34   32  99  99    99   =     x       ___n
35   29  99  99    99   elif  ___o
36   29  99  SEQ6
37   36  99  99    99   +     ___p    $a      0d1
38   36  99  99    99   >     ___o    ___p    0d3
39   29  99  SEQ7
40   39  99  99    99   .()   ___q    $c
41   39  99  99    99   =     x       ___q
42   29  99  SEQ8
43   42  99  99    99   .()   ___r    $d
44   42  99  99    99   =     x       ___r
45   26  99  99    99   .()   ___s    $e
46   26  99  99    99   =     y       ___s
47   23  99  SEQ9
48   47  99  99    99   .()   ___t    $f
49   47  99  99    99   =     x       ___t
50   1   99  99    99   +     ___u    x       $a
51   1   99  99    99   =     %o1     ___u
52   1   99  99    99   +     ___v    y       $a
53   1   99  99    99   =     %o2     ___v

 *
 */

  void setup_lnast_golden(){
    ast_gld.set_root(std::make_tuple((std::string)Lnast_ntype::create_top().debug_name(), "")); //knum = K1
    auto top_sts = ast_gld.add_child(ast_gld.get_root(), std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(), "")); //knum = K1

    auto K1      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""      ));
    auto K1_tar  = ast_gld.add_child(K1,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___a"  ));
    auto K1_op1  = ast_gld.add_child(K1,        std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),          "__bits"));
    auto K1_op2  = ast_gld.add_child(K1_op1,    std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d1"   ));
    (void) K1_tar;
    (void) K1_op2;

    auto K2      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),            ""    ));
    auto K2_tar  = ast_gld.add_child(K2,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$a"  ));
    auto K2_op1  = ast_gld.add_child(K2,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___a"));
    (void) K2_op1;
    (void) K2_tar;

    auto K3      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""  ));
    auto K3_tar  = ast_gld.add_child(K3,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___b"));
    auto K3_op1  = ast_gld.add_child(K3,        std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),          "__bits"));
    auto K3_op2  = ast_gld.add_child(K3_op1,    std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d1"   ));
    (void) K3_op2;
    (void) K3_tar;

    auto K4      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),            ""    ));
    auto K4_tar  = ast_gld.add_child(K4,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$b"  ));
    auto K4_op1  = ast_gld.add_child(K4,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___b"));
    (void) K4_tar;
    (void) K4_op1;

    auto K5      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""      ));
    auto K5_tar  = ast_gld.add_child(K5,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___c"  ));
    auto K5_op1  = ast_gld.add_child(K5,        std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),          "__bits"));
    auto K5_op2  = ast_gld.add_child(K5_op1,    std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d1"   ));
    (void) K5_op2;
    (void) K5_tar;

    auto K6      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),            ""    ));
    auto K6_tar  = ast_gld.add_child(K6,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%s"  ));
    auto K6_op1  = ast_gld.add_child(K6,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___c"));
    (void) K6_tar;
    (void) K6_op1;

    auto K7      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_and().debug_name(),         ""      ));
    auto K7_tar  = ast_gld.add_child(K7,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___d"  ));
    auto K7_op1  = ast_gld.add_child(K7,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"    ));
    auto K7_op2  = ast_gld.add_child(K7,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$b"    ));
    (void) K7_op1;
    (void) K7_op2;
    (void) K7_tar;

    auto K8      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto K8_tar  = ast_gld.add_child(K8,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%s"  ));
    auto K8_op1  = ast_gld.add_child(K8,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___d"));
    (void) K8_op1;
    (void) K8_tar;

    auto func_def     = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_func_def().debug_name(),      ""    ));
    auto K9_tar       = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "fun1"));
    auto K9_op1       = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$a"));
    auto K9_op2       = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$b"));
    auto K9_op3       = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%o1"));
    auto K9_op4       = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%o2"));
    auto func_def_sts = ast_gld.add_child(func_def, std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),    ""));
    (void) K9_tar;
    (void) K9_op1;
    (void) K9_op2;
    (void) K9_op3;
    (void) K9_op4;

    auto K100      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),      ""      ));
    auto K100_tar  = ast_gld.add_child(K100,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___f"  ));
    auto K100_op1  = ast_gld.add_child(K100,           std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),       "__bits"));
    auto K100_op2  = ast_gld.add_child(K100_op1,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),      "0d2"   ));
    (void) K100_tar;
    (void) K100_op2;


    auto K101      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),         ""    ));
    auto K101_tar  = ast_gld.add_child(K101,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "$a"  ));
    auto K101_op1  = ast_gld.add_child(K101,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___f"));
    (void) K101_op1;
    (void) K101_tar;

    auto K102      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),      ""      ));
    auto K102_tar  = ast_gld.add_child(K102,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___g"  ));
    auto K102_op1  = ast_gld.add_child(K102,           std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),       "__bits"));
    auto K102_op2  = ast_gld.add_child(K102_op1,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),      "0d2"   ));
    (void) K102_tar;
    (void) K102_op2;


    auto K103      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),         ""    ));
    auto K103_tar  = ast_gld.add_child(K103,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "$b"  ));
    auto K103_op1  = ast_gld.add_child(K103,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___g"));
    (void) K103_op1;
    (void) K103_tar;

    auto K104      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),      ""      ));
    auto K104_tar  = ast_gld.add_child(K104,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___h"  ));
    auto K104_op1  = ast_gld.add_child(K104,           std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),       "__bits"));
    auto K104_op2  = ast_gld.add_child(K104_op1,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),      "0d2"   ));
    (void) K104_tar;
    (void) K104_op2;


    auto K105      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),         ""    ));
    auto K105_tar  = ast_gld.add_child(K105,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "%o1"  ));
    auto K105_op1  = ast_gld.add_child(K105,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___h"));
    (void) K105_op1;
    (void) K105_tar;

    auto K106      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),      ""      ));
    auto K106_tar  = ast_gld.add_child(K106,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___i"  ));
    auto K106_op1  = ast_gld.add_child(K106,           std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),       "__bits"));
    auto K106_op2  = ast_gld.add_child(K106_op1,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),      "0d2"   ));
    (void) K106_tar;
    (void) K106_op2;


    auto K107      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),         ""    ));
    auto K107_tar  = ast_gld.add_child(K107,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "%o2"  ));
    auto K107_op1  = ast_gld.add_child(K107,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),        "___i"));
    (void) K107_op1;
    (void) K107_tar;





    auto K108      = ast_gld.add_child(func_def_sts,  std::make_tuple((std::string)Lnast_ntype::create_xor().debug_name(),           ""    ));
    auto K108_tar  = ast_gld.add_child(K108,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___j"));
    auto K108_op1  = ast_gld.add_child(K108,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$a"  ));
    auto K108_op2  = ast_gld.add_child(K108,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$b"  ));
    (void) K108_tar;
    (void) K108_op1;
    (void) K108_op2;

    auto K109      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto K109_tar  = ast_gld.add_child(K109,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%o1"  ));
    auto K109_op1  = ast_gld.add_child(K109,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___j"));
    (void) K109_tar;
    (void) K109_op1;


    auto K110      = ast_gld.add_child(func_def_sts,  std::make_tuple((std::string)Lnast_ntype::create_and().debug_name(),           ""    ));
    auto K110_tar  = ast_gld.add_child(K110,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___k"));
    auto K110_op1  = ast_gld.add_child(K110,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$a"  ));
    auto K110_op2  = ast_gld.add_child(K110,          std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$b"  ));
    (void) K110_tar;
    (void) K110_op1;
    (void) K110_op2;

    auto K111      = ast_gld.add_child(func_def_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto K111_tar  = ast_gld.add_child(K111,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%o2"  ));
    auto K111_op1  = ast_gld.add_child(K111,           std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___k"));
    (void) K111_tar;
    (void) K111_op1;


    auto K15      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""    ));
    auto K15_tar  = ast_gld.add_child(K15,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___m"));
    auto K15_op1  = ast_gld.add_child(K15,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "a"   ));
    auto K15_op2  = ast_gld.add_child(K15,      std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d2" ));
    (void) K15_tar;
    (void) K15_op1;
    (void) K15_op2;

    auto K16      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""    ));
    auto K16_tar  = ast_gld.add_child(K16,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___n"));
    auto K16_op1  = ast_gld.add_child(K16,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "b"   ));
    auto K16_op2  = ast_gld.add_child(K16,      std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d3" ));
    (void) K16_tar;
    (void) K16_op1;
    (void) K16_op2;

    auto K17      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_func_call().debug_name(),     ""    ));
    auto K17_tar  = ast_gld.add_child(K17,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___l"));
    auto K17_op1  = ast_gld.add_child(K17,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "fun1"));
    auto K17_op2  = ast_gld.add_child(K17,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___m"));
    auto K17_op3  = ast_gld.add_child(K17,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___n"));
    (void) K17_tar;
    (void) K17_op1;
    (void) K17_op2;
    (void) K17_op3;

    auto K18      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""      ));
    auto K18_tar  = ast_gld.add_child(K18,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "result"));
    auto K18_op1  = ast_gld.add_child(K18,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___l"));
    (void) K18_tar;
    (void) K18_op1;



    auto K112      = ast_gld.add_child(top_sts,     std::make_tuple((std::string)Lnast_ntype::create_label().debug_name(),         ""      ));
    auto K112_tar  = ast_gld.add_child(K112,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___o"  ));
    auto K112_op1  = ast_gld.add_child(K112,        std::make_tuple((std::string)Lnast_ntype::create_attr().debug_name(),          "__bits"));
    auto K112_op2  = ast_gld.add_child(K112_op1,    std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),         "0d3"   ));
    (void) K112_tar;
    (void) K112_op2;

    auto K113      = ast_gld.add_child(top_sts,     std::make_tuple((std::string)Lnast_ntype::create_as().debug_name(),            ""    ));
    auto K113_tar  = ast_gld.add_child(K113,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%s2"  ));
    auto K113_op1  = ast_gld.add_child(K113,        std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___o"));
    (void) K113_op1;
    (void) K113_tar;


    auto K114      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_dot().debug_name(),           ""    ));
    auto K114_tar  = ast_gld.add_child(K114,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___q"));
    auto K114_op1  = ast_gld.add_child(K114,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "result"  ));
    auto K114_op2  = ast_gld.add_child(K114,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "o1"  ));
    (void) K114_tar;
    (void) K114_op1;
    (void) K114_op2;


    auto K115      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_dot().debug_name(),           ""    ));
    auto K115_tar  = ast_gld.add_child(K115,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___r"));
    auto K115_op1  = ast_gld.add_child(K115,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "result"  ));
    auto K115_op2  = ast_gld.add_child(K115,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "o2"  ));
    (void) K115_tar;
    (void) K115_op1;
    (void) K115_op2;


    auto K116      = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_plus().debug_name(),           ""    ));
    auto K116_tar  = ast_gld.add_child(K116,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___p"));
    auto K116_op1  = ast_gld.add_child(K116,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___q"  ));
    auto K116_op2  = ast_gld.add_child(K116,     std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___r"  ));
    (void) K116_tar;
    (void) K116_op1;
    (void) K116_op2;


    auto K117      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto K117_tar  = ast_gld.add_child(K117,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "%s2"  ));
    auto K117_op1  = ast_gld.add_child(K117,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___p"));
    (void) K117_tar;
    (void) K117_op1;


    auto   K118      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto   K118_tar  = ast_gld.add_child(K118,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___s"  ));
    auto   K118_op1  = ast_gld.add_child(K118,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "$a"));
    (void) K118_tar;
    (void) K118_op1;


    auto   K119      = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(),   ""    ));
    auto   K119_tar  = ast_gld.add_child(K119,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "x"  ));
    auto   K119_op1  = ast_gld.add_child(K119,      std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),           "___s"));
    (void) K119_tar;
    (void) K119_op1;




    auto K22         = ast_gld.add_child(top_sts,  std::make_tuple((std::string)Lnast_ntype::create_if().debug_name(),          ""));
    auto K22_c0sts   = ast_gld.add_child(K22,      std::make_tuple((std::string)Lnast_ntype::create_cstatements().debug_name(), ""));
    auto K21      = ast_gld.add_child(K22_c0sts,   std::make_tuple((std::string)Lnast_ntype::create_gt().debug_name(),          ""    ));
    auto K21_tar  = ast_gld.add_child(K21,         std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___t"));
    auto K21_op1  = ast_gld.add_child(K21,         std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"  ));
    auto K21_op2  = ast_gld.add_child(K21,         std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),       "0d1"  ));
    (void) K21_tar;
    (void) K21_op1;
    (void) K21_op2;

    auto K22_c0      = ast_gld.add_child(K22,      std::make_tuple((std::string)Lnast_ntype::create_cond().debug_name(),         "___t"));
    auto K24sts      = ast_gld.add_child(K22,      std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),  ""));
    auto K44sts      = ast_gld.add_child(K22,      std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),  ""));
    (void) K22_c0sts ;
    (void) K22_c0    ;
    (void) K24sts;
    //(void) K22_c1sts ;
    //(void) K22_c1    ;
    (void) K44sts;

    auto K24         = ast_gld.add_child(K24sts, std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K24_tar     = ast_gld.add_child(K24,    std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___u"));
    auto K24_op1     = ast_gld.add_child(K24,    std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$e"));
    (void) K24_op1;
    (void) K24_tar;

    auto K25         = ast_gld.add_child(K24sts, std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K25_tar     = ast_gld.add_child(K25,    std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K25_op1     = ast_gld.add_child(K25,    std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___u"));
    (void) K25_op1;
    (void) K25_tar;


    auto K27         = ast_gld.add_child(K24sts,    std::make_tuple((std::string)Lnast_ntype::create_if().debug_name(),          ""));
    auto K27_c0sts   = ast_gld.add_child(K27,       std::make_tuple((std::string)Lnast_ntype::create_cstatements().debug_name(), ""));
    auto K26         = ast_gld.add_child(K27_c0sts, std::make_tuple((std::string)Lnast_ntype::create_gt().debug_name(),          ""));
    auto K26_tar     = ast_gld.add_child(K26,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___v"));
    auto K26_op1     = ast_gld.add_child(K26,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"));
    auto K26_op2     = ast_gld.add_child(K26,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),       "0d2"));
    (void) K26_tar;
    (void) K26_op1;
    (void) K26_op2;
    auto K27_c0      = ast_gld.add_child(K27,     std::make_tuple((std::string)Lnast_ntype::create_cond().debug_name(),        "___v"));
    auto K29sts      = ast_gld.add_child(K27,     std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),  ""));
    auto K27_c1sts   = ast_gld.add_child(K27,     std::make_tuple((std::string)Lnast_ntype::create_cstatements().debug_name(),  "")); //K32sts
    (void) K27_c0sts ;
    (void) K27_c0    ;
    (void) K29sts;
    (void) K27_c1sts;

    auto K32         = ast_gld.add_child(K27_c1sts, std::make_tuple((std::string)Lnast_ntype::create_plus().debug_name(),        ""));
    auto K32_tar     = ast_gld.add_child(K32,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___y"));
    auto K32_op1     = ast_gld.add_child(K32,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"));
    auto K32_op2     = ast_gld.add_child(K32,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),       "0d1"));
    (void) K32_tar;
    (void) K32_op1;
    (void) K32_op2;

    auto K33         = ast_gld.add_child(K27_c1sts, std::make_tuple((std::string)Lnast_ntype::create_gt().debug_name(),       ""));
    auto K33_tar     = ast_gld.add_child(K33,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),      "___x"));
    auto K33_op1     = ast_gld.add_child(K33,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),      "___y"));
    auto K33_op2     = ast_gld.add_child(K33,       std::make_tuple((std::string)Lnast_ntype::create_const().debug_name(),    "0d3"));
    (void) K33_tar;
    (void) K33_op1;
    (void) K33_op2;

    auto K27_c1      = ast_gld.add_child(K27,    std::make_tuple((std::string)Lnast_ntype::create_cond().debug_name(),        "___x"));
    auto K36sts      = ast_gld.add_child(K27,    std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),  ""));
    auto K39sts      = ast_gld.add_child(K27,    std::make_tuple((std::string)Lnast_ntype::create_statements().debug_name(),  ""));
    (void) K27_c1     ;
    (void) K36sts     ;
    //(void) K27_c2sts  ;
    //(void) K27_c2     ;
    (void) K39sts     ;

    auto K29         = ast_gld.add_child(K29sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K29_tar     = ast_gld.add_child(K29,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___w"));
    auto K29_op1     = ast_gld.add_child(K29,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$b"));
    (void) K29_tar;
    (void) K29_op1;

    auto K30         = ast_gld.add_child(K29sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K30_tar     = ast_gld.add_child(K30,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K30_op1     = ast_gld.add_child(K30,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___w"));
    (void) K30_tar;
    (void) K30_op1;



    auto K36         = ast_gld.add_child(K36sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K36_tar     = ast_gld.add_child(K36,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___z"));
    auto K36_op1     = ast_gld.add_child(K36,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$c"));
    (void) K36_tar;
    (void) K36_op1;

    auto K37         = ast_gld.add_child(K36sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K37_tar     = ast_gld.add_child(K37,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K37_op1     = ast_gld.add_child(K37,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___z"));
    (void) K37_tar;
    (void) K37_op1;

    auto K39         = ast_gld.add_child(K39sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K39_tar     = ast_gld.add_child(K39,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___aa"));
    auto K39_op1     = ast_gld.add_child(K39,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$d"));
    (void) K39_tar;
    (void) K39_op1;

    auto K40         = ast_gld.add_child(K39sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K40_tar     = ast_gld.add_child(K40,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K40_op1     = ast_gld.add_child(K40,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___aa"));
    (void) K40_tar;
    (void) K40_op1;

    auto K41         = ast_gld.add_child(K24sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K41_tar     = ast_gld.add_child(K41,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ab"));
    auto K41_op1     = ast_gld.add_child(K41,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$e"));
    (void) K41_tar;
    (void) K41_op1;

    auto K42         = ast_gld.add_child(K24sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K42_tar     = ast_gld.add_child(K42,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "y"));
    auto K42_op1     = ast_gld.add_child(K42,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ab"));
    (void) K42_tar;
    (void) K42_op1;

    auto K44         = ast_gld.add_child(K44sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K44_tar     = ast_gld.add_child(K44,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ac"));
    auto K44_op1     = ast_gld.add_child(K44,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$f"));
    (void) K44_tar;
    (void) K44_op1;

    auto K45         = ast_gld.add_child(K44sts,    std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K45_tar     = ast_gld.add_child(K45,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K45_op1     = ast_gld.add_child(K45,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ac"));
    (void) K45_op1;
    (void) K45_tar;

    auto K47         = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_plus().debug_name(),        ""));
    auto K47_tar     = ast_gld.add_child(K47,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ad"));
    auto K47_op1     = ast_gld.add_child(K47,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "x"));
    auto K47_op2     = ast_gld.add_child(K47,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"));
    (void) K47_op1;
    (void) K47_op2;
    (void) K47_tar;

    auto K48         = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K48_tar     = ast_gld.add_child(K48,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "%o1"));
    auto K48_op1     = ast_gld.add_child(K48,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ad"));
    (void) K48_tar;
    (void) K48_op1;

    auto K49         = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_plus().debug_name(),        ""));
    auto K49_tar     = ast_gld.add_child(K49,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ae"));
    auto K49_op1     = ast_gld.add_child(K49,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "y"));
    auto K49_op2     = ast_gld.add_child(K49,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "$a"));
    (void) K49_tar;
    (void) K49_op1;
    (void) K49_op2;

    auto K50         = ast_gld.add_child(top_sts,   std::make_tuple((std::string)Lnast_ntype::create_pure_assign().debug_name(), ""));
    auto K50_tar     = ast_gld.add_child(K50,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "%o2"));
    auto K50_op1     = ast_gld.add_child(K50,       std::make_tuple((std::string)Lnast_ntype::create_ref().debug_name(),         "___ae"));
    (void) K50_tar;
    (void) K50_op1;

    setup_ast_sorted_golden();
    setup_ast_preorder_golden();
  }

  void setup_ast_sorted_golden(){
    fmt::print("setup_ast_sorted_golden\n");
    ast_gld.each_top_down_fast([this] (const mmap_lib::Tree_index &self, const tuple &node_data) {
      const mmap_lib::Tree_index &parent = ast_gld.get_parent(self);

      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();

      std::string type(std::get<0>(node_data));
      std::string name(std::get<1>(node_data));

      std::string ptype(std::get<0>(ast_gld.get_data(parent)));
      std::string pname(std::get<1>(ast_gld.get_data(parent)));

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
    fmt::print("setup_ast_preorder_golden\n");
    for (const auto &it:ast_gld.depth_preorder(ast_gld.get_root())) {
      while (static_cast<size_t>(it.level)>=ast_preorder_golden.size())
        ast_preorder_golden.emplace_back();

      auto node_data = ast_gld.get_data(it);
      std::string type(std::get<0>(node_data));
      std::string name(std::get<1>(node_data));

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

  void check_goldens_sorted_against_preorder(){
    for(auto &a:ast_preorder_golden) {
      std::sort(a.begin(), a.end());
    }
    EXPECT_EQ(ast_preorder_golden, ast_sorted_golden);
  }


  std::string get_current_working_dir(){
    std::string cwd("\0", FILENAME_MAX + 1);
    return getcwd(&cwd[0],cwd.capacity());
  }

  std::string_view setup_memblock(){
    std::string tmp_str = get_current_working_dir();
    std::string file_path = tmp_str + "/inou/cfg/tests/lnast_utest.cfg";
    int fd = open(file_path.c_str(), O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "error, could not open %s\n", file_path.c_str());
        exit(-3);
    }

    struct stat sb;
    fstat(fd, &sb);
    printf("Size: %lu\n", (uint64_t)sb.st_size);

    char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    //fprintf(stderr, "Content of memblock: \n%s\n", memblock);
    if(memblock == MAP_FAILED) {
        fprintf(stderr, "error, mmap failed\n");
        exit(-3);
    }
    return memblock;
  }

  void setup_lnast_testee(){
    std::string_view memblock = setup_memblock();
    Elab_scanner::Token_list tlist;
    lnast_parser.parse("lnast", memblock, tlist);
  }
};//end class

TEST_F(Lnast_test, Traverse_breadth_first_check_on_ast) {
  fmt::print("Traverse_breadth_first_check_on_ast\n");
  auto lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership
  std::vector<std::vector<tuple>> ast_sorted_testee;
  std::string_view memblock = setup_memblock();

  lnast->each_top_down_fast([&ast_sorted_testee, &memblock, &lnast] (const mmap_lib::Tree_index &self,
                                                                           const Lnast_node &node_data) {
    const mmap_lib::Tree_index &parent = lnast->get_parent(self);

    while (static_cast<size_t>(self.level)>=ast_sorted_testee.size())
        ast_sorted_testee.emplace_back();

    std::string name(node_data.token.get_text(memblock));
    std::string type(node_data.type.debug_name());

    std::string pname(lnast->get_data(parent).token.get_text(memblock));
    std::string ptype(lnast->get_data(parent).type.debug_name());

    fmt::print("nname:{}, ntype:{}\n", name, type);
    fmt::print("pname:{}, ptype:{}\n\n", pname, ptype);

    auto tuple_data = std::make_tuple(name, type);
    ast_sorted_testee[self.level].emplace_back(tuple_data);
    EXPECT_EQ(lnast-> get_parent(self), parent);
  });

  check_sorted_against_ast(ast_sorted_testee);
}


TEST_F(Lnast_test,Traverse_preorder_check_on_lnast){
  fmt::print("Traverse_preorder_check_on_ast\n");
  auto lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership
  std::vector<std::vector<tuple>> ast_preorder_testee;
  std::string_view memblock = setup_memblock();

  for (const auto &it: lnast->depth_preorder(lnast->get_root()) ) {

    const auto& node_data = lnast->get_data(it);
    std::string name(node_data.token.get_text(memblock)); //str_view to string for sorting
    std::string type(node_data.type.debug_name());
    auto tuple_data = std::make_tuple(name, type);

    while (static_cast<size_t>(it.level)>=ast_preorder_testee.size())
      ast_preorder_testee.emplace_back();

    ast_preorder_testee[it.level].emplace_back(tuple_data);
  }

  check_preorder_against_ast(ast_preorder_testee);
}

//sh:fixme: new bug??? not important for now
//TEST_F(Lnast_test, Preorder_golden_vs_sorted_golden){
//  check_goldens_sorted_against_preorder();
//}

