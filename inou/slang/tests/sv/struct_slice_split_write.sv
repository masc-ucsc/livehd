// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// A slice write that covers several fields of a per-field packed-struct variable (here an instance output on
// `e[0+:9]`: the eight one-bit flags and bit 0 of `ptag`) is split per overlapped field. Each field must receive
// only its own bits of the slice. The split used to hand the one-bit flags the whole shifted remainder
// (`bit-range-overflow`: "9-bit value does not fit the 1-bit destination slice", bp_processor's bp_tlb).
// :test: lec_no_x
typedef struct packed {
  logic [27:0] ptag;
  logic        g, m, a, d, u, x, w, r;
} leaf_s;

module slice_src (
    input  [8:0] i,
    output [8:0] o
);
  assign o = ~i;
endmodule

module struct_slice_split_write (
    input  [8:0]  lo_i,
    input  [26:0] hi_i,
    output [35:0] o
);
  leaf_s e;
  slice_src s_lo (
      .i(lo_i),
      .o(e[0+:9])
  );
  assign e[9+:27] = hi_i;
  assign o        = e;
endmodule
