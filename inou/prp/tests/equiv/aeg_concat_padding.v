module top(input [5:0] x, y, input a, b, output [7:0] o);
  wire [7:0] ext = {2'b00, x};
  wire [7:0] packed_x = {ext[7:4], ext[3:1], ext[0]};
  wire [7:0] combined = packed_x | y | {b, 7'b0};
  assign o = {combined[7], combined[6:0] | {a, 6'b0}};
endmodule
