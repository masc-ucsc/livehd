module trivial_logic( input [3:0] a, input [3:0] b, output [3:0] c, output [3:0] d, output [3:0] e);
assign c = a & b;
assign d = ~a;
assign e = 4'b1111 ^ a;
endmodule

