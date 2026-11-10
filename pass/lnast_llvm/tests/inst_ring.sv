// A ring of instance-level dependencies with no combinational loop: `u.x` depends only on `u.b`,
// and feeds (through the parent, and through a second instance) back into `u.a`. No statement order
// has every argument of every call ready before the call: lg2lnast calls one instance early and
// passes the missing argument as the settled value of a net assigned further down. `v` reads one
// of its own outputs directly.
module inst_ring_leaf (
  input  logic        clk,
  input  logic [7:0]  a,
  input  logic [7:0]  b,
  output logic [7:0]  x,
  output logic [7:0]  y
);
  logic [7:0] acc;
  assign x = b + 8'd3;        // from b alone
  assign y = (a ^ acc) + b;   // from a, b and state
  always_ff @(posedge clk) begin
    acc <= acc + a;
  end
endmodule

module inst_ring (
  input  logic        clk,
  input  logic [7:0]  in0,
  input  logic [7:0]  in1,
  output logic [7:0]  o0,
  output logic [7:0]  o1,
  output logic [7:0]  o2
);
  logic [7:0] ux, uy, wx, wy, vx, vy;
  // u.a needs w.y, w.a needs u.x: a ring of two instances
  inst_ring_leaf u (.clk(clk), .a(wy),        .b(in0), .x(ux), .y(uy));
  inst_ring_leaf w (.clk(clk), .a(ux ^ in1),  .b(in1), .x(wx), .y(wy));
  // v.a is v's own x
  inst_ring_leaf v (.clk(clk), .a(vx),        .b(in0 ^ in1), .x(vx), .y(vy));
  assign o0 = uy;
  assign o1 = wx + wy;
  assign o2 = vy;
endmodule
