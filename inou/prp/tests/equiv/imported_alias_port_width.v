// Golden for imported_alias_port_width.prp: Byte = u8, Nib = s4.
module top (
  input         [7:0] a,
  input         [7:0] b,
  input  signed [3:0] c,
  output        [7:0] y,
  output signed [3:0] m,
  output signed [3:0] k
);
  assign y = a ^ b;
  assign m = c;
  assign k = c;
endmodule
