// Registers whose clock is computed INSIDE the module that holds them (no hierarchy
// boundary to make it a region input): a latch-based gate `clk & latch`, and a plain
// combinational derived clock. With no integrated clock-gate cell in the Liberty they stay
// native flops (derived-clock-native); with synth.derived_clock=true they map to DFF
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

// A negedge register on a derived clock: the cell is posedge-only, so the mapped clock net
// goes through one shared inverter (abc_clock_inv_*) and the register stays a DFF cell.
module abc_derived_clock_neg(input clk, input en, input [3:0] d, output [3:0] q);
  reg en_l;
  always_latch if (!clk) en_l = en;
  wire g = clk & en_l;
  reg [3:0] r;
  always @(negedge g) r <= r + d;
  assign q = r;
endmodule

// NVDLA's CKLNQD12 shape: the gate's enable is held by a NEGEDGE FLOP, `gclk = clk & en_q`
// (not a latch, so no integrated clock-gate cell is equivalent). The flop is an ordinary
// falling-edge register on the region clock, the AND is mapped logic, and registers on the
// gate take the mapped net as their clock -- also falling-edge ones.
module abc_derived_clock_negff(input clk, input en, input [3:0] d, output [3:0] qa, output [3:0] qb);
  reg en_q;
  always @(negedge clk) en_q <= en;
  wire g = clk & en_q;
  reg [3:0] a;
  always @(posedge g) a <= a + d;
  reg [3:0] b;
  always @(negedge g) b <= b ^ d;
  assign qa = a;
  assign qb = b;
endmodule

// The reset x clock-edge matrix on the region clock (no derived clock): every flavour keeps its
// meaning through synthesis -- async resets map to clear/preset cells whatever the clock edge and
// level, sync resets fold into D -- and the unbounded LEC proves it.
module abc_reset_edge_matrix(input clk, input rst_n, input rst_h, input [3:0] d,
                             output [3:0] pa, output [3:0] na, output [3:0] ps, output [3:0] ns, output [3:0] nh);
  reg [3:0] a, b, c, e, f;
  always @(posedge clk or negedge rst_n) if (!rst_n) a <= 4'd5; else a <= a ^ d;
  always @(negedge clk or negedge rst_n) if (!rst_n) b <= 4'd5; else b <= b ^ d;
  always @(posedge clk) if (!rst_n) c <= 4'd5; else c <= c ^ d;
  always @(negedge clk) if (!rst_n) e <= 4'd5; else e <= e ^ d;
  always @(negedge clk or posedge rst_h) if (rst_h) f <= 4'd3; else f <= f ^ d;
  assign pa = a; assign na = b; assign ps = c; assign ns = e; assign nh = f;
endmodule
