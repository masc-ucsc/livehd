module top(
  input  [7:0] x,
  input  [7:0] y,
  input        s,
  output [7:0] r,
  output [7:0] e,
  output [7:0] o,
  output [7:0] q
);
  wire [7:0] v = s ? y : x;
  assign r = (v ^ 8'd1) - v;
  assign e = s ? (y ^ 8'd1) : x;
  assign o = v + 8'd1;
  assign q = v + 8'd1;
endmodule
