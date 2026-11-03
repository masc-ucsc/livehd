
module shiftx_simple(input [7:0] a, input [2:0] b, input [1:0] b2, output [3:0] c);

assign c = a[$signed(b) +: 4];
//assign c = a[b +: 8];
//assign c = $signed(a) >>> $unsigned(b);
//assign c = a[$signed(-2)+:8];

endmodule

