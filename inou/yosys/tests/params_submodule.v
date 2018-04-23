
module add(a,b,c);
parameter WIDTH = 32;

input [WIDTH-1:0] a, b;
output [WIDTH-1:0] c;
assign c = a + b;

endmodule

module params_submodule(a, b, c);

input [64-1:0] a, b;
output [64-1:0] c;

add #(.WIDTH(64)) adder (.a(a), .b(b), .c(c));

endmodule
