// Golden for a runtime contiguous bit-range select `a#[base ..= base+len]` (the
// NATIVE Pyrope form).  The slice is `len+1` bits starting at `base`, so the
// reference is `(a >> base) & ((1 << (len+1)) - 1)`.  Because the high endpoint
// is `base+len`, `hi >= lo` holds for every input, so there is no
// descending-range corner case to reason about.  `len + 1` uses unsized `1`
// (32-bit) so the +1 does not wrap the 3-bit `len`.
module \bitrange_dyn.rsel (
  input  [15:0] a,
  input  [3:0]  base,
  input  [2:0]  len,
  output [15:0] z
);
  assign z = (a >> base) & ((16'd1 << (len + 1)) - 16'd1);
endmodule
