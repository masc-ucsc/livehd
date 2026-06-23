// Golden for single_bit_arith.  The .prp extracts bit 31 with the shift/mask
// `(io_instruction >> 31) & 1` and OR-shifts it (`(b | (b<<1)) << 32`); this
// golden codes the SAME value with a DIRECT bit-select `io_instruction[31]` and
// a 2-bit replication, a different LNAST lowering that does not share the
// shift/mask datapath.
//
//   b   = io_instruction[31]            (an unsigned 0/1)
//   b | (b<<1)                          = {b,b}  (b ? 2'b11 : 2'b00)
//   ( … ) << 32                         -> bits [33:32]
//   gated on opcode (io_instruction[6:0]) == 55, else 0 (the empty `if`s in the
//   .prp leave _mux_10 unchanged, so only the ==55 arm contributes).
module \single_bit_arith.sb (
   input  [63:0] io_instruction
  ,output [63:0] io_sextImm
);
  wire sign = io_instruction[31];
  assign io_sextImm = (io_instruction[6:0] == 7'd55)
                      ? {30'd0, sign, sign, 32'd0}   // {2{sign}} << 32
                      : 64'd0;
endmodule
