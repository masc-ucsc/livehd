// A `reg signed` array stores the written BIT PATTERN re-signed at the element
// width. slop stored an unsigned 0xF8 as +248 (and the constant 16'sd26323 into
// a 5-bit element as 26323), so `0 <= mem[ra]` read true where Icarus,
// Verilator and the llvm backend read -8 / -13 (random Verilog sim fuzz,
// 2026-10-09).
module signed_mem_store(input clock, input reset, input signed [7:0] d, input [1:0] wa, input [1:0] ra,
                        output o, output signed [7:0] q, output o2, output signed [4:0] q2);
  reg signed [7:0] mem [0:3];
  reg signed [4:0] cm [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) begin mem[mi] <= 0; cm[mi] <= 0; end
    end else begin
      mem[wa] <= d;
      cm[wa]  <= 16'sd26323;
    end
  end
  assign q  = mem[ra];
  assign o  = (0 <= q);
  assign q2 = cm[ra];
  assign o2 = (0 <= cm[ra]);
endmodule
