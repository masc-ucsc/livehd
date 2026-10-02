// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// :test: roundtrip
// :lec_timeout: 30
module writer_placeholder_names (
    input struct packed { logic [3:0] _0; logic [3:0] _1; } src,
    input logic [3:0] _,
    output logic [3:0] _2
);
  logic [3:0] _123;
  assign _123 = src._0 ^ src._1;
  assign _2 = _123 ^ _;
endmodule
