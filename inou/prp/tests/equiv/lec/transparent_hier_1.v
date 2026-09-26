/*
The `__flat___` levels (outer and inner, around the ordinary `foo`) drop out of
the logical name, so leaf.q pairs with the golden's `foo.q` BY NAME and semdiff
matches the def with no solver call. The verdict alone would not pin that: with
a non-transparent instance name the solver's tier-2 state pairing still proves
it, so the grep is the test.
:lec_grep: 'top' MATCHED \(semdiff
:lec_grep: PROVEN equivalent
*/
module leaf(input clk, rst, d, output reg q);
always @(posedge clk) if (rst) q <= 0; else q <= d;
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
