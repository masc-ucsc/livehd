// Golden for tuple_value_cond.prp: every pair is a 2:1 mux on `s` between the
// reassigned value and the initial one.
module top (
  input  [3:0] x,
  input        s,
  input        t,
  output [3:0] pa, output pb,
  output [3:0] qa, output qb,
  output [3:0] ra, output rb,
  output [3:0] ca, output cb,
  output [3:0] ka, output kb,
  output [3:0] ga, output gb,
  output [3:0] ea, output eb,
  output [3:0] ha, output [3:0] hb,
  output [3:0] ma, output [3:0] mb
);
  wire [3:0] x2 = x ^ 4'd2;
  assign pa = s ? (x ^ 4'd3) : (x ^ 4'd1);
  assign pb = s ? (x == 4'd1) : (x == 4'd3);
  assign qa = s ? (x2 ^ 4'd1) : (x ^ 4'd1);
  assign qb = s ? (x2 == 4'd3) : (x == 4'd3);
  assign ra = s ? (x2 ^ 4'd1) : (x ^ 4'd1);
  assign rb = s ? (x2 == 4'd3) : (x == 4'd3);
  assign ca = s ? (x ^ 4'd3) : (x ^ 4'd1);
  assign cb = s ? (x == 4'd1) : (x == 4'd3);
  assign ka = s ? x : 4'd5;
  assign kb = s ? (x == 4'd2) : 1'b1;
  assign ga = s ? 4'd1 : t ? (x2 ^ 4'd1) : (x ^ 4'd1);
  assign gb = s ? 1'b0 : t ? (x2 == 4'd3) : (x == 4'd3);
  assign ea = s ? 4'd9 : t ? (x ^ 4'd7) : x;
  assign eb = s ? t : t ? 1'b0 : s;
  assign ha = t ? 4'd9 : s ? 4'd7 : x;
  assign hb = s ? x : 4'd2;
  assign ma = s ? x : 4'd7;
  assign mb = s ? (x ^ 4'd5) : 4'd6;
endmodule
