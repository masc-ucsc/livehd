// Golden for instance_state_anon — the flat-confirmation backstop guard.
// The instance names (ua0..ua3) deliberately do NOT match the .prp side's
// destination-variable names (s0..s3), so the name-first box correspondence
// falls back to occurrence order over REVERSED declarations and mispairs the
// lanes. The collapsed parent spuriously refutes; the hierarchical driver's
// flat confirmation must prove it and report PASS.
module icsa_lane_acc(
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

module \instance_state_anon.top (
  input         clock,
  input         reset,
  input  [31:0] d,
  output [31:0] q
);
  wire [7:0] s0, s1, s2, s3;
  icsa_lane_acc ua0(.clock(clock), .reset(reset), .a(d[7:0]),   .s(s0));
  icsa_lane_acc ua1(.clock(clock), .reset(reset), .a(d[15:8]),  .s(s1));
  icsa_lane_acc ua2(.clock(clock), .reset(reset), .a(d[23:16]), .s(s2));
  icsa_lane_acc ua3(.clock(clock), .reset(reset), .a(d[31:24]), .s(s3));
  assign q = {s3, s2, s1, s0};
endmodule
