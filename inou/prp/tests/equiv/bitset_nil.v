// Golden for bitset_nil. Both accumulators cover every bit, so the `nil` base
// is fully overwritten and the values are concrete. `s`'s 4-bit signed source
// is sign-extended into its 8-bit slot before assembly.
module \bitset_nil.foo (
   input  [31:0] i,
   output signed [63:0] io_sextImm,
   output [12:0] io_direct,
   output signed [63:0] s_sext
);
  wire signed [12:0] b = i[0] | (i[11:8] << 1) | (i[30:25] << 5) | (i[7] << 11) | (i[31] << 12);
  assign io_sextImm = b;           // sext 13 -> 64
  assign io_direct  = b[12:0];

  wire signed [3:0] hi4  = i[31:28];      // 4-bit signed source slice
  wire signed [7:0] slot = hi4;           // sign-extend into the 8-bit slot
  wire signed [12:0] s   = {slot, i[4:0]};// bits 12:5 = slot, 4:0 = low run
  assign s_sext = s;               // sext 13 -> 64
endmodule
