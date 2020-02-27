
module shiftx_simple(input [7:0] a, input [1:0] b, output [7:0] c);

assign c = a[$signed(b) +: 8];
//assign c = a[b +: 8];
//assign c = $signed(a) >>> $unsigned(b);
//assign c = a[$signed(3)+:8];

endmodule

