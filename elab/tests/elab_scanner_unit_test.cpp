//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gtest/gtest.h"

#include "elab_scanner.hpp"
#include "fmt/format.h"

class Test_scanner : public Elab_scanner{
  public:
    std::vector<std::string> debug_token_list;
    void elaborate(){
      while(!scan_is_end()){
        std::string token;
        scan_append(token);
        debug_token_list.push_back(token);
        scan_next();
      }
    }
};

class Elab_test : public ::testing::Test{
public:
  Test_scanner scanner;
};

TEST_F(Elab_test, token_comp1){
  std::string_view txt1("<<\n++\n--\n==\n<=\n>=");
  Elab_scanner::Token_list tlist;
  scanner.parse("txt1", txt1, tlist);
  EXPECT_EQ(9, scanner.debug_token_list.size());
  EXPECT_EQ(9, tlist.size());

  int tok_num = 0;

  for(auto i = scanner.debug_token_list.begin(); i != scanner.debug_token_list.end(); ++i){
    switch(tok_num){
      case 0:
        EXPECT_EQ(0, i->compare("<"));
        break;
      case 1:
        EXPECT_EQ(0, i->compare("<"));
        break;
      case 2:
        EXPECT_EQ(0, i->compare("+"));
        break;
      case 3:
        EXPECT_EQ(0, i->compare("+"));
        break;
      case 4:
        EXPECT_EQ(0, i->compare("-"));
        break;
      case 5:
        EXPECT_EQ(0, i->compare("-"));
        break;
      case 6:
        EXPECT_EQ(0, i->compare("=="));
        break;
      case 7:
        EXPECT_EQ(0, i->compare("<="));
        break;
      case 8:
        EXPECT_EQ(0, i->compare(">="));
        break;
    }
    // std::cout << *i << std::endl;
    tok_num++;
  }
}

TEST_F(Elab_test, token_comp2){
  std::string_view txt1("=>foo<=33+_foo");
  Elab_scanner::Token_list tlist;
  scanner.parse("txt1", txt1, tlist);
  EXPECT_EQ(7, scanner.debug_token_list.size());

  EXPECT_EQ(scanner.debug_token_list[0], "=");
  EXPECT_EQ(scanner.debug_token_list[1], ">");
  EXPECT_EQ(scanner.debug_token_list[2], "foo");
  EXPECT_EQ(scanner.debug_token_list[3], "<=");
  EXPECT_EQ(scanner.debug_token_list[4], "33");
  EXPECT_EQ(scanner.debug_token_list[5], "+");
  EXPECT_EQ(scanner.debug_token_list[6], "_foo");
}

TEST_F(Elab_test, token_comp3){
  std::string_view txt1("100s3bit /*110 /* comment */ 33*/_3_44_u32bits");
  Elab_scanner::Token_list tlist;
  scanner.parse("txt1", txt1, tlist);
  EXPECT_EQ(3, scanner.debug_token_list.size());

  EXPECT_EQ(scanner.debug_token_list[0], "100s3bit");
  EXPECT_EQ(scanner.debug_token_list[1], "/*110 /* comment */ 33*/"); // comment
  EXPECT_EQ(scanner.debug_token_list[2], "_3_44_u32bits");
}

TEST_F(Elab_test, token_comp4){
  std::string_view txt1("\%in\n@out=a+b");
  Elab_scanner::Token_list tlist;
  scanner.parse("txt1", txt1, tlist);
  EXPECT_EQ(6, scanner.debug_token_list.size());
  
  EXPECT_EQ(scanner.debug_token_list[0], "\%in");
  EXPECT_EQ(scanner.debug_token_list[1], "@out");
  EXPECT_EQ(scanner.debug_token_list[2], "=");
  EXPECT_EQ(scanner.debug_token_list[3], "a");
  EXPECT_EQ(scanner.debug_token_list[4], "+");
  EXPECT_EQ(scanner.debug_token_list[5], "b");
}

