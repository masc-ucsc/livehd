// Four nibble adders whose carry inputs and outputs are assembled as packed
// words.  The cin <-> cout dependency is cyclic at word granularity but forms
// an ordinary acyclic carry chain at bit granularity.
module packed_carry_chain (
  input  logic [15:0] a,
  input  logic [15:0] b,
  output logic [15:0] result
);
  wire [3:0] cin;
  wire [3:0] cout;
  wire [4:0] bits_0 = {1'b0, a[3:0]}   + {1'b0, b[3:0]}   + cin[0];
  wire [4:0] bits_1 = {1'b0, a[7:4]}   + {1'b0, b[7:4]}   + cin[1];
  wire [4:0] bits_2 = {1'b0, a[11:8]}  + {1'b0, b[11:8]}  + cin[2];
  wire [4:0] bits_3 = {1'b0, a[15:12]} + {1'b0, b[15:12]} + cin[3];

  assign cout = {bits_3[4], bits_2[4], bits_1[4], bits_0[4]};
  assign cin = {cout[2:0], 1'b0};
  assign result = {bits_3[3:0], bits_2[3:0], bits_1[3:0], bits_0[3:0]};
endmodule
