// Upward-reference fixture (lhd_formal_hier_ref_test.sh): leaf reads top.x, refused.
module leaf(input logic clk_i, input logic a);
  Up: assert property (@(posedge clk_i) top.x == a);
endmodule
module top(input logic clk_i, input logic a);
  logic x;
  assign x = a;
  leaf l(.clk_i, .a);
endmodule
