// prp_writer regression golden -- it used to emit a `_mux_N` it never DECLARED.
// The ternary below lowers to a merge temp whose single use (`out = _mux_1`) the
// enclosing `if (cond)` mux collapse had ALREADY folded away, so inlining the
// mux expression at that store dropped the temp's definition and left a bare
// `_mux_1` read: generated Pyrope that our own parser rejects ("read of
// undefined variable '_mux_1'"). A writer self-consistency bug -- generated
// Pyrope must always re-parse. Root cause and fix: see the sibling .prp.
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
