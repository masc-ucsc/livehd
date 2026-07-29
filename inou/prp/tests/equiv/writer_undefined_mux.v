// FIXME tracker -- prp_writer emits a `_mux_N` it never DECLARES.
// Look at the Pyrope below: `_mux_1` is read but never defined anywhere, so our own
// parser rejects our own output ("read of undefined variable '_mux_1'"). The shape
// is an expression mux (a ternary) whose result is then overwritten by a
// conditional assignment nested in an outer conditional: the writer folds the pair
// into one if-expression but keeps the ternary only BY REFERENCE.
// A writer self-consistency bug -- generated Pyrope must always re-parse.
module writer_undefined_mux (
  input        s0,
  input        s1,
  input        cond,
  output reg [1:0] out
);
  always @(*) begin
    out = 2'd0;
    if (s0) begin
      out = s1 ? 2'd1 : 2'd2;
      if (cond) out = 2'd3;
    end
  end
endmodule
