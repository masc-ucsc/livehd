// Golden for comb_named_tuple_out.prp.
module top (
  input  [3:0] x,
  output [3:0] wa, output wb,
  output [3:0] fa, output fb,
  output [3:0] ta, output tb,
  output [3:0] ma, output mb
);
  assign wa = x ^ 4'd1;  assign wb = (x == 4'd3);
  assign fa = x ^ 4'd2;  assign fb = (x == 4'd1);
  assign ta = x ^ 4'd4;  assign tb = (x == 4'd5);
  assign ma = x ^ 4'd8;  assign mb = (x == 4'd7);
endmodule
