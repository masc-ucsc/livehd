// Golden for instance_state_order — the STATEFUL box-instance pairing guard.
// Four identical accumulator lanes, instantiated by NAME (u0..u3) in ASCENDING
// lane order; the .prp declares the same named instances in DESCENDING order.
// A stateful collapsed leaf's abstract state cut is shared per corresponding
// instance PAIR, so pairing lane i with lane j diverges the threaded state and
// spuriously refutes — the name-first box correspondence (u0..u3 on both
// sides) must pair them regardless of declaration order.
module icso_lane_acc(
  input        clock,
  input        reset,
  input  [7:0] a,
  output [7:0] s
);
  reg [7:0] acc;
  always @(posedge clock) begin
    if (reset) acc <= 8'b0;
    else       acc <= acc + a;
  end
  assign s = acc;
endmodule

module \instance_state_order.top (
  input         clock,
  input         reset,
  input  [31:0] d,
  output [31:0] q
);
  wire [7:0] s0, s1, s2, s3;
  icso_lane_acc u0(.clock(clock), .reset(reset), .a(d[7:0]),   .s(s0));
  icso_lane_acc u1(.clock(clock), .reset(reset), .a(d[15:8]),  .s(s1));
  icso_lane_acc u2(.clock(clock), .reset(reset), .a(d[23:16]), .s(s2));
  icso_lane_acc u3(.clock(clock), .reset(reset), .a(d[31:24]), .s(s3));
  assign q = {s3, s2, s1, s0};
endmodule
