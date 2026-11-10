module mem_collide(input clk, input we, input [2:0] a, input [7:0] wd, output [7:0] rd);
  reg [7:0] mem [0:7];
  always @(posedge clk) if (we) mem[a] <= wd;
  assign rd = mem[a];
endmodule
