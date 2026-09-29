// A dynamic word select over an unpacked array (bedrock br_flow_mux_select,
// reduced). `data[select]` lowers to a right shift of the packed array by the
// AFFINE amount `(select << 5) + 32` -- a 4:1 select of 32-bit words, whose
// shift amount always has five constant-zero low bits.
//
// The shifter is enclosed as a `ware` module. Enclosed naively, the amount was
// an opaque module input: ABC built all eight barrel stages over 160 bits
// (1744 gates, 6.5x yosys' combinational area). The module now keeps the affine
// amount inside its body and the blaster selects among words directly (293
// gates). This fixture proves the word select stays equivalent under both
// mappers.
module dynamic_word_select #(
    parameter int NumWords = 4,
    parameter int Width    = 32
) (
    input  logic [$clog2(NumWords)-1:0] select,
    input  logic [Width-1:0]            data   [NumWords],
    output logic [Width-1:0]            word,
    output logic [NumWords-1:0]         onehot
);
  assign word   = data[select];
  assign onehot = NumWords'(1) << select;
endmodule
