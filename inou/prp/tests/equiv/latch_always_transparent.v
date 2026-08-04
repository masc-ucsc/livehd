// Regression: an ALWAYS-TRANSPARENT latch emitted NOTHING -- output undriven.
//
// An `always @(*)` case with a `default` writes `o` on every path. The reader
// may initially classify the nonblocking assignment as a latch, but cprop
// folds its full-path write condition to enable=true and removes the
// always-open cell. Historically cgen treated the missing enable as malformed,
// leaving Q -- here a module output -- with no driver at all: the netlist read
// X and LEC refuted it.
//
// The fix must also not spell it `always_latch`: a cell that stores nothing is
// combinational, and yosys rejects an always_latch that infers no latch ("No
// latch inferred for signal ... from always_latch process"), so the miter fails
// and there is still no round trip. It goes out as `always_comb` + blocking `=`.
module latch_always_transparent(input [1:0] c, output reg [7:0] o);
  always @(*) begin
    case (c)
      2'd0: o <= 8'hc0;
      default: o <= 8'hff;
    endcase
  end
endmodule
