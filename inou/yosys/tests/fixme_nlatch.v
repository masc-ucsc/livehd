// todo/livehd/2f-latch — transparent-LOW D latch (gate active low), the
// polarity twin of fixme_latch.v.
//
// Same history and same real blocker as fixme_latch.v — see that file's header
// for the full account. In short: the old failure was NOT latch debt (`output q`
// is a net, procedurally assigned inside `always_latch` — illegal per IEEE 1800,
// correctly rejected by iverilog and by our slang reader); fixed to
// `output logic q`. What still blocks it is 2f-latch M1: cgen's `process_latch`
// emits the enable through `get_wire_or_const`, so an inlined single-fanout
// driver becomes a bare UNDECLARED identifier in the generated Verilog.
//
// This one carries extra weight: the transparent-LOW polarity is what the
// lgyosys bounded miter actually DISCRIMINATES (a port-matched high-vs-low flip
// genuinely REFUTES — see lhd/tests/lec_latch_polarity_test.sh), so once M1
// lands, its round-trip LEC is a real polarity check, not a vacuous one.
//
// PROMOTE to `nlatch.v` alongside fixme_latch.v when M1 lands.
module nlatch(input d, input c, output logic q);

always_latch begin
  if(c == 0) begin
    q <= d;
  end
end
endmodule
