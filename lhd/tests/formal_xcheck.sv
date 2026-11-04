// Case equality vs X/Z constants (lhd_formal_xcheck_test.sh).
module top(input logic clk_i, input logic rst_ni, input logic a, input logic en, input logic [3:0] b);
  logic q;
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) q <= 1'b0; else q <= a;
  NotX: assert property (@(posedge clk_i) disable iff (!rst_ni) (q !== 1'bx || !en));
  NotXWide: assert property (@(posedge clk_i) disable iff (!rst_ni) (b !== 4'b10x1));
  IsX: assert property (@(posedge clk_i) disable iff (!rst_ni) !(a === 1'bx));
  Plain: assert property (@(posedge clk_i) disable iff (!rst_ni) (b !== 4'd5));
endmodule
