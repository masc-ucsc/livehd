// rem_sign.sv with each `x % c` written as `x - (x / c) * c` (Verilog `/`
// truncates too, so the two agree for every sign). No $mod cell.
module rem_sign(
  input  signed [3:0] a,
  input  signed [3:0] b,
  input         [3:0] ua,
  output              ge0,
  output              lt0,
  output              eqm1,
  output        [3:0] r_o,
  output              ngeq,
  output signed [7:0] r8,
  output              r2ge0,
  output              vge0,
  output              veqm1,
  output        [3:0] ur
);
  wire signed [3:0] r  = a - (a / 4'sd3) * 4'sd3;
  wire signed [3:0] rn = a - (a / -4'sd3) * -4'sd3;
  wire signed [1:0] r2 = a - (a / 4'sd3) * 4'sd3;
  wire signed [3:0] rv = (b == 4'sd0) ? 4'sd0 : a - (a / b) * b;
  assign ge0   = r >= 4'sd0;
  assign lt0   = r < 4'sd0;
  assign eqm1  = r == -4'sd1;
  assign r_o   = r;
  assign ngeq  = rn >= 4'sd0;
  assign r8    = a - (a / 4'sd3) * 4'sd3;
  assign r2ge0 = r2 >= 2'sd0;
  assign vge0  = rv >= 4'sd0;
  assign veqm1 = rv == -4'sd1;
  assign ur    = ua - (ua / 4'd3) * 4'd3;
endmodule
