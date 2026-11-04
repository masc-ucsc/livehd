// Hierarchical-read fixture (lhd_formal_hier_ref_test.sh): top reads c.q and a.b.q.
module cnt(input logic clk_i, input logic rst_ni, input logic en, output logic odd);
  logic [3:0] q;
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) q <= '0; else if (en) q <= q + 1;
  assign odd = q[0];
endmodule
module mid(input logic clk_i, input logic rst_ni, input logic en);
  cnt b(.clk_i, .rst_ni, .en, .odd());
endmodule
module top(input logic clk_i, input logic rst_ni, input logic en, output logic o1, output logic o2);
  logic [3:0] mirror;
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) mirror <= '0; else if (en) mirror <= mirror + 1;
  cnt c(.clk_i, .rst_ni, .en, .odd(o1));
  cnt c2(.clk_i, .rst_ni, .en(1'b0), .odd(o2));
  mid a(.clk_i, .rst_ni, .en);
  HierEq: assert property (@(posedge clk_i) disable iff (!rst_ni) c.q == mirror);
  DeepEq: assert property (@(posedge clk_i) disable iff (!rst_ni) a.b.q == mirror);
  HierZero: assert property (@(posedge clk_i) disable iff (!rst_ni) c.q == 4'd0);
endmodule
