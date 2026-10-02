// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// :test: roundtrip
// :lec_timeout: 30
// Empty replication lanes consume no bits, even inside nested concatenations.
module concat_zero_replication #(parameter int W = 4) (
    input logic [W-1:0] a,
    output logic [W-1:0] leading, trailing,
    output logic [2*W-1:0] middle, nested,
    output logic [W+1:0] nonempty
);
  assign leading = {{(W-W){1'b0}}, a};
  assign trailing = {a, {(W-W){1'b1}}};
  assign nested = {a, {{(W-W){a}}, a}};
  assign middle = {a, {(W-W){1'b1}}, a};
  assign nonempty = {1'b0, a, 1'b0};
endmodule
