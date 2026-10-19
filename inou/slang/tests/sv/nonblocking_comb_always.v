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
    output [7:0] w_o,
    output [3:0] v_o
);
  // Migen declares its variables with a power-on value, as here.
  reg [7:0] y = 8'd0;
  reg [7:0] z = 8'd0;
  reg [7:0] w = 8'd0;
  assign y_o = y;
  assign z_o = z;
  assign w_o = w;
  // Migen's per-bit form: a whole default write dominates, bit writes override it.
  reg [3:0] v = 4'd0;
  assign v_o = v;
  always @(*) begin
    v <= 4'd0;
    v[0] <= a[0] & c;
    v[3] <= b[7] | sel[1];
  end
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
