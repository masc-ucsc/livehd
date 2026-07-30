// Golden for a runtime bit-range select `a#[base ..= base+len]` with a NARROW low
// endpoint (`base` is 1 bit).  The slice is `len+1` bits starting at `base`, so
// the reference is `(a >> base) & ((1 << (len+1)) - 1)` -- identical in form to
// bitrange_dyn.v, and deliberately so: the two differ only in the endpoint
// WIDTHS, which is what decides whether the lowered shift amount survives
// Verilog's self-determined sizing (see the .prp header).
//
// `len + 1` uses unsized `1` (32-bit) so the +1 cannot wrap the 3-bit `len` on
// the golden side either.  Because the high endpoint is `base+len`, `hi >= lo`
// holds for every input.
module \bitrange_dyn_narrow.rsel_n (
  input  [15:0] a,
  input         base,
  input  [2:0]  len,
  output [15:0] z
);
  assign z = (a >> base) & ((16'd1 << (len + 1)) - 16'd1);
endmodule
