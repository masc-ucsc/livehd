// FIXME tracker -- a flop with TWO async rungs (async SET and async RESET).
// `always @(posedge clk, negedge S, negedge R)` needs two async controls, but the
// reader has a SINGLE async-reset slot per flop: it stamps `reset_pin_refname`
// once per rung and upass.attributes is add-only, so the second rung collides with
// "attribute `reset_pin_refname` of `Q` is already set to a different value".
// There is no Pyrope spelling for the second rung either, so the .prp below models
// only the async SET (R becomes synchronous) -- which is why the forward direction
// refutes too. ALL THREE directions are fixme until the cell grows a second slot.
module dual_async_set_reset(
  input  clk,
  input  D,
  input  S,
  input  R,
  output reg Q
);
  always @(posedge clk, negedge S, negedge R) begin
    if (!S)
      Q <= 1'b1;
    else if (!R)
      Q <= 1'b0;
    else
      Q <= D;
  end
endmodule
