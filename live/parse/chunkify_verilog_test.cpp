

#include <iostream>

#include "gtest/gtest.h"

#include "chunkify_verilog.hpp"

class VTest1 : public ::testing::Test {
protected:
  void SetUp() override {
  }
};

TEST_F(VTest1, interface) {

  unlink("test1_moda.v");
  unlink("test1_modb.v");

  Chunkify_verilog chunker(".");

  std::string test1_verilog = "  "
"                                                   "
"    module test1_moda(input [7:0] a, input [7:0] b,"
"  output signed [7:0] h"
");"
"                                                   "
"  signed\twire\t[7:0] as = a;signed wire [7:0] bs = b;   "
" /* endmodule module in comment */                 "
"  wire [7:0] f = as + bs;assign h = as+bs-as; // endmodule inside comment"
"  /* module a() /* nested module /* aa */ */*/     "
"endmodule\tmodule test1_modb(a,b,"
"  h) ;"
"  input a;"
"  output h;"
"  input b;"
""
"  reg [4:0] a;"
"  reg [0:4] b;"
""
"  assign h = a ^ b;"
" typo in this block of code \"module\" "
"endmodule"; // No return/space after endmodule

  chunker.parse("test1.v", test1_verilog.c_str(), test1_verilog.size());

  EXPECT_NE(access("test1_moda.v", R_OK), -1);
  EXPECT_NE(access("test1_modb.v", R_OK), -1);
}

