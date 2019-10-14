
module gates(input a, input b, input [3:0] c, input [3:0] d, output [3:0] e,
output f, output g);

assign f = ~(a^b);
assign g = ~^c;
/* assign e = ~(c^d); */

endmodule


