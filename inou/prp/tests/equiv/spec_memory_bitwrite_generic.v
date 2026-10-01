// State names are the inlined instance names (`ba.r`, `bb.r`) so prp-statematch pairs them BY NAME.
// Golden for spec_memory_bitwrite_generic: per-bit writes into a 4x8 and a 2x4
// memory, parallel synchronous reset to 0; reads before the write (committed).
module spec_memory_bitwrite_generic(input clk, input rst, input we, input [1:0] ia, input [2:0] ja,
                                    input ib, input [1:0] jb, input x, input [1:0] ra, input rb,
                                    output [7:0] qa, output [3:0] qb);
  reg [7:0] \ba.r  [0:3];
  reg [3:0] \bb.r  [0:1];
  integer k;
  always @(posedge clk) begin
    if (rst) begin
      for (k = 0; k < 4; k = k + 1) \ba.r [k] <= 8'd0;
      for (k = 0; k < 2; k = k + 1) \bb.r [k] <= 4'd0;
    end else if (we) begin
      \ba.r [ia][ja] <= x;
      \bb.r [ib][jb] <= x;
    end
  end
  assign qa = \ba.r [ra];
  assign qb = \bb.r [rb];
endmodule
