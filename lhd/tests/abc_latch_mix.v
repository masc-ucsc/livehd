// Level-sensitive data latches pass.abc maps onto Liberty latch cells
// (lhd_abc_latch_test.sh): both enable polarities, a 1p/2p latch pipeline,
// resets the reader folds into the enable and D, latches enabled by a
// latch-based clock gate's output (directly and through a child's clock
// port), and a latch on a computed enable.

// active-high and active-low latches, a 1p/2p pipeline, folded resets
module abc_latch_mix(input clk, input rst, input rst_n, input [3:0] d, input [3:0] e,
                     output [3:0] qa, output [3:0] qb, output [3:0] qc, output [3:0] qd, output [3:0] qe,
                     output [3:0] qf);
  logic [3:0] a;
  always_latch if (clk) a = d;
  logic [3:0] b;
  always_latch if (!clk) b = d ^ e;
  logic [3:0] p1, p2;
  always_latch if (!clk) p1 = d;
  always_latch if (clk) p2 = p1 + 4'd1;
  logic [3:0] r;  // clear and preset bits, active-high reset
  always_latch if (rst) r = 4'b0110; else if (clk) r = e;
  logic [3:0] s;  // active-low reset, active-low enable
  always_latch if (!rst_n) s = 4'b1001; else if (!clk) s = d;
  assign qa = a;
  assign qb = b;
  assign qc = p1;
  assign qd = p2;
  assign qe = r;
  assign qf = s;
endmodule

// A latch transparent while its clock port is LOW (minion's register-file
// preview latch), driven from a clock gate in the parent.
module abc_latch_preview(input pclk, input [3:0] d, output logic [3:0] q);
  always_latch if (!pclk) q <= d;
endmodule

// a data latch transparent while an ICG output is HIGH
module abc_latch_gated(input clk, input en, input [3:0] d, output [3:0] qg);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  logic [3:0] g;
  always_latch if (gclk) g = d;
  assign qg = g;
endmodule

// the preview shape: transparent while the ICG output is LOW, through a child
module abc_latch_gprev(input clk, input en, input [3:0] e, output [3:0] qh);
  logic en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  abc_latch_preview u_prev(.pclk(gclk), .d(e), .q(qh));
endmodule

// A computed enable: the cell's enable pin is driven by mapped logic.
module abc_latch_cen(input clk, input en, input [3:0] d, input [3:0] e, output [3:0] qc);
  logic [3:0] c;
  always_latch if (clk && en) c = d + e;
  assign qc = c;
endmodule
