// todo/livehd/2f-latch — transparent-HIGH D latch, yosys-reader round-trip.
// LIVE (promoted out of `fixme_latch.v` at M1, 2026-07-20). Two independent
// bugs had to fall before this eight-line module could round-trip; both are
// worth remembering, because each masqueraded as the other kind of problem.
//
// 1. It was never latch debt (fixed at M0). The fixture declared `output q` —
//    a NET — and then wrote it procedurally inside `always_latch`, which is
//    illegal per IEEE 1800. iverilog says "'q' is not a valid l-value for a
//    procedural assignment"; our own slang reader says "cannot assign to a net
//    within a procedural context". Both were RIGHT: the fixture was wrong, not
//    LiveHD.  Fixed to `output logic q`.
//
// 2. cgen emitted an UNDECLARED identifier (fixed at M1). `process_latch` read
//    din/enable through `get_wire_or_const`, which ignores pin2expr — so an
//    INLINED single-fanout enable driver came out as a bare name declared
//    nowhere:
//
//        always @* begin
//          if (eq_20) q = d;      // <-- eq_20 exists in no declaration
//        end
//
//    iverilog rejects it outright. yosys, more dangerously, invents an implicit
//    wire (reading X) and lgcheck then REFUTES the round-trip — a real bug
//    reported as a "not equivalent" verdict. This module is the reproducer: its
//    enable has fanout 1, which is exactly what the six live prp-equiv-latch_*
//    pairs happen to avoid (their enable muxes have fanout >= 2), so nothing
//    else in the suite caught it. cgen now emits the enable INLINE via
//    get_expression, like the flop path always did.
//
// The emission is also `always_latch` + nonblocking `<=` now, not `always @*` +
// blocking `=`: yosys re-infers a latch from either, but our own slang reader
// classifies a non-edge `always` with blocking writes as plain COMBINATIONAL
// logic, so the old form could not round-trip through our front end at all.
module latch(input d, input c, output logic q);

always_latch begin
  if(c == 1) begin
    q <= d;
  end
end
endmodule
