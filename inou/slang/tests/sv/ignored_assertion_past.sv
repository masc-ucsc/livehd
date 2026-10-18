// A concurrent assertion that uses `$past`, compiled with --ignore-assertions. Ignoring an
// assertion drops it entirely, so its `$past` must not mint a history register and a
// shift-chain update: those flops feed nothing but the dropped property, and on nets
// that an instance output reaches the chain update used to fail the compile
// ("`idx_o__past1` is assigned to itself", "may not fit its declared range").
// Checked by tests/ignored_assertion_past_test.sh, not by the LEC ladder (the
// property is temporal, which the reader refuses unless assertions are ignored).
module ignored_assertion_past (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [1:0] a,
    input  logic [2:0] b,
    output logic [2:0] y,
    output logic [1:0] idx_o
);
  logic [2:0] q;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= '0;
    else        q <= b;
  end
  assign y     = q;
  assign idx_o = a;

  lock: assert property (@(posedge clk) a == 2'd1 |=> idx_o == $past(idx_o)) else $fatal(1, "lock");
endmodule
