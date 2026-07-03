// Golden for instance_collapse_order — hierarchical-LEC STATELESS
// box-instance pairing guard (formerly a known-failing tracker; the residual
// BypassNetwork LEC-lhd fail after lec.gold_x=ignore removed its
// X false-refute).
//
// THE (FIXED) ISSUE: `lec.hierarchical` proves each submodule def
// leaves-first, then COLLAPSES the proven def into an opaque box inside the
// parent. The old collapse shared ONE output symbol between the two designs'
// "corresponding" box instances and compared their INPUTS (the `bbin:` taps),
// with correspondence keyed `defname#N` by a GRAPH-TRAVERSAL occurrence
// counter — so with several interchangeable instances of the SAME def, the
// slang-read reference and the prp-read implementation could enumerate them
// in different orders, miter lane i against lane j, and refute an EQUIVALENT
// pair (in BypassNetwork: 24 UIntExtractor_24 instances; the cycle-0 taps
// demonstrably sliced [6:0] on one side vs [7:1] on the other).
//
// THE FIX (this pair now PROVES under the default hierarchical run):
// a stateless collapsed leaf is PAIRING-FREE — every output is
// UF_def_port(inputs), one uninterpreted function per (def, port) shared
// across both designs and all instances, so the solver's congruence closure
// pairs instances dynamically and no correspondence key exists to get wrong
// (Comb_box in pass/lec/encode.hpp). Stateful leaves, which genuinely need a
// per-instance-pair state cut, pair by canonicalized instance hier-NAME first
// (tests/equiv/instance_state_order) with an occurrence fallback whose
// spurious refutes are rescued by the driver's flat confirmation
// (tests/equiv/instance_state_anon).
//
// This golden declares four identical `icso_lane_parity` instances on the
// four byte lanes of `d`, in ASCENDING lane order; the .prp declares the same
// four instances in DESCENDING order.
module icso_lane_parity(
  input  [7:0] a,
  output       p
);
  assign p = ^a;
endmodule

module \instance_collapse_order.top (
  input  [31:0] d,
  output [3:0]  q
);
  wire p0, p1, p2, p3;
  icso_lane_parity u0(.a(d[7:0]),   .p(p0));
  icso_lane_parity u1(.a(d[15:8]),  .p(p1));
  icso_lane_parity u2(.a(d[23:16]), .p(p2));
  icso_lane_parity u3(.a(d[31:24]), .p(p3));
  assign q = {p3, p2, p1, p0};
endmodule
