// Regression: prp_writer emitting a self-assignment `o = o`.
//
// `n` is a combinational alias of `o`, so the register update `o <= n` folds to
// `o <= o` (a hold). The writer's self-store fold only compared RAW LNAST names
// (`o` vs `n___ssa_1`) and missed it, because `n___ssa_1 = o` is a single-use
// temp INLINED at this very store -- what reached the file was `o = o`, which
// Pyrope rejects on re-parse ("irrelevant assignment"), killing the v->prp->v
// round trip. Two registers are needed: with one, the if-merge renders as an
// if-EXPRESSION (`o = if rst { 0 } else { o }`, legal) instead of a statement.
module reg_hold_self_store(input clk, input rst, input [1:0] d,
                           output reg [1:0] o, output reg [1:0] p);
  reg [1:0] n;
  always @(*) n = o;
  always @(posedge clk) begin
    if (rst) begin o <= 0; p <= 0; end
    else begin o <= n; p <= d; end
  end
endmodule
