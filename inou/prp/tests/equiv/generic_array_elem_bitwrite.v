// Golden for generic_array_elem_bitwrite: the arrays are packed buses (entry k
// at bits [k*W +: W]); each write clears its window and ORs the value in.
module generic_array_elem_bitwrite (
  input  [1:0]  i,
  input  [1:0]  j,
  input         x,
  input         k,
  input  [7:0]  d,
  input  [31:0] v,
  input  [15:0] w,
  output [31:0] o8,
  output [15:0] o4
);
  wire [5:0]  b8 = {i, 3'b000} + j;
  wire [5:0]  l8 = {i ^ 2'd1, 3'b000} + {k, 2'b00};
  wire [31:0] t8 = (v & ~(32'h1 << b8)) | ({31'b0, x} << b8);
  assign o8 = (t8 & ~(32'hf << l8)) | ({28'b0, d[3:0]} << l8);
  wire [4:0]  b4 = {i, 2'b00} + j;
  wire [4:0]  l4 = {i ^ 2'd1, 2'b00};
  wire [15:0] t4 = (w & ~(16'h1 << b4)) | ({15'b0, x} << b4);
  assign o4 = (t4 & ~(16'hf << l4)) | ({12'b0, d[3:0]} << l4);
endmodule
