// Golden for spec_memory_inferred_from_init: 8x3 memory, entry k resets to k.
module spec_memory_inferred_from_init(input clk, input rst, input we, input [2:0] a, input [2:0] d,
                                      input [2:0] ra, output [2:0] q);
  reg [2:0] mem [0:7];
  integer k;
  initial for (k = 0; k < 8; k = k + 1) mem[k] = k;
  always @(posedge clk) begin
    if (rst) begin
      for (k = 0; k < 8; k = k + 1) mem[k] <= k;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
