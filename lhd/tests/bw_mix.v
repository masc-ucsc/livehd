// Cell-type mix for the --recipe O2 (pass.bitwidth) regression: sum, mult,
// bitwise ops, comparators, shifts (logic + arithmetic), mux and a flop.
module bw_mix(input clk, input [7:0] a, input [7:0] b, input sel,
              output reg [15:0] q, output [7:0] y, output z);
  wire [15:0] m   = a * b;
  wire [8:0]  s   = a + b;
  wire [7:0]  ba  = a & b;
  wire [7:0]  bo  = a | b;
  wire [7:0]  bx  = a ^ b;
  wire [7:0]  bn  = ~a;
  wire        cmp = a < b;
  wire        ceq = a == b;
  wire [7:0]  sh  = a << 2;
  wire [7:0]  sr  = b >> 1;
  wire signed [7:0] sa = $signed(a) >>> 1;
  wire [7:0]  mx  = sel ? ba : bo;
  assign y = mx + bx + bn + sh + sr + sa[7:0];
  assign z = cmp ^ ceq;
  always @(posedge clk) begin
    q <= m + {7'b0, s};
  end
endmodule
