// A memory written from two clock domains: port 1 on clk writes even words, port 2 on clk2 writes
// odd words (so two ports never write one word in one step, which Verilog leaves to scheduling
// order). The clocks fire apart, so the certificate's commit table is used for every subset.
module twoclkmem (
  input clk, input clk2,
  input we, input [2:0] wa, input [15:0] wd,
  input we2, input [2:0] wa2, input [15:0] wd2,
  input [2:0] ra,
  output [15:0] rd, output [127:0] all, output [7:0] cnt, output [7:0] cnt2
);
  reg [15:0] m [0:7];
  integer i;
  initial for (i = 0; i < 8; i = i + 1) m[i] = 16'h1111 * i;
  wire [2:0] a1 = {wa[2:1], 1'b0};
  wire [2:0] a2 = {wa2[2:1], 1'b1};
  always @(posedge clk)  if (we)  m[a1] <= wd;
  always @(posedge clk2) if (we2) m[a2] <= wd2;
  assign rd  = m[ra];
  assign all = {m[7], m[6], m[5], m[4], m[3], m[2], m[1], m[0]};
  // one counter per domain shows which clocks fired
  reg [7:0] c1 = 0, c2 = 0;
  always @(posedge clk)  c1 <= c1 + 8'd1;
  always @(posedge clk2) c2 <= c2 + 8'd1;
  assign cnt = c1;
  assign cnt2 = c2;
endmodule
