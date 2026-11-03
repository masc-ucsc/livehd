// todo/livehd/2f-latch — transparent-LOW D latch (gate active low), the
// polarity twin of latch.v. LIVE (promoted out of `fixme_nlatch.v` at M1,
// 2026-07-20). See latch.v's header for the full account of the two bugs that
// had to fall first: the fixture's own non-LRM procedural write to a net (M0)
// and cgen's undeclared inlined enable (M1).
//
// This twin carries the extra weight. Transparent-LOW is the polarity the
// lgyosys bounded miter actually DISCRIMINATES — a port-matched high-vs-low
// flip genuinely REFUTES with a counterexample (pinned by
// lhd/tests/lec_latch_polarity_test.sh) — so its round-trip LEC is a real
// polarity check rather than a vacuous one. It is also where cgen's `posclk`
// handling is exercised: the reader connects a const-0 posclk for active-low
// enable, and process_latch turns that back into the `!` in the emission. A
// double negation anywhere on that path shows up here and nowhere else in the
// yosys ladder.
module nlatch(input d, input c, output logic q);

always_latch begin
  if(c == 0) begin
    q <= d;
  end
end
endmodule
