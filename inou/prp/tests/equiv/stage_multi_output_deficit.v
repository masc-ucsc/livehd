// Golden for stage_multi_output_deficit.prp: the same hierarchy (the comb is
// inlined), with each late output's missing cycles as its own flop.
module pp (
  input            clock,
  input      [7:0] a,
  output reg [7:0] x,
  output reg [7:0] y
);
  always @(posedge clock) begin
    x <= a;
    y <= a ^ 8'h0F;
  end
endmodule

module lag (
  input            clock,
  input      [7:0] a,
  output reg [7:0] q1,
  output reg [7:0] q2
);
  reg [7:0] d2;
  always @(posedge clock) begin
    q1 <= a;
    d2 <= a ^ 8'h33;
    q2 <= d2;
  end
endmodule

module mid (
  input        clock,
  input  [7:0] a,
  output [7:0] m1,
  output [7:0] m2
);
  wire [7:0] q1;
  reg  [7:0] v_q1;
  lag v (.clock(clock), .a(a), .q1(q1), .q2(m2));
  always @(posedge clock) v_q1 <= q1;
  assign m1 = v_q1;
endmodule

module stage_multi_output_deficit (
  input        clock,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  output [7:0] o1,
  output [7:0] o2,
  output [7:0] o3,
  output [7:0] o4,
  output [7:0] o5,
  output [7:0] o6
);
  reg  [7:0] t_q1, t_q2, u_x, u_y;
  wire [7:0] x, y;
  pp  u (.clock(clock), .a(b), .x(x), .y(y));
  mid w (.clock(clock), .a(c), .m1(o5), .m2(o6));
  always @(posedge clock) begin
    t_q1 <= a;
    t_q2 <= a ^ 8'h5A;
    u_x  <= x;
    u_y  <= y;
  end
  assign o1 = t_q1;
  assign o2 = t_q2;
  assign o3 = u_x;
  assign o4 = u_y;
endmodule
