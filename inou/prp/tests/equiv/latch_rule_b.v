// FIXME tracker -- latch contract rule B, and it is a DOUBLE FALSE POSITIVE.
// A `case` with no `default:` makes the slang reader infer a LATCH, ignoring that
// the items are EXHAUSTIVE over the selector (a 1-bit `sel` with both `1'b0` and
// `1'b1` arms is provably combinational -- yosys reports zero $dlatch for this
// module). The inferred latch then holds its own value on the (unreachable)
// unmatched path, so the contract check sees Q -> D and rejects rule B.
// This is the single biggest compile-failure category in the corpus (21 files,
// including a plain 4-way `mux4`, which has no state at all).
// FIX: definite_blocking_writes must treat a case whose constant items cover the
// selector's whole value space as fully assigned.
//
// SECOND, INDEPENDENT bug in the same repro (found by delta-reduction): even for
// a GENUINE latch, a hand-written `match` on a state target fails rule B while
// the semantically identical `if/elif` COMPILES CLEAN -- so `match` lowers the
// hold into `D = mux(cond, val, Q)`, materializing the Q->D self-loop, where the
// if/elif path correctly produces D=val plus a latch ENABLE. Rule B is behaving
// correctly given the netlist it is handed. A real fix needs BOTH halves; fixing
// only the case-coverage half leaves the genuinely-incomplete-case latches
// (e.g. the corpus StateMachine.v `case({ld,ce})` blocks) still rejected.
module latch_rule_b (
    input            sel,
    input      [3:0] a,
    input      [3:0] b,
    output reg [3:0] q
);
  always @(*) begin
    case (sel)
      1'b0: q = a;
      1'b1: q = b;
    endcase
  end
endmodule
