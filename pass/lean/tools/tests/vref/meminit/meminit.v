// A 4-word memory initialized by an initial for loop with nonzero values, read combinationally.
module meminit(input clk, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra,
               output [7:0] rd, output [31:0] all);
  reg [7:0] m [0:3];
  integer k;
  initial for (k = 0; k < 4; k = k + 1) m[k] = 8'h11 * (k + 1);
  always @(posedge clk) if (we) m[wa] <= wd;
  assign rd = m[ra];
  assign all = {m[3], m[2], m[1], m[0]};
endmodule
