// FIXME tracker -- a 2-flop synchronizer written with blocking `=`.
// Every synthesis tool infers two flops here; `--reader slang` models state from
// non-blocking `<=` only and now fail-closes (`blocking-ff-state`) rather than
// silently lowering it to stateless logic, which used to DELETE the registers.
// The refusal is correct behaviour for today's reader -- this pair tracks the
// missing FEATURE. The .prp below is the hardware the Verilog means, and it PROVES
// against the golden, so only the Verilog-reading legs are fixme.
module blocking_ff_state (
    input  clk,
    input  d,
    output q
);

  reg sync_0, sync_1;

  always @(posedge clk) sync_0 = d;

  always @(posedge clk) sync_1 = sync_0;

  assign q = sync_1;

endmodule
