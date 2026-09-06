// One array with synchronous writes, an asynchronous read, and a registered
// read. The Yosys RD_CLK_ENABLE vector is a bit mask, never the Memory type.
module memory_mixed_read_latency(input clk, we, re, input [1:0] wa, ra, rb,
                                input [7:0] d, output [7:0] qa, output reg [7:0] qb);
  reg [7:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= d;
    if (re) qb <= mem[rb];
  end
  assign qa = mem[ra];
endmodule
