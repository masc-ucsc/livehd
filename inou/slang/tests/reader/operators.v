
module operators (input [7:0] a, input [7:0] b,
output c, output d, output e, output f, output g, output h);

assign c = a == b;
assign d = !(a == b);
assign e = a > b;
assign f = a >= b;
assign g = a < b;
assign h = a <= b;

endmodule

