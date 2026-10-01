// Golden for file_scope_comptime_attr_capture.prp: Z:u6 -> bits 6, max 63.
module file_scope_comptime_attr_capture (
  input  [7:0] b,
  output [7:0] s,
  output [7:0] t,
  output [7:0] m
);
  assign s = b + 8'd6;
  assign t = b + 8'd6;
  assign m = b + 8'd63;
endmodule
