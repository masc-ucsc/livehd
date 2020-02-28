module comparison( input [3:0] a, input [3:0] b, output c, output d, output e);
assign c = a > b;
assign d = a <= b;
assign e = a >= 4'b0111;
endmodule

