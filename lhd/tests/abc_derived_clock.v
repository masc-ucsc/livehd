// Registers whose clock is computed INSIDE the module that holds them (no hierarchy
// boundary to make it a region input): a latch-based gate `clk & latch`, and a plain
// combinational derived clock. With no integrated clock-gate cell in the Liberty they stay
// native flops (derived-clock-native); with pass.abc.derived_clock=true they map to DFF
// cells whose clock pin the mapped logic drives (lhd_abc_derived_clock_test.sh).
module abc_derived_clock(input clk, input rst_n, input en0, input en1, input [3:0] d,
                         output [3:0] qa, output [3:0] qb, output [3:0] qc);
  reg en_l;
  always_latch if (!clk) en_l = en0;
  wire g0 = clk & en_l;       // latch-based gate

  reg [3:0] a;                // plain register on the gated clock
  always @(posedge g0) a <= a + d;

  reg [3:0] b;                // async-reset register (clear and preset bits) on the gated clock
  always @(posedge g0 or negedge rst_n)
    if (!rst_n) b <= 4'd5;
    else b <= b ^ d;

  wire g1 = clk & en1 & ~d[0];  // a combinational derived clock, not a latch gate
  reg [3:0] c;
  always @(posedge g1) c <= c - d;

  assign qa = a;
  assign qb = b;
  assign qc = c;
endmodule

// A negedge register on a derived clock stays native even with the option: the
// selected cell is posedge-only, so it keeps the documented derived-clock-native path.
module abc_derived_clock_neg(input clk, input en, input [3:0] d, output [3:0] q);
  reg en_l;
  always_latch if (!clk) en_l = en;
  wire g = clk & en_l;
  reg [3:0] r;
  always @(negedge g) r <= r + d;
  assign q = r;
endmodule
