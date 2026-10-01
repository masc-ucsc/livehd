// Golden for tuple_field_cast.prp: bool -> u1 is the bit itself, and a
// widening uN() cast of an unsigned field zero-extends.
module top (
  input  [5:0] \req.addr ,
  input        \req.write ,
  input  [7:0] \req.data ,
  input        w,
  input  [5:0] a,
  input        s,
  input  [3:0] n4,
  input  signed [3:0] s4,
  output       y0,
  output [7:0] y1,
  output       y2,
  output       y3,
  output [7:0] y4,
  output       y5,
  output [7:0] y6,
  output       y7,
  output signed [7:0] y8,
  output [7:0] y9,
  output signed [5:0] y10
);
  assign y0 = \req.write ;
  assign y1 = {2'b00, \req.addr };
  assign y2 = \req.write ;
  assign y3 = w;
  assign y4 = {2'b00, a};
  assign y5 = s;
  assign y6 = {2'b00, a};
  assign y7 = \req.write ;
  assign y8 = {4'b0000, n4};
  assign y9 = {{4{s4[3]}}, s4};
  assign y10 = {2'b00, n4};
endmodule
