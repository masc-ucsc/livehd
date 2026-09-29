/*
*/
module psigned(
  input  signed [7:0] a,
  input  signed [7:0] b,
  output signed [15:0] r
);
  wire struct packed {logic signed [7:0] lo; logic signed [7:0] hi; logic signed [15:0] prod;} s;
  wire signed [15:0] p = s.lo * s.hi;
  assign s = '{lo: a, hi: b, prod: p + s.lo};
  assign r = s.prod;
endmodule
