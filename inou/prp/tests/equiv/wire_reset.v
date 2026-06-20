module \wire_reset.wreset (input clock, input rst, input do_flush, input [7:0] a, output [7:0] o);
  reg [7:0] acc;
  assign o = acc;
  always @(posedge clock) acc <= (rst | do_flush) ? 8'h0 : (acc + a);
endmodule
