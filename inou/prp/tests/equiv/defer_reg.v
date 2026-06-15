module \defer_reg.dreg (input clock, input reset, input [7:0] a, output [8:0] o);
  assign o = a + 1;          // r.[defer] is the next-state value, available this cycle
endmodule
