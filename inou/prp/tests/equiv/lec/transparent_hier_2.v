/*
Negative control: the same transparent hierarchy with a changed transition.
:lec_expect: refuted
:lec_set: formal.engine=bmc formal.bound=2
*/
module leaf(input clk, rst, d, output reg q);
always @(posedge clk) if (rst) q <= 0; else q <= ~d;
endmodule
module middle(input clk, rst, d, output q);
leaf __flat___inner(.clk(clk), .rst(rst), .d(d), .q(q));
endmodule
module outer(input clk, rst, d, output q);
middle foo(.clk(clk), .rst(rst), .d(d), .q(q));
endmodule
module top(input clk, rst, d, output q);
outer __flat___outer(.clk(clk), .rst(rst), .d(d), .q(q));
endmodule
