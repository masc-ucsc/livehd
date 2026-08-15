module array_bitview_runtime (
  input  wire [1:0]  idx,
  input  wire [3:0]  off,
  input  wire [7:0]  v,
  output wire [7:0]  before_o,
  output wire [31:0] after_o,
  output wire [7:0]  pick_o
);
  wire [31:0] initial_value = 32'h44332211;
  wire [31:0] shifted_mask  = 32'h000000ff << off;
  wire [31:0] updated =
      (initial_value & ~shifted_mask) |
      (({24'b0, v} << off) & shifted_mask);

  assign before_o = initial_value >> off;
  assign after_o  = updated;
  assign pick_o   = updated >> (idx * 8);
endmodule
