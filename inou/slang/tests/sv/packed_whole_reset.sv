// :test: roundtrip
// :lec_timeout: 30
module packed_whole_reset(input clk, reset, input index, input [1:0] d, output [1:0] out);
logic [1:0][1:0] q, next;
always_comb begin next=q; next[index]=d; end
always_ff @(posedge clk) if(reset) q <= {2'b10,2'b01}; else q <= next;
assign out = q[index];
endmodule
