module fluid_example (
//  output reg y, z, w,
  input in1, in2, in3, clk,
  output C, D, F
);

reg A, B, C, D, E, F;

/*
always @ (*) begin
end
*/

always @ (posedge clk) begin
  A <= in1;
  B <= in2;
  C <= A & B;
  D <= A | B;
  E <= (B ^ D) ^ in3;
  F <= (in3 & E) ^ E;
end
endmodule
/*
A: idx18
B: idx19
C: idx1
D: idx2
E: idx21
F: idx3
clk: idx4
in1: idx5
in2: idx6
in3: idx7
B ^ D: idx25
in3 & E: idx
A | B: idx13
A & B: idx15
flop of C: idx22
flop of D: idx23
(B ^ D) ^ in3: idx11
in3 & E: idx20
(in3 & E) ^ E: idx9
flop of F: idx24
* */
