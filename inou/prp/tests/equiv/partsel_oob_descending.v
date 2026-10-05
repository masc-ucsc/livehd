// Descending twin of partsel_oob_ascending: a dynamic part-select entirely
// outside a `[24:0]` vector reads X (IEEE 1800 11.5.1), it does not shift
// everything out to 0. inou/slang resolves it to X, the same as the ascending
// declaration.
module partsel_oob_descending(input [24:0] a, input [2:0] s, output [3:0] y);
  wire [24:0] x;
  assign x = a;
  assign y = x[31 + s +: 4];
endmodule
