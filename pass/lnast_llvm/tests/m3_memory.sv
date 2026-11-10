module m3_memory(input clk, input we0, input we1, input [4:0] wa0, input [4:0] wa1, input [3:0] ra0, input [3:0] ra1,
              input [7:0] wd0, input [7:0] wd1, output [7:0] rd0, output [7:0] rd1, output [7:0] rq);
  reg [7:0] mem [0:15];
  reg [7:0] r;
  always @(posedge clk) begin
    if (we0 && wa0 < 16) mem[wa0[3:0]] <= wd0;
    if (we1) mem[wa1[3:0]] <= wd1 ^ rd0;
    r <= mem[ra1] + 1;
  end
  assign rd0 = mem[ra0];
  assign rd1 = mem[ra1] & {8{we0}};
  assign rq = r;
endmodule
