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

using tuple = std::tuple<std::string, std::string , uint8_t>;// <name, type, scope>

class Lnast_test : public ::testing::Test, public Lnast_parser {
  std::vector<std::vector<tuple>> ast_sorted_golden;
  std::vector<std::vector<tuple>> ast_preorder_golden;

public:
  Tree<Lnast_node_str>                  ast_gld;
  Language_neutral_ast<Lnast_node_str>  ast_hcoded;// hard-coded
  Lnast_parser                          lnast_parser;

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
  //graphvis_lnast_data("ast_hcoded", &ast_hcoded);
  //graphvis_lnast_data("ast_hcoded_phi", &ast_hcoded);
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
    ast_gld.set_root(Lnast_node_str(Lnast_ntype_top, "", 0));
    auto c1    = ast_gld.add_child(Tree_index(0,0), Lnast_node_str(Lnast_ntype_statement, "", 0));

    auto c11   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_label,     "___a"  , 0));
    auto c112  = ast_gld.add_child(c11,  Lnast_node_str(Lnast_ntype_attr_bits, "__bits", 0));
    auto c1121 = ast_gld.add_child(c112, Lnast_node_str(Lnast_ntype_const,     "0d1"   , 0));
    (void) c1121;

    auto c12   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_as,     "$a"  , 0));
    auto c122  = ast_gld.add_child(c12,  Lnast_node_str(Lnast_ntype_ref,    "___a", 0));
    (void) c122;

    auto c13   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_label,     "___b"  , 0));
    auto c132  = ast_gld.add_child(c13,  Lnast_node_str(Lnast_ntype_attr_bits, "__bits", 0));
    auto c1321 = ast_gld.add_child(c132, Lnast_node_str(Lnast_ntype_const,     "0d1"   , 0));
    (void) c1321;

    auto c14   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_as,     "$b"  , 0));
    auto c142  = ast_gld.add_child(c14,  Lnast_node_str(Lnast_ntype_ref,    "___b", 0));
    (void) c142;

    auto c15   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_label,     "___c"  , 0));
    auto c152  = ast_gld.add_child(c15,  Lnast_node_str(Lnast_ntype_attr_bits, "__bits", 0));
    auto c1521 = ast_gld.add_child(c152, Lnast_node_str(Lnast_ntype_const,     "0d1"   , 0));
    (void) c1521;

    auto c16   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_as,         "%s"  , 0));
    auto c162  = ast_gld.add_child(c16,  Lnast_node_str(Lnast_ntype_ref,        "___c", 0));
    (void) c162;

    auto c17   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_and,       "___d", 0));
    auto c172  = ast_gld.add_child(c17,  Lnast_node_str(Lnast_ntype_input,     "$a"  , 0));
    auto c173  = ast_gld.add_child(c17,  Lnast_node_str(Lnast_ntype_input,     "$b"  , 0));
    (void) c172;
    (void) c173;

    auto c18   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_pure_assign,   "%s"  , 0));
    auto c182  = ast_gld.add_child(c18,  Lnast_node_str(Lnast_ntype_ref,           "___d", 0));
    (void) c182;

    auto c19    = ast_gld.add_child(c1,     Lnast_node_str(Lnast_ntype_sub,        "",1));
    auto c191   = ast_gld.add_child(c19,    Lnast_node_str(Lnast_ntype_statement,  "",1));
    auto c1911  = ast_gld.add_child(c191,   Lnast_node_str(Lnast_ntype_xor,        "___f",1));
    auto c19112 = ast_gld.add_child(c1911,  Lnast_node_str(Lnast_ntype_input,      "$a",1));
    auto c19113 = ast_gld.add_child(c1911,  Lnast_node_str(Lnast_ntype_input,      "$b",1));
    (void) c19112;
    (void) c19113;


    auto c1912   = ast_gld.add_child(c191,   Lnast_node_str(Lnast_ntype_pure_assign,    "%o",   1));
    auto c19122  = ast_gld.add_child(c1912,  Lnast_node_str(Lnast_ntype_ref,            "___f", 1));
    (void) c19122;


    auto c1913   = ast_gld.add_child(c191,   Lnast_node_str(Lnast_ntype_func_def,     "fun1",1));
    auto c19132  = ast_gld.add_child(c1913,  Lnast_node_str(Lnast_ntype_input ,       "$a",  1));
    auto c19133  = ast_gld.add_child(c1913,  Lnast_node_str(Lnast_ntype_input ,       "$b",  1));
    auto c19134  = ast_gld.add_child(c1913,  Lnast_node_str(Lnast_ntype_output,       "%o",  1));
    (void) c19132;
    (void) c19133;
    (void) c19134;

    auto c1a  = ast_gld.add_child(c1,  Lnast_node_str(Lnast_ntype_label, "___h",   0));
    auto c1a2 = ast_gld.add_child(c1a, Lnast_node_str(Lnast_ntype_ref,   "a",      0));
    auto c1a3 = ast_gld.add_child(c1a, Lnast_node_str(Lnast_ntype_const, "0d3",    0));
    (void) c1a2;
    (void) c1a3;


    auto c1b  = ast_gld.add_child(c1,  Lnast_node_str(Lnast_ntype_label, "___i", 0));
    auto c1b2 = ast_gld.add_child(c1b, Lnast_node_str(Lnast_ntype_ref,   "b",    0));
    auto c1b3 = ast_gld.add_child(c1b, Lnast_node_str(Lnast_ntype_const, "0d4",  0));
    (void) c1b2;
    (void) c1b3;

    auto c1c  = ast_gld.add_child(c1,  Lnast_node_str(Lnast_ntype_func_call, "___g",   0));
    auto c1c2 = ast_gld.add_child(c1c, Lnast_node_str(Lnast_ntype_ref,       "fun1",   0));
    auto c1c3 = ast_gld.add_child(c1c, Lnast_node_str(Lnast_ntype_ref,       "___h",   0));
    auto c1c4 = ast_gld.add_child(c1c, Lnast_node_str(Lnast_ntype_ref,       "___i",   0));
    (void) c1c2;
    (void) c1c3;
    (void) c1c4;


    auto c1d   = ast_gld.add_child(c1,   Lnast_node_str(Lnast_ntype_pure_assign, "result",  0));
    auto c1d2  = ast_gld.add_child(c1d,  Lnast_node_str(Lnast_ntype_ref,         "___g",    0));
    (void) c1d2;

    setup_ast_sorted_golden();
    setup_ast_preorder_golden();
  }


  void setup_ast_sorted_golden(){
    ast_gld.each_breadth_first_fast([this] (const Tree_index &parent, const Tree_index &self, const Lnast_node_str &node_data) {
      while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
          ast_sorted_golden.emplace_back();

      std::string name(node_data.token);
      std::string type  = ntype_dbg(node_data.type);
      auto        scope = node_data.scope;

      std::string pname(ast_gld.get_data(parent).token);
      std::string ptype  = ntype_dbg(ast_gld.get_data(parent).type);
      auto        pscope = ast_gld.get_data(parent).scope;

      fmt::print("nname:{}, ntype:{}, nscope:{}\n", name, type, scope);
      fmt::print("pname:{}, ptype:{}, pscope:{}\n\n", pname, ptype, pscope);

      tuple tuple_data = std::make_tuple(name, type, scope);
      ast_sorted_golden[self.level].emplace_back(tuple_data);
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
      std::string name(node_data.token);
      std::string type  = ntype_dbg(node_data.type);
      auto        scope = node_data.scope;
      tuple tuple_data = std::make_tuple(name, type, scope);

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

  //SH:FIXME: temporarily use Lnast_node_str
  void graphvis_lnast_data(std::string_view files, const Language_neutral_ast<Lnast_node_str> *lnast) {
    std::string data = "digraph {\n";

    for(const auto& itr : lnast->depth_preorder(lnast->get_root())){
      const auto &node_data = lnast->get_data(itr);
      const auto &type  = lnast_parser.ntype_dbg(node_data.type);
      const auto &scope = node_data.scope;
      //std::string name(node_data.token.get_text(memblock));
      auto name  = node_data.token;
      if(node_data.type == Lnast_ntype_top)
        name = "top";


      auto id = std::to_string(itr.level)+std::to_string(itr.pos);
      data += fmt::format(" {} [label=\"{}:{}:{}\"];\n", id, type, name, scope);
      if(node_data.type == Lnast_ntype_top)
        continue;

      //get parent data for link
      const auto &p = lnast->get_parent(itr);
      const auto &ptype = lnast->get_data(p).type;
      //std::string pname(lnast->get_data(p).token.get_text(memblock));
      auto pname  = lnast->get_data(p).token;

      if(ptype == Lnast_ntype_top)
        pname = "top";

      auto parent_id = std::to_string(p.level)+std::to_string(p.pos);
      data += fmt::format(" {}->{};\n", parent_id, id);
    }

    data += "}\n";

    std::string file = absl::StrCat( "./", files, ".dot");
    int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
      //Pass::error("inou.graphviz_lnast unable to create {}", file);
      return;
    }
    write(fd, data.c_str(), data.size());
    close(fd);
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
    auto        scope = node_data.scope;

    std::string pname(lnast->get_data(parent).token.get_text(memblock));
    std::string ptype  = ntype_dbg(lnast->get_data(parent).type);
    auto        pscope = lnast->get_data(parent).scope;

    fmt::print("nname:{}, ntype:{}, nscope:{}\n", name, type, scope);
    fmt::print("pname:{}, ptype:{}, pscope:{}\n\n", pname, ptype, pscope);

    tuple tuple_data = std::make_tuple(name, type, scope);
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
    auto        scope = node_data.scope;
    tuple tuple_data = std::make_tuple(name, type, scope);

    while (static_cast<size_t>(it.level)>=ast_preorder_testee.size())
      ast_preorder_testee.emplace_back();

    ast_preorder_testee[it.level].emplace_back(tuple_data);
  }

  check_preorder_against_ast(ast_preorder_testee);
}

//ast_gld.set_root(std::make_tuple("", ntype_dbg(Lnast_ntype_top), 0));
//auto c1    = ast_gld.add_child(Tree_index(0,0), std::make_tuple("K1", ntype_dbg(Lnast_ntype_statement), 0));
//
//auto c11   = ast_gld.add_child(c1,   std::make_tuple("K1",     ntype_dbg(Lnast_ntype_label), 0));
//auto c111  = ast_gld.add_child(c11,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
//auto c112  = ast_gld.add_child(c11,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
//auto c1121 = ast_gld.add_child(c112, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
//(void) c111; // for turn off un-used warning
//(void) c1121;
//
//auto c12   = ast_gld.add_child(c1,   std::make_tuple("K2",     ntype_dbg(Lnast_ntype_as), 0));
//auto c121  = ast_gld.add_child(c12,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
//auto c122  = ast_gld.add_child(c12,  std::make_tuple("___a",   ntype_dbg(Lnast_ntype_ref), 0));
//(void) c121;
//(void) c122;
//
//auto c13   = ast_gld.add_child(c1,   std::make_tuple("K3",     ntype_dbg(Lnast_ntype_label), 0));
//auto c131  = ast_gld.add_child(c13,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
//auto c132  = ast_gld.add_child(c13,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
//auto c1321 = ast_gld.add_child(c132, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
//(void) c131; // for turn off un-used warning
//(void) c1321;
//
//auto c14   = ast_gld.add_child(c1,   std::make_tuple("K4",     ntype_dbg(Lnast_ntype_as), 0));
//auto c141  = ast_gld.add_child(c14,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
//auto c142  = ast_gld.add_child(c14,  std::make_tuple("___b",   ntype_dbg(Lnast_ntype_ref), 0));
//(void) c141;
//(void) c142;
//
//auto c15   = ast_gld.add_child(c1,   std::make_tuple("K5",     ntype_dbg(Lnast_ntype_label), 0));
//auto c151  = ast_gld.add_child(c15,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
//auto c152  = ast_gld.add_child(c15,  std::make_tuple("__bits", ntype_dbg(Lnast_ntype_attr_bits), 0));
//auto c1521 = ast_gld.add_child(c152, std::make_tuple("0d1",    ntype_dbg(Lnast_ntype_const), 0));
//(void) c151; // for turn off un-used warning
//(void) c1521;
//
//auto c16   = ast_gld.add_child(c1,   std::make_tuple("K6",     ntype_dbg(Lnast_ntype_as), 0));
//auto c161  = ast_gld.add_child(c16,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
//auto c162  = ast_gld.add_child(c16,  std::make_tuple("___c",   ntype_dbg(Lnast_ntype_ref), 0));
//(void) c161;
//(void) c162;
//
//auto c17   = ast_gld.add_child(c1,   std::make_tuple("K7",     ntype_dbg(Lnast_ntype_and), 0));
//auto c171  = ast_gld.add_child(c17,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
//auto c172  = ast_gld.add_child(c17,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 0));
//auto c173  = ast_gld.add_child(c17,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 0));
//(void) c171;
//(void) c172;
//(void) c173;
//
//auto c18   = ast_gld.add_child(c1,   std::make_tuple("K8",     ntype_dbg(Lnast_ntype_pure_assign), 0));
//auto c181  = ast_gld.add_child(c18,  std::make_tuple("%s",     ntype_dbg(Lnast_ntype_output), 0));
//auto c182  = ast_gld.add_child(c18,  std::make_tuple("___d",   ntype_dbg(Lnast_ntype_ref), 0));
//(void) c181;
//(void) c182;
//
//auto c19    = ast_gld.add_child(c1,     std::make_tuple("K11",  ntype_dbg(Lnast_ntype_sub),1));
//auto c191   = ast_gld.add_child(c19,    std::make_tuple("K11",  ntype_dbg(Lnast_ntype_statement),1));
//auto c1911  = ast_gld.add_child(c191,   std::make_tuple("K11",  ntype_dbg(Lnast_ntype_plus),1));
//auto c19111 = ast_gld.add_child(c1911,  std::make_tuple("___f", ntype_dbg(Lnast_ntype_ref),1));
//auto c19112 = ast_gld.add_child(c1911,  std::make_tuple("$a",   ntype_dbg(Lnast_ntype_input),1));
//auto c19113 = ast_gld.add_child(c1911,  std::make_tuple("$b",   ntype_dbg(Lnast_ntype_input),1));
//(void) c19111;
//(void) c19112;
//(void) c19113;
//
//
//auto c1912   = ast_gld.add_child(c191,   std::make_tuple("K12",    ntype_dbg(Lnast_ntype_pure_assign), 1));
//auto c19121  = ast_gld.add_child(c1912,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
//auto c19122  = ast_gld.add_child(c1912,  std::make_tuple("___f",   ntype_dbg(Lnast_ntype_ref), 1));
//(void) c19121;
//(void) c19122;
//
//
//auto c1913   = ast_gld.add_child(c191,   std::make_tuple("K9",     ntype_dbg(Lnast_ntype_func_def), 1));
//auto c19131  = ast_gld.add_child(c1913,  std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref), 1));
//auto c19132  = ast_gld.add_child(c1913,  std::make_tuple("$a",     ntype_dbg(Lnast_ntype_input), 1));
//auto c19133  = ast_gld.add_child(c1913,  std::make_tuple("$b",     ntype_dbg(Lnast_ntype_input), 1));
//auto c19134  = ast_gld.add_child(c1913,  std::make_tuple("%o",     ntype_dbg(Lnast_ntype_output), 1));
//(void) c19131;
//(void) c19132;
//(void) c19133;
//(void) c19134;
//
//auto c1a  = ast_gld.add_child(c1,  std::make_tuple("K15",    ntype_dbg(Lnast_ntype_label), 0));
//auto c1a1 = ast_gld.add_child(c1a, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
//auto c1a2 = ast_gld.add_child(c1a, std::make_tuple("a",      ntype_dbg(Lnast_ntype_ref),   0));
//auto c1a3 = ast_gld.add_child(c1a, std::make_tuple("0d3",    ntype_dbg(Lnast_ntype_const), 0));
//(void) c1a1;
//(void) c1a2;
//(void) c1a3;
//
//
//auto c1b  = ast_gld.add_child(c1,  std::make_tuple("K16",    ntype_dbg(Lnast_ntype_label), 0));
//auto c1b1 = ast_gld.add_child(c1b, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
//auto c1b2 = ast_gld.add_child(c1b, std::make_tuple("b",      ntype_dbg(Lnast_ntype_ref),   0));
//auto c1b3 = ast_gld.add_child(c1b, std::make_tuple("0d4",    ntype_dbg(Lnast_ntype_const), 0));
//(void) c1b1;
//(void) c1b2;
//(void) c1b3;
//
//auto c1c  = ast_gld.add_child(c1, std::make_tuple("K17",     ntype_dbg(Lnast_ntype_func_call), 0));
//auto c1c1 = ast_gld.add_child(c1c, std::make_tuple("___g",   ntype_dbg(Lnast_ntype_ref),   0));
//auto c1c2 = ast_gld.add_child(c1c, std::make_tuple("fun1",   ntype_dbg(Lnast_ntype_ref),   0));
//auto c1c3 = ast_gld.add_child(c1c, std::make_tuple("___h",   ntype_dbg(Lnast_ntype_ref),   0));
//auto c1c4 = ast_gld.add_child(c1c, std::make_tuple("___i",   ntype_dbg(Lnast_ntype_ref),   0));
//(void) c1c1;
//(void) c1c2;
//(void) c1c3;
//(void) c1c4;
//
//
//auto c1d   = ast_gld.add_child(c1,   std::make_tuple("K18",     ntype_dbg(Lnast_ntype_pure_assign), 0));
//auto c1d1  = ast_gld.add_child(c1d,  std::make_tuple("result",  ntype_dbg(Lnast_ntype_ref), 0));
//auto c1d2  = ast_gld.add_child(c1d,  std::make_tuple("___g",    ntype_dbg(Lnast_ntype_ref), 0));
//(void) c1d1;
//(void) c1d2;
//
//ast_gld.each_breadth_first_fast([this](const Tree_index &parent, const Tree_index &self, tuple tuple_data) {
//while (static_cast<size_t>(self.level)>=ast_sorted_golden.size())
//ast_sorted_golden.emplace_back();
//ast_sorted_golden[self.level].emplace_back(tuple_data);
//EXPECT_EQ(ast_gld.get_parent(self), parent);
//});
