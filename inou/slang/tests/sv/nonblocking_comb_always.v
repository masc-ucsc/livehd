// :test: lec
// A level-sensitive `always @(*)` that assigns every output on every path with
// nonblocking `<=` (the Migen/LiteX style) is combinational logic, not a
// latch. Chained blocks used to trip the latch contract (rule C).
module nonblocking_comb_always (
    input  [1:0] sel,
    input        c,
    input  [7:0] a,
    input  [7:0] b,
    output [7:0] y_o,
    output [7:0] z_o,
    output [7:0] w_o
);
  // Migen declares its variables with a power-on value; that initial value is
  // what kept the (degenerate) latch alive and made the chain a rule C pair.
  reg [7:0] y = 8'd0;
  reg [7:0] z = 8'd0;
  reg [7:0] w = 8'd0;
  assign y_o = y;
  assign z_o = z;
  assign w_o = w;
  always @(*) begin
    y <= 8'd0;
    if (c) y <= a;
    else y <= b ^ a;
  end
  always @(*) begin
    z <= 8'd0;
    case (sel)
      2'd0: z <= a;
      2'd1: z <= b;
      default: z <= a & b;
    endcase
  end
  always @(*) begin
    w <= 8'd0;
    case (sel)
      2'd0: w <= y;
      2'd2: w <= z + y;
      default: w <= z;
    endcase
  end
endmodule
