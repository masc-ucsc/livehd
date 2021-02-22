module trivial2a(input a, input [1:0]b, output c, output d, output [2:0] e, output signed [4:0] f);
assign c = ~a;
assign d = !b;
assign e = !b;
assign f = {~b,a};
endmodule

