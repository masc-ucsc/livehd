module top(input [7:0] a, output [7:0] z, output [7:0] y, output [7:0] x, output [7:0] w, output [7:0] u, output [7:0] t, output [7:0] s, output [7:0] q, output [7:0] p);
  assign z = a ^ 8'hFF;
  assign y = a ^ 8'hFF;
  assign x = a ^ 8'hFF;
  assign w = a ^ 8'hFF;
  assign u = a ^ 8'hFF;
  assign t = a ^ 8'hFF;
  assign s = a | 8'h80;
  assign q = a | 8'h80;
  assign p = a | 8'h80;
endmodule
