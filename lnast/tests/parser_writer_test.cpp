
#include "lnast_lexer.hpp"
#include "lnast_parser.hpp"
#include "lnast_writer.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gtest/gtest.h"
#include <format>

class Lnast_parser_writer_test : public ::testing::Test {
};

TEST_F(Lnast_parser_writer_test, parse_then_write_eq) {
  for (const auto & entry : std::filesystem::directory_iterator("./lnast/tests/ln")) {
    std::print("\nTest - {}\n", std::string{entry.path()});

    // Parser test
    std::ifstream fs;
    fs.open(entry.path());
    Lnast_parser parser(fs);
    auto lnast = parser.parse_all();

    std::cout << "\nlnast->dump():\n\n";
    lnast->dump();

    // Writer test
    std::stringstream ss;
    Lnast_writer writer(ss, lnast);
    writer.write_all();

    // Lex diff (ignore comments and spaces difference)
    fs.clear();                  // Re-read fs
    fs.seekg(0, std::ios::beg);

    Lnast_lexer lexer_source(fs);
    Lnast_lexer lexer_target(ss);

    while (true) {
      auto token_source = lexer_source.lex_token();
      auto token_target = lexer_target.lex_token();
      EXPECT_TRUE(token_source.get_string() == token_target.get_string()) <<
        "Token mismatch : Source( " << token_source.get_string() << " ), Target( " <<
        token_target.get_string() << " )\n";
      if (token_source.is(Lnast_token::eof) || token_target.is(Lnast_token::eof)) {
        break;
      }
    }

    std::cout << "\n";
  }
}

