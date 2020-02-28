module mulDiv( input [3:0] a, input [3:0] b, output [3:0] c, output [3:0] d, output [3:0] e, output [3:0] f, output [3:0] g, output [3:0] h);
assign c = a + b;
assign d = a - b;
assign e = a * b;
assign f = a / b;
assign g = a / 4'b0111;
assign h = a % 4'b0111;
endmodule

