// A 4-word memory cleared by an asynchronous active-low reset (a for loop in the reset branch).
module memrst(input clk, input rst_n, input we, input [1:0] wa, input [7:0] wd, input [1:0] ra,
              output [7:0] rd, output [31:0] all);
  reg [7:0] m [0:3];
  integer k;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin for (k = 0; k < 4; k = k + 1) m[k] <= 8'h00; end
    else if (we) m[wa] <= wd;
  assign rd = m[ra];
  assign all = {m[3], m[2], m[1], m[0]};
endmodule
