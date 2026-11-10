module mem_bytes(input clk, input [3:0] we, input [3:0] a, input [31:0] d, input [3:0] ra, output [31:0] q);
  reg [31:0] mem [0:15];
  integer i;
  always @(posedge clk) begin
    for (i = 0; i < 4; i = i + 1) begin
      if (we[i]) mem[a][i*8 +: 8] <= d[i*8 +: 8];
    end
  end
  assign q = mem[ra];
endmodule
