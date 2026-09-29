// Behavioral golden for loop_hier_state_match.prp: two explicit `lane`
// instances, one per source-loop iteration.
//
// NOTE the .prp side compiles ROLLED by default (`compile.unroll=false`), so
// its hierarchy is NOT this shape — the loop stays rolled as one replicated
// node whose body (`__loop0`) holds a single `lane`, and cgen expands that to
// two `__loop0` occurrences (`u_loop_0__li0.lane_q`, `u_loop_0__li1.lane_q`).
// The instance names below are the ones the UNROLLED lowering produces —
// `<lhs>__li<k>` (upass_runner.cpp loop_inst_suffix) — and pass/lec's
// canon_flop_name folds the rolled wrapper level onto that same spelling, so
// the flat inductive miter pairs each lane's state by name and the proof is
// unbounded, not a bounded BMC. The property the fixture is for: rolling must
// not change what the design computes.
module lane(
  input        clock,
  input        reset,
  input  [3:0] x,
  input        addr,
  input        we,
  output [3:0] q
);
  // Match the current Pyrope lowering exactly: the declared u4 register has a
  // 5-bit storage node, while its wrapped next value and visible output are
  // narrowed to the low nibble.
  reg [4:0] acc;
  reg [3:0] mem [0:1];
  // `reg mem:[2]u4 = 0` resets every entry in ONE cycle, exactly like the
  // scalar `acc` beside it (the memory's whole-array reset).
  always @(posedge clock) begin
    if (reset) begin
      acc <= 5'b0;
      mem[0] <= 4'b0;
      mem[1] <= 4'b0;
    end else begin
      acc <= {1'b0, (acc[3:0] + x)};
      if (we) mem[addr] <= x;
    end
  end
  // The generated memory forwards the ordinary write port; a write suppressed
  // by reset is not forwarded.
  wire [3:0] mem_q = (!reset && we) ? x : mem[addr];
  assign q = acc[3:0] ^ mem_q;
endmodule

module \loop_hier_state_match.top (
  input         clock,
  input         reset,
  input  [7:0]  d,
  input         addr,
  input  [1:0]  write_mask,
  output [7:0]  q
);
  wire [3:0] q0, q1;

  lane lane_q__li0(.clock(clock), .reset(reset), .x(d[3:0]), .addr(addr), .we(write_mask[0]), .q(q0));
  lane lane_q__li1(.clock(clock), .reset(reset), .x(d[7:4]), .addr(addr), .we(write_mask[1]), .q(q1));

  assign q = {q1, q0};
endmodule
