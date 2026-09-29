// Golden for the `concat(...)` builtin's RTL lowering — written from the
// specification in docs/pyrope/10-internals.md, not from the emitter's output.
//
// A concat is MSB-first (`{a,b,c}`, argument 0 in the high bits) and every lane
// occupies exactly its DECLARED width. Verilog states both by construction: a
// concatenation operand is SELF-DETERMINED, so it contributes its own width and
// nothing else — which is why `\p.lo `, though signed, sits in three bits here
// and does not sign-extend into the field above it, while the very same
// `\p.lo ` assigned to `sx` does.
//
// The bundle input `p:(hi:u4, lo:s3)` flattens to the dotted leaf ports
// `\p.hi ` / `\p.lo `.
module \concat_bundle_lanes.top (
   input         [3:0]  \p.hi
  ,input  signed [2:0]  \p.lo
  ,input         [1:0]  c
  ,output        [8:0]  zu
  ,output signed [8:0]  zs
  ,output        [11:0] wu
  ,output signed [11:0] ws
  ,output        [8:0]  nest
  ,output        [5:0]  kz
  ,output signed [8:0]  sx
);
  // 4 + 3 + 2 = 9 bits: p.hi in [8:5], p.lo in [4:2], c in [1:0].
  wire [8:0] packed_bits = {\p.hi , \p.lo , c};

  assign zu   = packed_bits;
  assign zs   = packed_bits;  // same bits; `s9` vs `u9` is only how it reads back

  // A concat result is non-negative by construction, so widening EITHER
  // destination pads with zeros — the signed one included.
  assign wu   = {3'b000, packed_bits};
  assign ws   = {3'b000, packed_bits};

  // Nesting: the inner concat is 4+3 = 7 bits, so this is the same 9-bit layout.
  assign nest = {{\p.hi , \p.lo }, c};

  // `k:u4 = 1` — the declared four bits, not the one bit the value needs.
  assign kz   = {4'b0001, c};

  // The signed field used as a VALUE: sign-extended to nine bits. Contrast
  // bits [4:2] of zu, where the same field is a three-bit window.
  assign sx   = {{6{\p.lo [2]}}, \p.lo };
endmodule
