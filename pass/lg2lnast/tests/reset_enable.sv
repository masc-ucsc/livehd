module reset_enable(input clk, input rst, input en, input [7:0] a, input signed [7:0] b, output [15:0] m, output [7:0] c, output [7:0] d, output signed [8:0] e, output [3:0] f);
  reg [7:0] cnt;
  reg [7:0] hold = 8'h5a;
  always @(posedge clk) if (rst) cnt <= 8'd3; else cnt <= cnt + 1;
  always @(posedge clk) if (en) hold <= a;
  assign m = a * a;
  assign c = ~cnt;
  assign d = (a & hold) | (cnt ^ a);
  assign e = b - $signed({1'b0, a});
  assign f = a[6:3] + a[3:0];
endmodule
