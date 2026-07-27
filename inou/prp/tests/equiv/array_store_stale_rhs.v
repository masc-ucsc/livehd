// Semantic golden for array_store_stale_rhs.prp: with m = d, arr = {d, 0},
// so o = sel ? 0 : d. Deliberately array-free (a plain mux) so yosys
// read_verilog observes the intended value exactly and the golden's own v2v
// round trip is not itself subject to the comb-array store bug being tracked.
module array_store_stale_rhs(input logic sel, input logic d, output logic o);
  assign o = sel ? 1'b0 : d;
endmodule
