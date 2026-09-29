/*
:lec_top: top
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
An unrelated reset input must not turn unreset state into a don't-care. Only
`v` has a reset; the prologue never writes `cnt`, `r`, `s` or `u` with known
data. In a single-clock design equal names keep sharing one power-on value on
both sides, exactly as without a reset input. Marking every reference bit
unknown would mask `o` and `p` for the whole run: `_1` (count by two) would
prove although no initial value makes the two sequences agree, and `_2` would
prove because an uncorrelated per-bit unknown plane cannot see that the two
copies `s` and `u` of `r` are equal, so `s ^ u` is zero after one clock.
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
assign p  = s ^ u;
assign ov = v;
endmodule
