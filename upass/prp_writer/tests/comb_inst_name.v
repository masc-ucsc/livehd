// Regression fixture: a STATELESS multi-output combinational submodule must keep
// its hierarchical instance name through a v->prp re-compile.
//
// `fwd` is a multi-output `comb` (no reg/latch), emitted as a whole-bind
// `mut fwd = fwdunit::[name=fwd](…)` with `fwd.port` output reads.  Earlier
// bugs this guards: the `::[name=]` annotation was once gated to stateful mods
// only (synthesised `u_fwdunit_<tmp>` names broke v2prp correspondence), and
// the old destructure form `(t = fwdunit.p, …) = fwdunit(…)` silently dropped
// the output bindings once `hdl` combs stopped being runner-inlined.
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
