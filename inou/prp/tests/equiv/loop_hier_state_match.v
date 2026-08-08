// Behavioral golden for loop_hier_state_match.prp: eight explicit `lane`
// instances, one per source-loop iteration.
//
// NOTE the .prp side now compiles with `compile.upass.roll=true`, so its
// hierarchy is NOT this shape — the loop stays rolled as one replicated node
// whose body (`__loop0`) holds a single `lane`, and cgen expands that to eight
// `__loop0` occurrences. So this golden no longer pairs instance-for-instance;
// LEC proves the two designs equivalent by flattening, which is the stronger
// statement and exactly the property the fixture is for: rolling must not
// change what the design computes.
module lane(
  input        clock,
  input        reset,
  input  [7:0] x,
  input        addr,
  input        we,
  output [7:0] q
);
  // Match the current Pyrope lowering exactly: the declared u8 register has a
  // 9-bit storage node, while its wrapped next value and visible output are
  // narrowed to the low byte.
  reg [8:0] acc;
  reg [7:0] mem [0:1];
  always @(posedge clock) begin
    if (reset) begin
      acc <= 9'b0;
      mem[0] <= 8'b0;
      mem[1] <= 8'b0;
    end else begin
      acc <= {1'b0, (acc[7:0] + x)};
      if (we) mem[addr] <= x;
    end
  end
  // The generated memory forwards the ordinary write port, but reset writes
  // are not forwarded until their edge commits.
  wire [7:0] mem_q = (!reset && we) ? x : mem[addr];
  assign q = acc[7:0] ^ mem_q;
endmodule

module \loop_hier_state_match.top (
  input         clock,
  input         reset,
  input  [63:0] d,
  input         addr,
  input  [7:0]  write_mask,
  output [63:0] q
);
  wire [7:0] q0, q1, q2, q3, q4, q5, q6, q7;

  lane lane_q_li0(.clock(clock), .reset(reset), .x(d[7:0]),   .addr(addr), .we(write_mask[0]), .q(q0));
  lane lane_q_li1(.clock(clock), .reset(reset), .x(d[15:8]),  .addr(addr), .we(write_mask[1]), .q(q1));
  lane lane_q_li2(.clock(clock), .reset(reset), .x(d[23:16]), .addr(addr), .we(write_mask[2]), .q(q2));
  lane lane_q_li3(.clock(clock), .reset(reset), .x(d[31:24]), .addr(addr), .we(write_mask[3]), .q(q3));
  lane lane_q_li4(.clock(clock), .reset(reset), .x(d[39:32]), .addr(addr), .we(write_mask[4]), .q(q4));
  lane lane_q_li5(.clock(clock), .reset(reset), .x(d[47:40]), .addr(addr), .we(write_mask[5]), .q(q5));
  lane lane_q_li6(.clock(clock), .reset(reset), .x(d[55:48]), .addr(addr), .we(write_mask[6]), .q(q6));
  lane lane_q_li7(.clock(clock), .reset(reset), .x(d[63:56]), .addr(addr), .we(write_mask[7]), .q(q7));

  assign q = {q7, q6, q5, q4, q3, q2, q1, q0};
endmodule
