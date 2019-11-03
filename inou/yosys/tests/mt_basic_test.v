module mt_basic_test (
input  [1:0] a0, 
input  [1:0] b0, 
output [1:0] s0,
output [1:0] s1,
output [1:0] s2
);

assign s0 = a0 & b0;
assign s1 = a0 | b0;
assign s2 = a0 ^ b0;


endmodule
