// Golden for spec_memory_inferred_size: a 4x8 memory, parallel reset to 0 through
// the minted `reset`.
module spec_memory_inferred_size(input clk, input reset, input we, input [1:0] wa, input [7:0] d,
                                 input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  initial for (k = 0; k < 4; k = k + 1) mem[k] = 8'd0;
  always @(posedge clk) begin
    if (reset) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd0;
    end else if (we) begin
      mem[wa] <= d;
    end
  end
  assign q = mem[ra];
endmodule
