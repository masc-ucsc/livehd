
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_lexer.hpp"
#include "lnast_ntype.hpp"
#include "lnast_parser.hpp"
#include "lnast_writer.hpp"
#include "ln_test_utils.hpp"

class Lnast_parser_writer_test : public ::testing::Test {};

TEST_F(Lnast_parser_writer_test, parse_then_write_eq) {
  for (const auto& entry : std::filesystem::directory_iterator("./lnast/tests/ln")) {
    std::print("\nTest - {}\n", std::string{entry.path()});

    // Parser test
    std::ifstream fs;
    fs.open(entry.path());
    Ln_test_parser parser(fs);
    auto         lnast = parser.parse_all();

    std::cout << "\nlnast->print():\n\n";
    lnast->print();

    // Writer test
    std::stringstream ss;
    Lnast_writer      writer(ss, lnast);
    writer.write_all();

    // std::cout << "\n------------\n";
    // std::cout << ss.rdbuf();
    // std::cout << "\n------------\n";

    // Lex diff (ignore comments and spaces difference)
    fs.clear();  // Re-read fs
    fs.seekg(0, std::ios::beg);

    Lnast_lexer lexer_source(fs);
    Lnast_lexer lexer_target(ss);

    while (true) {
      auto token_source = lexer_source.lex_token();
      auto token_target = lexer_target.lex_token();
      EXPECT_TRUE(token_to_string(token_source) == token_to_string(token_target))
          << "Token mismatch : Source( " << token_to_string(token_source) << " ), Target( " << token_to_string(token_target) << " )\n";
      if (token_source.is(Lnast_token::eof) || token_target.is(Lnast_token::eof)) {
        break;
      }
    }

    std::cout << "\n";
  }
}

TEST_F(Lnast_parser_writer_test, dump_read_roundtrip) {
  for (const auto& entry : std::filesystem::directory_iterator("./lnast/tests/ln")) {
    std::ifstream fs(entry.path());
    Ln_test_parser parser(fs);
    auto          lnast = parser.parse_all();

    std::stringstream first;
    lnast->dump(first);

    std::stringstream first_in(first.str());
    auto              loaded = Lnast::read(first_in);
    ASSERT_TRUE(loaded != nullptr);

    std::stringstream second;
    loaded->dump(second);

    EXPECT_EQ(first.str(), second.str()) << "dump/read round-trip diverged on " << std::string{entry.path()};
  }
}

// Task 1t — the new `declare` / `store` node types must survive the name-based
// dump/read round-trip (nothing emits them through the textual Lnast_parser yet,
// so build the tree programmatically).
TEST_F(Lnast_parser_writer_test, declare_store_dump_read_roundtrip) {
  auto ln = std::make_shared<Lnast>("t1t");

  auto top   = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(top, Lnast_ntype::create_stmts());

  // declare x : #int(255, 0) "mut" = 42   (prim_type_int carries the range)
  auto d = ln->add_child(stmts, Lnast_ntype::create_declare());
  ln->add_child(d, Lnast_node::create_ref("x"));
  auto ty = ln->add_child(d, Lnast_ntype::create_prim_type_int());
  ln->add_child(ty, Lnast_node::create_const("255"));  // max
  ln->add_child(ty, Lnast_node::create_const("0"));    // min
  ln->add_child(d, Lnast_node::create_const("mut"));
  ln->add_child(d, Lnast_node::create_const("42"));

  // store x = 99   (0 levels == old assign)
  auto s = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(s, Lnast_node::create_ref("x"));
  ln->add_child(s, Lnast_node::create_const("99"));

  // store y[0][a] = 7   (N levels == old tuple_set path)
  auto s2 = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(s2, Lnast_node::create_ref("y"));
  ln->add_child(s2, Lnast_node::create_const("0"));
  ln->add_child(s2, Lnast_node::create_const("a"));
  ln->add_child(s2, Lnast_node::create_const("7"));

  (void)s2;

  std::stringstream first;
  ln->dump(first);
  EXPECT_NE(first.str().find("declare"), std::string::npos) << "dump missing declare node:\n" << first.str();
  EXPECT_NE(first.str().find("store"), std::string::npos) << "dump missing store node:\n" << first.str();

  std::stringstream first_in(first.str());
  auto              loaded = Lnast::read(first_in);
  ASSERT_TRUE(loaded != nullptr);

  std::stringstream second;
  loaded->dump(second);
  EXPECT_EQ(first.str(), second.str()) << "declare/store dump/read round-trip diverged";
}
