module async_reset(input clk, input rst_n, input en, input [7:0] d, output [7:0] q, output [7:0] m);
  reg [7:0] r = 8'h2a;
  reg [7:0] t;
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) r <= 8'h2a;
    else if (en) r <= r + d;
  end
  always @(posedge clk) t <= r ^ d;
  assign q = r;
  assign m = t;
endmodule
