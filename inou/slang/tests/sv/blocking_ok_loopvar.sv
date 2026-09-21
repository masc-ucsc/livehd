// :test: roundtrip
// :top: ok_loopvar
module ok_loopvar(input clk, input [3:0] d, output reg [3:0] q, output reg [3:0] c);
  integer n;
  always @(posedge clk)
    for (n = 0; n < 4; n = n + 1) q[n] <= d[n];
  always @(*) begin
    c = 0;
    for (n = 0; n < 4; n = n + 1) c = c + d[n];
  end
endmodule
