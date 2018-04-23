

module aoi12(a, b, c, y);
	input a, b, c;
	output y;
	assign y = ~((a & b) | c);
endmodule

module arraycells(a, b, c, y);
	input a;
	input [31:0] b, c;
	output [31:0] y;

	aoi12 p [31:0] (a, b, c, y);
endmodule
