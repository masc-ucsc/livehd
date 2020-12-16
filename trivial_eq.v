module trivial(input signed [3:0] a, input signed [3:0] b, output signed [3:0] c);
//assign c = (a | b) & 4'sh3; // 0b0011
assign c = a == 4'sh7; // 0b0111
endmodule

