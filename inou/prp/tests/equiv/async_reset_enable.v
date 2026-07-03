// Golden for async_reset_enable: the module reset is edge-triggered in the
// sensitivity list but guarded by a COMPOUND condition (`reset || arst`), so
// no async-reset rung can be extracted — this exercises the slang reader's
// async-reset-as-sync demotion (state is only observed after clock updates,
// so an edge-triggered reset is indistinguishable from a synchronous one).
// `arst` is a plain synchronous clear input.
module \async_reset_enable.pipe (
  input        clock,
  input        reset,
  input        en,
  input        arst,
  input  [7:0] d,
  output [7:0] q
);
  reg [7:0] r;
  assign q = r;

  always @(posedge clock or posedge reset) begin
    if (reset || arst)
      r <= 8'd0;
    else if (en)
      r <= d;
  end
endmodule
