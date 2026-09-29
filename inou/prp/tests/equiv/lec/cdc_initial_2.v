/*
Negative control: `_1` with the output inverted.
:lec_expect: refuted
*/
module cdc(input src_clk, dst_clk, src_rst, src_pulse, output dst_pulse);
reg src_level, sync0, sync1, dst_level_d;
always @(posedge src_clk) src_level<=src_rst | (src_pulse ~^ ~src_level);
always @(posedge dst_clk) begin sync0<=~src_level; sync1<=sync0; dst_level_d<=sync1; end
assign dst_pulse=~(sync1 ^ dst_level_d);
endmodule
