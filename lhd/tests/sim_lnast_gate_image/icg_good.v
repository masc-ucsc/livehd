// The standard integrated clock gate as RTL: a latch transparent while clk is LOW, ANDed with clk.
module icg_good(input clk, input en, input [7:0] d, output [7:0] q, output [7:0] q2);
  reg l;
  always_latch if (!clk) l = en;
  wire g = l & clk;
  reg [7:0] r;
  always @(posedge g) r <= d;
  assign q = r;
  reg [7:0] r2;
  always @(posedge clk) r2 <= d;
  assign q2 = r2;
endmodule
