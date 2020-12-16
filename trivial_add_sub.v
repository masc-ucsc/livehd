module trivial(input signed [3:0] a, input signed [3:0] b, output signed [3:0] c);
assign c = (a + 4'sh7) - (b - 4'sh7); // 0b0111
endmodule

