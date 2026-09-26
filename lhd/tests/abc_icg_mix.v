// Latch-based clock gates of every shape pass.abc maps onto a Liberty
// integrated clock-gate cell (lhd_abc_icg_test.sh), and the registers they
// clock: plain, asynchronous-reset (clear and preset bits), negedge, and on a
// gate chained off another gate.

// prim_clk_gate shape: event-control latch, the scan input forces the clock on
module abc_icg_prim(input clk_i, input en_i, input scan, output clk_o);
  reg en_latch;
  always @(clk_i or en_i or scan)
    if (!clk_i) en_latch <= en_i | scan;
  assign clk_o = clk_i & en_latch;
endmodule

// always_latch shape
module abc_icg_latch(input logic clk, input logic en, output logic gclk);
  logic en_l;
  always_latch if (!clk) en_l = en;
  assign gclk = clk & en_l;
endmodule

// a pre-built ICG-cell-style module (CLK/E/TE -> GCLK, `~CLK` enable)
module abc_icg_cell(input CLK, input E, input TE, output GCLK);
  reg q;
  always_latch if (~CLK) q <= E | TE;
  assign GCLK = CLK & q;
endmodule

module abc_icg_mix(input clk, input rst_n, input en0, input en1, input en2, input en3, input scan, input [3:0] d,
                   output [3:0] qa, output [3:0] qb, output [3:0] qc, output [3:0] qd, output [3:0] qe, output [3:0] qf);
  wire g0, g1, g2, g3;
  abc_icg_prim  u0(.clk_i(clk), .en_i(en0 & en2), .scan(scan), .clk_o(g0));
  abc_icg_latch u1(.clk(clk), .en(en1), .gclk(g1));
  abc_icg_latch u2(.clk(g0), .en(en3 ^ en1), .gclk(g2));  // a chain: gated off g0
  abc_icg_cell  u3(.CLK(clk), .E(en3), .TE(1'b0), .GCLK(g3));

  reg [3:0] a;  // a plain register on a gated clock
  always @(posedge g0) a <= a + d;

  reg [3:0] b;  // an async-reset register (clear + preset bits) on a gated clock
  always @(posedge g1 or negedge rst_n)
    if (!rst_n) b <= 4'b0110;
    else b <= b ^ d;

  reg [3:0] c;  // ungated
  always @(posedge clk) c <= c + a;

  reg [3:0] e;  // on the chained gate
  always @(posedge g2) e <= e - d;

  reg [3:0] f;  // negedge register on a gated clock
  always @(negedge g1) f <= f ^ b;

  reg [3:0] h;  // on the ICG-style cell
  always @(posedge g3) h <= {h[2:0], d[0] ^ c[3]};

  assign qa = a;
  assign qb = b;
  assign qc = c;
  assign qd = e;
  assign qe = f;
  assign qf = h;
endmodule

// The active-low flavour (minion's prim_clk_gate_n: the enable latched while
// the clock is HIGH, `clk | ~latch`) is not an ICG cell: its register stays a
// native flop, reported with the precise derived-clock-native reason.
module abc_icg_n(input clk, input en, input [3:0] d, output [3:0] q);
  reg en_latch;
  always_latch if (clk) en_latch = en;
  wire gclk_n = clk | ~en_latch;
  reg [3:0] r;
  always @(negedge gclk_n) r <= r + d;
  assign q = r;
endmodule
