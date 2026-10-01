// Golden for spec_memory_unused_bits: the declared 16-bit entries, only the low
// nibble ever written or read (same shape as the source so LEC pairs the
// unreset memory state; the narrowing is not observable at the ports).
module spec_memory_unused_bits(input clk, input we, input [1:0] a, input [3:0] x,
                               input [1:0] ra, output [3:0] q);
  reg [15:0] m [0:3];
  always @(posedge clk) begin
    if (we) m[a][3:0] <= x;
  end
  assign q = m[ra][3:0];
endmodule
