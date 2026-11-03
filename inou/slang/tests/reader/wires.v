

module wires(input [7:0] a, output [7:0] b, output [3:0] a_out);

assign b = {4'b1001, a[6:3]};
assign a_out = a[4:1];

endmodule
