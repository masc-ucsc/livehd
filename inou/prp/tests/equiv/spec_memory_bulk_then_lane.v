// Golden for spec_memory_bulk_then_lane: nonblocking writes in program order.
module spec_memory_bulk_then_lane(input clk, input wr, input clr, input we, input [1:0] a,
                                  input [7:0] x, input [1:0] ra, output [15:0] q);
  reg [15:0] mem [0:3];
  integer k;
  always @(posedge clk) begin
    if (wr) mem[a] <= 16'hffff;
    if (clr) for (k = 0; k < 4; k = k + 1) mem[k] <= 16'h0;
    if (we) mem[a][7:0] <= x;
  end
  assign q = mem[ra];
endmodule
