module shift( input [3:0] a, output [3:0] c, output [3:0] d);
assign c = a << 2'b10;
assign d = a >> 2'b01;
assign d = a >>> 2'b11;
endmodule

