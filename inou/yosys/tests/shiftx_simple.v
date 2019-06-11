
module shiftx_simple(input [7:0] a, input [7:0] b, output [7:0] c);

//assign c = a[$signed(b) +: 8];
assign c = a[b +: 8];

endmodule

