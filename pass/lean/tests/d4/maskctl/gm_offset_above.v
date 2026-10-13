// CONTROL: a NONZERO-OFFSET window whose high endpoint reaches ABOVE the
// source width.
//
// The 8-bit signed source is read through a window [4,12): lo = 4 is nonzero
// and hi = 12 > 8, so the sign has to reach bit 11.  The Get_mask RESULT width
// is 8, which is strictly less than 12 -- extending to the result width would
// under-extend and leave the top window bits unfilled.  The selected HIGH
// ENDPOINT, not the result width, is what the adapter must target.
module gm_offset_above(
  input  logic signed [7:0] a,
  output logic       [31:0] y
);
  assign y = (32'(a) >> 4) & 32'h000000ff;
endmodule
