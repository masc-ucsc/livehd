/*
`p` also depends on the input, while the reference's `p` is zero after the
first clock.
:lec_expect: refuted
*/
module top(input clk, rst, d, output [7:0] o, output p, output ov);
reg [7:0] cnt;
reg r, s, u, v;
always @(posedge clk) begin
  cnt <= cnt + 8'd1;
  r   <= r ^ d;
  s   <= r;
  u   <= r;
end
always @(posedge clk) if (rst) v <= 0; else v <= d;
assign o  = cnt;
assign p  = s ^ u ^ d;
assign ov = v;
endmodule
