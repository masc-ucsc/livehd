/*
:lec_top: cdc
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
A toggle-pulse clock-domain crossing. `_1` stores src_level complemented under
the same register name and folds its synchronous reset into the next-state
logic. The after_reset prologue starts before reset has reached src_level, and
a dst_clk edge in the prologue can capture its arbitrary power-on value into
the synchronizer. That pre-reset value of the reset-bearing flop must be
tracked as unknown on the reference, not shared with the complemented impl,
or the prologue manufactures a counterexample the hardware cannot have.
*/
module cdc(input src_clk, dst_clk, src_rst, src_pulse, output dst_pulse);
reg src_level, sync0, sync1, dst_level_d;
always @(posedge src_clk) if(src_rst) src_level<=0; else if(src_pulse) src_level<=!src_level;
always @(posedge dst_clk) begin sync0<=src_level; sync1<=sync0; dst_level_d<=sync1; end
assign dst_pulse=sync1 ^ dst_level_d;
endmodule
