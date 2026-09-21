/*
*/
module top(
  input        clock,
  input        v,
  input  [3:0] d,
  output [3:0] q,
  output       qv
);
  reg [3:0] r;
  always @(posedge clock)
    r <= v ? d : 4'b1111;
  reg rv;
  always @(posedge clock)
    rv <= v;
  assign q  = r;
  assign qv = rv;
endmodule
