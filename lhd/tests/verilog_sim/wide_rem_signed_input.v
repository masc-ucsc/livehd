// A 70-bit unsigned remainder whose divisor ORs in a NEGATIVE signed input. The
// llvm backend loaded the 70-bit input as `load i70` from sign-extended packed
// words -- undefined LLVM when the padding bits were not written by an i70
// store -- so `zext i70 -> i72` set bits 70-71 and the divide used the wrong
// divisor (random Verilog sim fuzz, 2026-10-09; Icarus, Verilator and slop
// agreed on 706863232 for the low bits).
module wide_rem_signed_input(input clock, input reset, input [32:0] i0, input [6:0] i1, input signed i2,
                             input signed [69:0] i3, input [7:0] i4, output [69:0] r);
  reg [15:0] t1;
  always @(*) begin
    t1 = i1;
    if (i4 == 0) t1 = (i3 ? 16'd5 : (i1 ? i2 : 1'b0));
  end
  assign r = {70{1'b1}} % ((t1 | ((i0 | i2) ? i3 : (i1 | i3))) | 1'b1);
endmodule
