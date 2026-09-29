// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// `{{N{v[msb]}}, v…}` is how firtool spells a sign extension. The reader must
// recognize it and emit LNAST's `sext` (Slang_context::lower_concat_sext), and
// must NOT fire on the look-alike shapes below.
module concat_sext(
    input  logic [31:0] a,
    input  logic [62:0] b,
    input  logic [11:0] c,
    input  logic        rdy,
    input  logic [ 3:0] d,
    output logic [63:0] o_whole,    // sext: replicated MSB of the whole lane
    output logic [63:0] o_slice,    // sext: replicated MSB of a range-select lane
    output logic [63:0] o_multi,    // sext: three lanes, MSB of the topmost one
    output logic [63:0] o_mask,     // NOT sext: an unrelated 1-bit net (mask idiom)
    output logic [63:0] o_notmsb,   // NOT sext: replicates bit 30, not the MSB
    output logic [39:0] o_other     // NOT sext: MSB of a DIFFERENT variable
);

  assign o_whole  = {{32{a[31]}}, a};
  assign o_slice  = {{32{b[31]}}, b[31:0]};
  assign o_multi  = {{51{c[11]}}, c, 1'b0};
  assign o_mask   = {{32{rdy}}, a};
  assign o_notmsb = {{32{a[30]}}, a};
  assign o_other  = {{8{d[3]}}, a};

endmodule
