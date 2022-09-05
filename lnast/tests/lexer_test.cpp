
#include "lnast_lexer.hpp"
#include "gtest/gtest.h"
#include "fmt/format.h"
#include "fmt/printf.h"

#include <sstream>

class Lnast_lexer_test : public ::testing::Test {
protected:
  std::stringstream ss;
  std::unique_ptr<Lnast_lexer> lexer;

  void TearDown() override {
    lexer = nullptr;
  }

  void start() {
    ss.str("");
    ss.clear();
    lexer = nullptr;
    lexer = std::make_unique<Lnast_lexer>(ss);
  }

  void check(std::string_view golden) {
    auto result = lexer->lex_token().get_string();
    EXPECT_TRUE(result == golden) << "result = " << result << " <-> golden = " << golden << std::endl;
  }
};

TEST_F(Lnast_lexer_test, comment) {
  start();

  ss << "/* \n multi-line comment \n */";
  ss << "// comment\n";

  check("eof");
}

TEST_F(Lnast_lexer_test, strings) {
  start();

  ss << "\"abc\"";
  ss << "\"abc\\ndef\"";
  ss << "\"abc\\\"def\"";
  check("string, abc");
  check("string, abc\\ndef");
  check("string, abc\\\"def");
}

TEST_F(Lnast_lexer_test, mixed) {
  start();

  ss << "%{a} = (2, 0)\n";
  ss << "%{a}[0] = %{b}\n";
  ss << "%{c} = add(%{a}, %{b})\n";
  ss << "@{multiply}(12345, 67890)\n";

  check("id_var, a");
  check("equal");
  check("lparen");
  check("number, 2");
  check("comma");
  check("number, 0");
  check("rparen");
  
  check("id_var, a");
  check("lbrack");
  check("number, 0");
  check("rbrack");
  check("equal");
  check("id_var, b");
  
  check("id_var, c");
  check("equal");
  check("fn_plus");
  check("lparen");
  check("id_var, a");
  check("comma");
  check("id_var, b");
  check("rparen");

  check("id_fun, multiply");
  check("lparen");
  check("number, 12345");
  check("comma");
  check("number, 67890");
  check("rparen");

  check("eof");
}

TEST_F(Lnast_lexer_test, token_mapping) {
  start();

#define SS(TEXT) \
  ss << #TEXT << " ";
#define SS_TY(TEXT) \
  ss << "#" << #TEXT << " ";
#define SS_PN(TEXT) \
  ss << TEXT << " ";
#define TOKEN_PN(NAME, SPELLING) SS_PN(SPELLING)
#define TOKEN_KW(SPELLING)       SS(SPELLING)
#define TOKEN_TY(SPELLING)       SS_TY(SPELLING)
#define TOKEN_FN(NAME, SPELLING) SS(SPELLING)
#include "lnast_tokens.def"
#undef SS
#undef SS_TY
#undef SS_PN

#define CHECK(TEXT) \
  check(#TEXT);
#define TOKEN_PN(NAME, SPELLING) CHECK(NAME)
#define TOKEN_KW(SPELLING)       CHECK(kw_##SPELLING)
#define TOKEN_TY(SPELLING)       CHECK(ty_##SPELLING)
#define TOKEN_FN(NAME, SPELLING) CHECK(fn_##NAME)
#include "lnast_tokens.def"
#undef CHECK
}
