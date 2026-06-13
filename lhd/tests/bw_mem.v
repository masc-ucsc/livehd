// Synchronous RAM for the O2 bitwidth memory path (Bitwidth::process_memory).
module bw_mem(input clk, input we, input [3:0] waddr, input [3:0] raddr,
              input [7:0] din, output reg [7:0] dout);
  reg [7:0] m [15:0];
  always @(posedge clk) begin
    if (we) m[waddr] <= din;
    dout <= m[raddr];
  end
endmodule
