// State names are the inlined instance names (`r1.mem`, `r2.mem`) so prp-statematch pairs them BY NAME.
// Golden for spec_memory_generic_ram: a 16x8 and an 8x4 memory, parallel
// synchronous reset to 0, reads of the committed word (read before write).
module spec_memory_generic_ram(input clk, input rst, input [3:0] a16, input [7:0] d8, input we1,
                               input [2:0] a8, input [3:0] d4, input we2,
                               output [7:0] q8, output [3:0] q4);
  reg [7:0] \r1.mem  [0:15];
  reg [3:0] \r2.mem  [0:7];
  integer i;
  always @(posedge clk) begin
    if (rst) begin
      for (i = 0; i < 16; i = i + 1) \r1.mem [i] <= 8'd0;
      for (i = 0; i < 8; i = i + 1)  \r2.mem [i] <= 4'd0;
    end else begin
      if (we1) \r1.mem [a16] <= d8;
      if (we2) \r2.mem [a8]  <= d4;
    end
  end
  assign q8 = \r1.mem [a16];
  assign q4 = \r2.mem [a8];
endmodule
