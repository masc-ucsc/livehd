// Golden for bitset_imm: build the 13-bit immediate with shifts + OR, then
// extract three ways. `b` is a signed 13-bit wire so reading it sign-extends
// (io_sextImm); the explicit zero-pad / part-select cover the unsigned reads.
module \bitset_imm.foo (
   input  [31:0] i,
   output signed [63:0] io_sextImm,
   output [63:0] io_zext,
   output [12:0] io_direct
);
  wire signed [12:0] b = (i[11:8] << 1) | (i[30:25] << 5) | (i[7] << 11) | (i[31] << 12);
  assign io_sextImm = b;                 // signed 13 -> signed 64 sign-extends
  assign io_zext    = {51'b0, b[12:0]};  // unsigned zero-extend
  assign io_direct  = b[12:0];           // bare 13-bit value
endmodule
