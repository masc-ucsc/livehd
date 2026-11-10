module mem_reset(input clk, input rst, input we, input [3:0] wa, input [3:0] ra, input [7:0] wd, output [7:0] rd, output [7:0] byp);
  reg [7:0] mem [0:15];
  integer i;
  always @(posedge clk or posedge rst) begin
    if (rst) begin
      for (i = 0; i < 16; i = i + 1) mem[i] <= 8'd0;
    end else if (we) mem[wa] <= wd;
  end
  assign rd  = mem[ra];
  assign byp = (we && wa == ra) ? wd : mem[ra];
endmodule
