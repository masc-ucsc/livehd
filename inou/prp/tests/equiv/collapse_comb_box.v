// Collapse tracker (NOT a phase issue — it needs none of the phase machinery).
//
// `u_fn` is a PROVEN STATELESS child, so pass/lec collapses it into a Comb_box
// whose outputs are APPLY_UF(uf_cb:<def>:<port>, in_concat). That branch returns
// before the `bbin:` obligation loop, so a comb box emits NO per-port compare
// points and has no analogue of the state-box `boxcong` rule. ABC abstracts an
// APPLY_UF to a free input keyed by term identity, so the two sides'
// applications become unrelated primary inputs and `nxt:acc` — whose cone reads
// the child's output — cannot be discharged by the cone pass.
//
// THIS PAIR PROVES TODAY, and that is expected: at 15 lines cvc5 discharges the
// one cut the cone pass could not, so the VERDICT is fine. What the fixture
// pins is the mechanism, which is already visible with LEC_CONE_LOG=1:
//     [LEC_CONE] nxt:acc   DIFF   abc  pis=9 ands=29
//     ... cones: 1/2 PROVEN (residue 1: 1 diff); decomposed (1 cuts each UNSAT)
// The cone-pass property itself is gated by //pass/lec:lec_comb_box_cone_test,
// which asserts that cut is discharged; this pair is the equivalence half and
// the regression guard for the fix.
//
// Measured at scale on minion, where cvc5 cannot mop up: 52 of
// minion_dcache_top's 78 residual cuts and all four of vpu_lane's come from
// exactly this, and minion_dcache_top reaches PROVEN the moment its four comb
// boxes are expanded. Note the residue also violates 2f-lec's own acceptance
// criterion, "collapse yields the same verdict as expansion".
//
// instance_collapse_order already covers a purely combinational parent; the
// missing ingredient here is a STATEFUL parent whose next-state cut reads the
// comb box's output.
//
// The Pyrope twin keeps the same hierarchy but computes the child differently:
// (a & b) ^ (a | b) == a ^ b.
module collapse_comb_box_fn (
  input  wire [2:0] a,
  input  wire [2:0] b,
  output wire [2:0] y
);
  assign y = (a & b) ^ (a | b);
endmodule

module collapse_comb_box (
  input  wire       clk,
  input  wire [2:0] a,
  input  wire [2:0] b,
  output wire [2:0] q
);
  wire [2:0] mixed;
  reg  [2:0] acc;
  collapse_comb_box_fn u_fn(.a(a), .b(b), .y(mixed));
  always @(posedge clk) acc <= acc ^ mixed;
  assign q = acc;
endmodule
