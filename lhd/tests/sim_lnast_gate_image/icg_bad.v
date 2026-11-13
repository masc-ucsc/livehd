// NOT a clean clock gate: the latch is transparent while clk is HIGH, so `l & clk` can glitch.
module icg_bad(input clk, input en, input [7:0] d, output [7:0] q, output [7:0] q2);
  reg l;
  always_latch if (clk) l = en;
  wire g = l & clk;
  reg [7:0] r;
  always @(posedge g) r <= d;
  assign q = r;
  reg [7:0] r2;
  always @(posedge clk) r2 <= d;
  assign q2 = r2;
endmodule
