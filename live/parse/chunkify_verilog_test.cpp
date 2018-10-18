

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
"`ifdef NOTHING  \n"
"  a = ; \n"
"  `endif  \n"
"      /* comment */                                \n"
"    module test1_moda(input [7:0] a, input [7:0] b,\n"
"  output signed [7:0] h\n"
");\n"
"                                                   \n"
"  signed\twire\t[7:0] as = a;signed wire [7:0] bs = b;   \n"
" /* endmodule module in comment */                 \n"
"  wire [7:0] f = as + bs;assign h = as+bs-as; // endmodule inside comment\n"
"  /* module a() /* nested module /* aa */ */*/     \n"
"endmodule\tmodule test1_modb(a,b,\n"
"  h) ;\n"
"  input a;\n"
"  output h;\n"
"  input b;\n"
"\n"
"  reg [4:0] a;\n"
"  reg [0:4] b;\n"
"\n"
"  assign h = a ^ b;\n"
" typo in this block of code \"module\" \n"
"endmodule"; // No return/space after endmodule

  chunker.parse("test1.v", test1_verilog.c_str(), test1_verilog.size());

  EXPECT_NE(access("test1_moda.v", R_OK), -1);
  EXPECT_NE(access("test1_modb.v", R_OK), -1);
}

