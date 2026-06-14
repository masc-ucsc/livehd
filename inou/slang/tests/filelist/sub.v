// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// A leaf module brought in by the `-F filelist.f` passthrough (see README).
module sub(
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] y
);
  assign y = (a & b) | (a ^ b);
endmodule
