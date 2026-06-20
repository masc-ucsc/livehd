module \match_const_else.t (
  input [1:0] s,
  input [7:0] a,
  input [7:0] b,
  output [7:0] o
);
  assign o = (s == 2'd0) ? a : ((s == 2'd1) ? b : 8'd0);
endmodule
