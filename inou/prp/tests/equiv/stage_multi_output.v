// Golden for stage_multi_output.prp: every output is its input (xor a
// constant) delayed by exactly two cycles, through the same two-output
// children the Pyrope instantiates.
module two (
  input            clock,
  input      [7:0] a,
  output reg [7:0] q1,
  output reg [7:0] q2
);
  reg [7:0] d1, d2;
  always @(posedge clock) begin
    d1 <= a;
    d2 <= a ^ 8'h5A;
    q1 <= d1;
    q2 <= d2;
  end
endmodule

module p2 (
  input            clock,
  input      [7:0] a,
  output reg [7:0] x,
  output reg [7:0] y
);
  reg [7:0] dx, dy;
  always @(posedge clock) begin
    dx <= a;
    dy <= a ^ 8'h0F;
    x  <= dx;
    y  <= dy;
  end
endmodule

module stage_multi_output (
  input        clock,
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] o1,
  output [7:0] o2,
  output [7:0] o3,
  output [7:0] o4
);
  two t (.clock(clock), .a(a), .q1(o1), .q2(o2));
  p2  u (.clock(clock), .a(b), .x(o3), .y(o4));
endmodule
