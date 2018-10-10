module fetch_block_icache(
    input clk,
    input [64-1:0] __tid2175__2770,
    input [64-1:0] __tid2240__2815,
    output [32-1:0] __tid2177__2772,
    output [32-1:0] __tid2242__2817);

  logic [32-1:0] icache[4096-1:0] /*verilator public*/;
  always @(posedge clk) begin
  end

  assign __tid2177__2772 = icache[__tid2175__2770[11:0]];
  assign __tid2242__2817 = icache[__tid2240__2815[11:0]];
endmodule