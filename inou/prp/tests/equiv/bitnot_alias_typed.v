module bitnot_alias_typed(input [7:0] a, input [4:0] b, input signed [3:0] s,
                          output [7:0] o, output [2:0] p, output signed [3:0] q, output [4:0] r);
  assign o = ~a;
  assign p = ~b[3:1];
  assign q = ~s;
  assign r = ~b;
endmodule
