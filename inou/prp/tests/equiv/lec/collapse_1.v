/*
:lec_expect: refuted
*/
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
module top(input [7:0] p, input [7:0] q, output [7:0] o); leaf u(.a(p), .b(q), .y(o)); endmodule
