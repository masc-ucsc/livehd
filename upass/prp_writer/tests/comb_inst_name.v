// Regression fixture: a STATELESS multi-output combinational submodule must keep
// its hierarchical instance name through a v->prp re-compile.
//
// `fwd` is a multi-output `comb` (no reg/latch), so the writer emits it as a
// destructure `(t = fwdunit.p, …) = fwdunit(…)`.  Earlier that destructure path
// dropped the call-site `::[name=]` annotation (it was gated to stateful mods
// only), so with `upass.inline=false` the re-compiled Sub got a synthesised
// `u_fwdunit_<tmp>` name instead of `fwd`, breaking v2prp name correspondence.
module fwdunit (
  input      [7:0] a,
  input      [7:0] b,
  output     [7:0] p,
  output     [7:0] q
);
  assign p = a & b;
  assign q = a | b;
endmodule

module top (
  input      [7:0] x,
  input      [7:0] y,
  output     [7:0] o
);
  wire [7:0] pp, qq;
  fwdunit fwd (.a(x), .b(y), .p(pp), .q(qq));
  assign o = pp ^ qq;   // both outputs used after the instance
endmodule
