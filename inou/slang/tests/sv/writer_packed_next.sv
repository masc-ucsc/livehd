// :test: roundtrip
// :lec_timeout: 30
module writer_packed_next(input clk, rst_n, en, input [1:0] data, output [1:0] out);
logic [1:0][1:0] q;
logic [3:0] d;
always_comb begin
  d = q;
  if (en) d[2] = data[0];
  else d[2] = data[1];
end
always_ff @(posedge clk or negedge rst_n)
  if (!rst_n) q <= '0;
  else q <= d;
assign out = q[1];
endmodule
