// Golden for spec_memory_bitwrite_runtime: a per-bit write into a 4x8 memory,
// parallel synchronous reset to 0; the read (before the write) is committed.
module spec_memory_bitwrite_runtime(input clk, input rst, input we, input [1:0] i, input [2:0] j,
                                    input x, input [1:0] ra, output [7:0] q);
  reg [7:0] m [0:3];
  integer k;
  always @(posedge clk) begin
    if (rst) begin
      for (k = 0; k < 4; k = k + 1) m[k] <= 8'd0;
    end else if (we) begin
      m[i][j] <= x;
    end
  end
  assign q = m[ra];
endmodule
