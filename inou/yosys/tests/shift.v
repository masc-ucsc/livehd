
module shift (input [7:0] a, output [7:0] b, output [7:0] c, output [7:0] d);

assign b = a >> 1;
assign c = a << 1;
assign d = a >>> 1;

endmodule
