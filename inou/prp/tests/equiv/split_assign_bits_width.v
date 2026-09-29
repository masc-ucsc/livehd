// Ordinary partial continuous assignments are concurrent and supported.  In
// particular, mentioning the destination inside $bits is a type query, not a
// runtime read-after-write dependency.  This is the shape used by Minion's
// split floating-point result buses.
module split_assign_bits_width (
  input  logic [8:0]  hi_i,
  input  logic [6:0]  lo_i,
  output logic [15:0] result_o
);
  assign result_o[15:7] = {($bits(result_o[15:7])){1'b1}} & hi_i;
  assign result_o[6:0]  = lo_i;
endmodule
