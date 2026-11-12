// Signed and unsigned `%` (yosys $mod) read back in signed and unsigned
// contexts. The remainder truncates: its sign follows the dividend, so a % 3
// is in [-2, 2] for a signed 4-bit a. rem_sign_ref.sv is the same design with
// each `x % c` written as `x - (x / c) * c`.
module rem_sign(
  input  signed [3:0] a,
  input  signed [3:0] b,
  input         [3:0] ua,
  output              ge0,    // a % 3 >= 0
  output              lt0,    // a % 3 < 0
  output              eqm1,   // a % 3 == -1
  output        [3:0] r_o,    // a % 3 on an unsigned port: its four bits
  output              ngeq,   // a % -3 >= 0
  output signed [7:0] r8,     // a % 3 at eight bits: sign-extended
  output              r2ge0,  // a % 3 kept in two signed bits, >= 0
  output              vge0,   // a % b >= 0 (b != 0)
  output              veqm1,  // a % b == -1 (b != 0)
  output        [3:0] ur      // unsigned ua % 3
);
  wire signed [3:0] r  = a % 4'sd3;
  wire signed [3:0] rn = a % -4'sd3;
  wire signed [1:0] r2 = a % 4'sd3;
  wire signed [3:0] rv = (b == 4'sd0) ? 4'sd0 : a % b;
  assign ge0   = r >= 4'sd0;
  assign lt0   = r < 4'sd0;
  assign eqm1  = r == -4'sd1;
  assign r_o   = r;
  assign ngeq  = rn >= 4'sd0;
  assign r8    = a % 4'sd3;
  assign r2ge0 = r2 >= 2'sd0;
  assign vge0  = rv >= 4'sd0;
  assign veqm1 = rv == -4'sd1;
  assign ur    = ua % 4'd3;
endmodule
