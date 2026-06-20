module \wire_reg.wreg (input clock, input reset, input [7:0] a, output [8:0] o);
  assign o = a + 1;          // nx is the next-state value, available this cycle
endmodule
