// CONTROL: the UNSIGNED twin of gm_offset_above.v.
//
// Byte-for-byte the same module except that `a` is UNSIGNED, so the two
// emitted certificates differ only where signedness is the reason.
//
//   * signed  : the adapter must insert a Sext to the window's HIGH ENDPOINT,
//               and the sign fills bits 8..11, leaving FIVE ones for a = 0x80.
//   * unsigned: `is_unsign` holds, the adapter must not fire, no Sext node is
//               emitted, and bits 8..11 stay zero -- for a = 0x80 exactly ONE
//               remaining 1.
module gm_unsigned_onebit(
  input  logic       [7:0] a,
  output logic      [31:0] y
);
  assign y = (32'(a) >> 4) & 32'h000000ff;
endmodule
