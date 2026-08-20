// Two disjoint continuous assigns to one net, plus a read of the lane that is
// written by the LATER statement. Continuous assigns are concurrent, so `hi`
// settles to `b` regardless of the order the two writes are spelled in.
module wire_lane_written_later (
  input  [3:0] a,
  input  [3:0] b,
  output [7:0] z
);
  wire [7:0] w;
  wire [3:0] hi;
  assign hi     = w[7:4];
  assign w[3:0] = a ^ hi;
  assign w[7:4] = b;
  assign z      = w;
endmodule
