// FIXME tracker -- an OUT-OF-RANGE part-select on an ASCENDING (big-endian)
// vector is a hard compile error where Verilog defines the result as X.
//
// `wire [0:24] x` indexes left-to-right, so declared index i sits at
// LSB-relative position (24 - i). A select whose base is 31..38 therefore
// lowers to a NEGATIVE shift, and upass.bitwidth's check_shift_amount
// (upass/bitwidth/upass_bitwidth.cpp) rejects the whole design:
//   "syntax: shift amount is always negative (range [-13, -6])".
// The IEEE rule is that a part-select entirely outside the vector yields X, not
// a diagnostic -- yosys and slang both read this file without complaint.
//
// The check itself is RIGHT for Pyrope source (a negative shift there is a real
// bug); the gap is that the slang reader hands it a negative shift instead of
// resolving the select to X first. Note the asymmetry this pair pins down: the
// IDENTICAL out-of-range select on a DESCENDING `wire [24:0]` compiles clean
// (it lowers to a positive shift that simply shifts everything out, yielding 0
// -- also not X, but at least not fatal). Only the ascending declaration dies.
//
// FIX: inou/slang's select lowering must bound the base index against the
// operand's declared range and emit an X constant (or an X-defaulted mux for a
// partial overlap) when the select cannot land inside the vector. Deliberately
// NOT attempted from the bitwidth side: relaxing check_shift_amount would blind
// it to genuine negative shifts in hand-written Pyrope.
//
// Corpus: tmp/chimera vloghammer partsel_00093 (line 44 `x11[31 + s1 +: 4]` on
// `wire [0:24] x11`), partsel_00403, partsel_00716 -- 3 of 400 sampled files.
module partsel_oob_ascending(input [24:0] a, input [2:0] s, output [3:0] y);
  wire [0:24] x;
  assign x = a;
  assign y = x[31 + s +: 4];
endmodule
