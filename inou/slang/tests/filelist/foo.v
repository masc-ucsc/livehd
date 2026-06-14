// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Top module of the filelist test; instantiates `sub`, so the compile only
// succeeds when the filelist supplies BOTH sources together (single unit).
module foo(
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] y
);
  wire [7:0] t;
  sub u_sub(.a(a), .b(b), .y(t));
  assign y = t + 8'd1;
endmodule
