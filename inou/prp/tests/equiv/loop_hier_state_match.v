// Behavioral golden for loop_hier_state_match.prp: two explicit `lane`
// instances, one per source-loop iteration.
//
// NOTE the .prp side now compiles with `compile.upass.roll=true`, so its
// hierarchy is NOT this shape — the loop stays rolled as one replicated node
// whose body (`__loop0`) holds a single `lane`, and cgen expands that to two
// `__loop0` occurrences. So this golden no longer pairs instance-for-instance;
// LEC proves the two designs equivalent by flattening, which is the stronger
// statement and exactly the property the fixture is for: rolling must not
// change what the design computes.
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
  // The generated memory forwards the ordinary write port, but reset writes
  // are not forwarded until their edge commits.
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

  lane lane_q_li0(.clock(clock), .reset(reset), .x(d[3:0]), .addr(addr), .we(write_mask[0]), .q(q0));
  lane lane_q_li1(.clock(clock), .reset(reset), .x(d[7:4]), .addr(addr), .we(write_mask[1]), .q(q1));

  assign q = {q1, q0};
endmodule
