

module wires(input [7:0] a, output [7:0] b);

assign b = {4'b1001, a[6:3]};

endmodule
