/* verilator lint_off WIDTH */
// Golden for sra_const_range: an exact-const 11-bit reg tested one bit at a
// time through the `(k >>> shamt) & 1` idiom cgen emits for packed-field
// control bits. `(2047 >>> 10) & 1` folds to 1, so io_q === io_en; the point
// of the pair is that the .v COMPILES (the bitwidth pass must not stamp the
// shift result with an unshifted lower bound and then error on the folded
// value) and LEC-proves against the passthrough Pyrope.
module \sra_const_range.top (
   input signed clock
  ,input signed reset
  ,input signed io_en
  ,output reg signed io_q
);
reg [10:0] k;
reg [1:0] bitsel;
always_comb begin
  k = 12'sh7ff;
  bitsel = (((k >>> (5'sha)) & (2'sh1)) == ('sb0)) == ('sb0);
  io_q = bitsel & io_en;
end
endmodule
