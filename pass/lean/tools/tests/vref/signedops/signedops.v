// Signed operands in a wider context. Verilog sign-extends a signed operand to the width of its
// context before the operation, and an expression that mixes signed and unsigned operands is
// unsigned (its signed operands are zero-extended). The certificate must say the same.
module signedops (
  input clk,
  input [7:0] a8, input [7:0] b8, input [15:0] a16, input [3:0] amt,
  output [15:0] shl16, output [15:0] sra16, output [15:0] shr16, output [15:0] add16,
  output [15:0] mix16, output [15:0] mul16, output [15:0] div16, output [15:0] mod16,
  output [15:0] neg16, output [15:0] mux16, output [15:0] cast16, output [3:0] cmp,
  output [15:0] q16
);
  wire signed [7:0]  sa  = a8;
  wire signed [7:0]  sb  = b8;
  wire signed [15:0] s16 = a16;
  assign shl16  = sa << amt;              // sa sign-extended to 16 bits, then shifted
  assign sra16  = sa >>> amt;             // arithmetic shift of the extended value
  assign shr16  = sa >> amt;              // logical shift of the sign-extended value
  assign add16  = sa + sb;                // both sign-extended
  assign mix16  = sa + b8;                // mixed: unsigned, so sa is zero-extended
  assign mul16  = sa * sb;
  assign div16  = s16 / sb;               // b8 is never 0 (the generator)
  assign mod16  = s16 % 16'sd7;
  assign neg16  = -sa;
  assign mux16  = amt[0] ? sa : s16;      // the extension goes through the mux
  assign cast16 = $signed(a8) >>> 2;
  assign cmp    = {sa < s16, sa > sb, $signed(a8) < $signed(b8), a8 < b8};
  reg signed [15:0] q;
  always @(posedge clk) q <= sa * sb + s16;
  assign q16 = q;
endmodule
