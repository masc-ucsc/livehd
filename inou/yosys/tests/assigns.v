
module assigns(input a, output b);

wire [1:0] c;
assign b = c[0];
assign c[0] = a;
assign c[1] = a;

endmodule

