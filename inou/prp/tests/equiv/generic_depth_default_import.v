// Golden for generic_depth_default_import.prp: lane 0 and lane 1 of `a`.
module top(input [31:0] a, output [7:0] lo, output [7:0] hi);
  assign lo = a[7:0];
  assign hi = a[15:8];
endmodule
