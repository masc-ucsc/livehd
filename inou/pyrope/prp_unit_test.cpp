#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"

#include "prp.hpp"
#include "fmt/format.h"

class Prp_test: public ::testing::Test{
public:
  Prp scanner;
};

TEST_F(Prp_test, assignment_expression) {

  std::string_view parse_txt{"\%out4 as (__bits:8)"};
  
  scanner.parse("assignment_expression", parse_txt, parse_txt.size());
}
