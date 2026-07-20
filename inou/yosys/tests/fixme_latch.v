// todo/livehd/2f-latch — transparent-HIGH D latch, yosys-reader round-trip.
//
// HISTORY (2f-latch M0): this fixture used to be counted as "latch debt", but
// its actual failure had NOTHING to do with latches — it declared `output q`
// (a NET) and then assigned it procedurally inside `always_latch`, which is
// illegal per IEEE 1800 and correctly rejected by iverilog ("'q' is not a valid
// l-value for a procedural assignment") AND by our own slang reader
// ("cannot assign to a net within a procedural context"). LiveHD was right; the
// fixture was wrong. Fixed below to `output logic q`.
//
// REAL remaining blocker (2f-latch M1, verified 2026-07-20): the round-trip now
// reads and lowers cleanly, but `Cgen_verilog::process_latch` reads din/enable
// through `get_wire_or_const` instead of `get_expression`, so an INLINED
// single-fanout enable driver emits a bare UNDECLARED identifier:
//
//     always @* begin
//       if (eq_20) q = d;      // <-- eq_20 is never declared anywhere
//     end
//
// iverilog rejects that; yosys silently treats it as an implicit wire, so
// lgcheck then REFUTES the round-trip. (The six live `prp-equiv-latch_*` pairs
// dodge this only because their enable muxes have fanout >= 2.) The same
// emission is `always @*` + BLOCKING `=`, which our own slang reader classifies
// as plain comb, so it does not round-trip through our front end either.
//
// PROMOTE to `latch.v` (dropping the `fixme_` prefix, which is what excludes it
// from VERILOG_TESTS) once M1 lands both fixes.
module latch(input d, input c, output logic q);

always_latch begin
  if(c == 1) begin
    q <= d;
  end
end
endmodule
