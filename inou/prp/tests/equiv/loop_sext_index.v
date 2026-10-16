// Hand-written golden for loop_sext_index.prp. a[i] sign-extended is 0 or -1,
// so `b & a#sext[i..=i]` is b exactly when bit i is set; `t` sums the four
// sign-extended prefixes a[0:0], a[1:0], a[2:0], a[3:0].
module \loop_sext_index.top (
  input        [3:0] a,
  input        [7:0] b,
  output       [7:0] o,
  output signed [7:0] t
);
  assign o = (a != 4'b0) ? b : 8'b0;
  wire signed [7:0] p0 = $signed(a[0:0]);
  wire signed [7:0] p1 = $signed(a[1:0]);
  wire signed [7:0] p2 = $signed(a[2:0]);
  wire signed [7:0] p3 = $signed(a[3:0]);
  assign t = p0 + p1 + p2 + p3;
endmodule
