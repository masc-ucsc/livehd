module trivial(input [3:0] a, input [3:0] b, output [3:0] c);
assign c = ~(a & 4'hF); // 0b1111
endmodule

