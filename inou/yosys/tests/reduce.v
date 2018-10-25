
module reduce(input [7:0] in, output a, output b, output c, output d);

assign a = &in;
assign b = |in;
assign c = ^in;
assign d = !in;

endmodule


