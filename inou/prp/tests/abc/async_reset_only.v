// A region whose logic is ALL native boundary: the test Liberty has no
// asynchronous clear flop, so the async-reset register stays a native flop and
// the mapped region has no real output. region_blast then adds a constant
// dummy PO, which &nf maps to Mio's zero-input `_const0_` gate; the region
// writer used to instance it, leaving a module that neither the Liberty nor the
// netlist declares, and the re-read mapped Verilog failed `unknown-module`.
module async_reset_only(input clk, input rst_n, input [3:0] d, output reg [3:0] q);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 4'd0; else q <= d;
endmodule
